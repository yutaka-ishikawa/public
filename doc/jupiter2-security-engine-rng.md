# Jupiter 2 / K3のSecurity EngineをSBI 3001へ利用する案

調査日：2026-11-08。公開ソースを確認した結果であり、実機のRNG取得は未実行。

## 結論

**K3専用のハードウェアRNGドライバがベンダーOP-TEEに存在する。
Zkr非対応のJupiter 2向けに、この実装を再利用する案が有力。**

前の調査は主にOpenSBI、Linux、製品資料が対象で、ベンダーOP-TEEのK3 platform実装まで
追えていなかった。今回 `k3_prng.c`、clock制御、Security Engine定義を確認した。
「利用可能な実装が見つからない」という調査状態を更新する。

ただし、専用のセキュアCPUがあるという説明と、暗号/RNGエンジン、
OP-TEEのtrusted domain、RT24管理コアは区別する。
このソースだけで、独立したセキュアCPUの構成・実機の稼働状態までは断定しない。

## 確認できた構成

| 構成要素 | 公開ソースで確認したこと |
| --- | --- |
| RT24 ×2 / ESOS | Energy Service OS。電力・リアルタイム管理用。secure OSという意味ではない。 |
| WTM系RNG | OP-TEE platform定義に `K3_TRNG_BASE=0xf0703800`。専用ドライバあり。 |
| TE200 crypto engine | K3 platformにbase `0xf0718000` と暗号ドライバ。RNGとは別に扱う。 |
| K3 OP-TEE | `CFG_K3_PRNG ?= y`、有効時 `CFG_WITH_SOFTWARE_PRNG ?= n`。 |
| OpenSBI | `fdt_mpxy_opteed.c` にMPXYを通すOP-TEE dispatcherあり。 |
| U-Boot | K3でoptee.itbをロードし、trusted-domain/MPXYのDT設定をLinuxへ渡す処理あり。 |

OP-TEEのK3設定はRISC-V S-mode、16 core、OpenSBI配下のtrusted domainを使う。
RT24上でOP-TEEが動くという構成ではない。

## 既存RNGドライバの内容

`core/arch/riscv/plat-k3/k3_prng.c` に次が実装されている。

- `hw_get_random_bytes()` と起動時の初期化。
- WTM RNGのFIFO・PRNG制御レジスタ、32 bitデータレジスタの読み出し。
- readyの有限待機、エラー返却、排他。
- 共有clockが閉じられた場合の再有効化と再初期化。
- CPU読み出し経路と、一定サイズ以上向けのDMA経路。

ドライバ名と制御内容はhardware **PRNG** 経路を指す。
定数名にTRNGがあるだけで、返る値が未処理の真性entropyであるとは断定しない。
初期seed、内部reseed、conditioning、起動ごとの出力の性質は仕様・実測で別途確認する。
単純な連続出力検査だけで暗号学的な品質を証明できるとも扱わない。

## 案A：既存ドライバの小さいCPU読み出し経路をSMへ移植

現在のKeystone単独構成には、この案を推奨する。

```text
SBI 3001 → Keystone SMのchecked provider
         → WTM RNGのclock/初期化/ready確認
         → MMIOから32 bit値を必要数取得
         → statusと乱数値を分離して返す
```

3001のRV64出力は8 bytesなので、最初はDMAを使わない。
DMA buffer、cache maintenance、SEC_CIUの高位アドレス制御を持ち込まずに済む。
OP-TEE全体を新しく起動する必要もない。

移植ではOP-TEEのmutex/timeout/MMIO wrapperを、OpenSBI/SM側のlock・timer・I/O helperへ
置き換える。clock状態を確認せずにMMIOを読むと停止し得るため、
ベンダーの `k3_ccu_ensure()` 相当の手順も合わせて確認する。
供給不能時はchecked APIで失敗させ、Eyrieまで伝える。

MMIOアドレスを置くだけでは完成しない。実機のアクセス権とclock/resetを確認し、
LinuxやOP-TEEが同じデバイスを独立してreset・読み出ししない所有方針を決める。
DRBGを併用する場合も、このRNGのseed/reseed仕様を確認してからentropy源として使う。

## 案B：OP-TEEの乱数サービスを呼ぶ

既存OP-TEEを製品で使う場合は、RNGをOP-TEE側に所有させる構成も考えられる。
OP-TEEには `core/pta/hwrng.c` と `CFG_HWRNG_PTA` で選ぶサービスがあり、
GET_ENTROPYは `crypto_rng_read()` を呼ぶ。
K3で実際にそのPTAが有効か、対応する呼び出しが動くかは未確認。

```text
SBI 3001 → SM/OpenSBIの専用bridge
         → OP-TEEのRNGサービス → K3 RNG driver
```

ただし、既存MPXYはS-modeからの要求とtrusted/untrusted domainのcontext切替を前提にする。
M-modeのSM処理から既存MPXY handlerを関数として呼ぶだけでは成立しない。
専用bridge、要求元の識別、buffer、再入、PMP/domainの復帰状態を設計する必要がある。

現在のKeystoneはOpenSBIとPMPエントリの所有・全hart同期を前提にしている。
OP-TEEのdomain切替が設定するPMPと同時に使えることを別途確認する。
そのため、現在の構成では案Aより作業範囲が広い。
LinuxのTEE driverやtee-supplicantへ処理を任せるだけの経路を、SM専用乱数と同一視しない。

## RT24 / RPMIを使う案との違い

ESOSのK3 RPMI実装にはclock、voltage、power domain、RTC、HSM、sysreset等がある。
調査したSpacemiT固有コードには乱数サービスを確認できなかった。
RT24という別CPUがあるだけでは乱数は供給されない。

RT24経由にするなら、RNGへのアクセス可否を確認し、対応するサービスをESOSと
OpenSBIの両方に追加する作業になる。既存WTMドライバの直接移植より先に選ぶ理由は、
デバイス所有を管理firmwareへ集約する必要がある場合などに限る。

## 移植計画への反映

- Zkrが実際に利用可能なら引き続き優先する。
- Jupiter 2の非Zkr経路は、WTM RNGのCPU読み出し移植を第一候補とする。
- 既にOP-TEEを使う構成では、二重所有を避けるため案Bの併用設計を検討する。
- 実機では最初にclock/ready/読み出し/timeoutを確認し、複数hartで繰り返す。
- RNGの初期seed/reseed仕様と実際の返却値を確認した後、3001へ接続する。
- 検証は実SBI・実SM経路で行い、模擬CSR/PMP試験へは戻さない。

この調査で確定したのは、再利用できる**具体的なK3向け実装の存在**。
現在のJupiter 2でのRNG動作、OP-TEE/PTAの稼働、乱数品質はまだ未確認。

## 参照ソース

以下はいずれも公開ソースの調査版を固定した参照。

- [SpacemiT OP-TEE K3 RNG driver](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/k3_prng.c)
- [K3 platform設定・RNG/TE200のアドレス](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/platform_config.h)
- [K3 OP-TEE build設定](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/conf.mk)
- [K3 clock制御](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/k3_ccu.c)
- [OP-TEE hwrng PTA](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/pta/hwrng.c)
- [OpenSBI OP-TEE MPXY dispatcher](https://github.com/spacemit-com/opensbi/blob/ad6091fd16c4d4a445168f6a479e1b8aadb4931c/lib/utils/mpxy/fdt_mpxy_opteed.c)
- [U-Boot K3 OP-TEE DT生成](https://github.com/spacemit-com/uboot-2022.10/blob/289dc2b7bd25bf9d88ce9e7e263e95234868ca14/board/spacemit/k3/k3-optee.c)
- [U-Boot K3 trusted-domain設定](https://github.com/spacemit-com/uboot-2022.10/blob/289dc2b7bd25bf9d88ce9e7e263e95234868ca14/arch/riscv/dts/k3-optee.dtsi)
- [ESOSの役割・RT24構成](https://github.com/spacemit-com/esos/blob/e5cd04556b98c0054256f165cc2c6d4d25326a15/README.md)
