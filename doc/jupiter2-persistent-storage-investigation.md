# Jupiter2のpersistent storageとNV Counter調査

- 調査日：2026年11月10日（日本時間）。新たな実機操作・書込み試験は行っていない。
- 結論：**永続領域が存在しないとは言えない。K3には資料上NV Counterが存在する。**
  ただし、TPM相当counterの安全な保存先として使えることはまだ確認していない。
- 追加調査：第5節。NV CounterはeFuse bank 5のARCN（Non-secure 224 bit／Secure 32 bit）で、
  bit消費型として扱う根拠を確認。1段の符号化と正式な更新APIは未確認。
- 設計：[PCR・counter・Quote統合設計の第9.6〜9.12節](keystone-tpm2-quote-design.md)。

## 1. 公式K3 datasheet

出典：[k3_ds.md（固定commit）](https://github.com/spacemit-com/docs-chip/blob/3ea42a6af47f2595428e2870122829dc5e8fc856/en/key_stone/k3/k3_docs/k3_ds.md)

- commit：`3ea42a6af47f2595428e2870122829dc5e8fc856`。
- 取得内容SHA-256：`e9a202a1064cd5a4273f6c65b5831f21f7ade63529b72c8b59043bcb863d4032`。
- 第2.8.3節 eFuse：4096 bit、256-bit bank×16、user customization 256 bit。
- 同節の機能欄に **“Supports 256-bit non-volatile counter (NV Counter)”** と明記。
- 第2.2.4節：eMMC 5.1 host controller対応。外部eMMCを接続する機能の記載。

これらはSoC機能の仕様であり、個々のJupiter2 boardのfuse残量・権限・provisioning・
eMMC搭載やRPMB利用可能性を示す実機試験結果ではない。
追加調査では、第5節のとおりeFuse bank 5の224＋32 bitという資源割当てを確認した。
書換可能な256-bit整数counterと解釈しない。1段の消費量と更新API、user customization領域のbank対応は未確認。

## 2. 以前提供された実機ログ

根拠：[hardware inventory](jupiter2-hardware-inventory.json)でhashを管理している
`jupiter2-info.tar.gz`の`dmesg.txt`と`running.dtb`。
ログを再読した結果であり、今回実機を動かして採取したものではない。

```text
[    2.282648] spi-nor spi4.0: supply vcc not found, using dummy regulator
[    2.319323] 6 cmdlinepart partitions found on MTD device d420c000.spi
[    2.319334] 0x000000000000-0x000000020000 : "bootinfo"
[    2.320117] 0x000000020000-0x0000000a0000 : "fsbl"
[    2.320903] 0x0000000a0000-0x0000000b0000 : "env"
[    2.321679] 0x0000000b0000-0x0000001b0000 : "esos"
[    2.322431] 0x0000001b0000-0x000000210000 : "opensbi"
[    2.323182] 0x000000210000-0x000000800000 : "uboot"
```

SPI NORが使われていることは確認できる。末尾partitionの境界は0x800000。
ただしここから全物理容量や空き領域を決め付けない。
既存領域はboot用なので、counter journalを無断で割当てない。
flashに保存できることと、過去imageへの巻き戻しを検出できることは別である。

今回参照した既存資料から、搭載eMMCの型番・RPMB容量・鍵設定は確認できなかった。
ログに見えないことを、boardにeMMC/RPMBの選択肢が存在しないという証拠にはしない。

## 3. repository内のeFuse定義とOP-TEE設定

- [K3 DT source](../linux-k3/arch/riscv/boot/dts/spacemit/k3.dtsi)に
  `fuse@f0702800`、efuse bank0/bank7、die ID等の定義がある。
  読出し用のDT nodeの存在だけでは、NV Counterの安全な更新APIは分からない。
- [K3 OP-TEE conf.mk](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/conf.mk)
  に`CFG_K3_EFUSE ?= y`とread-only accessのcommentがある。
- 同じconf.mkには`CFG_TE200_USE_DUMMY_OTP ?= y`、`CFG_K3_HUK_ALLOW_UNFUSED ?= y`があり、
  blank fuse時にpublic development HUKを使うという説明がある。
  これは開発設定であり、実機のHUKが秘密かつprovision済みであることの証拠ではない。

## 4. 次に確かめる事項

1. K3のNV Counter programming manual：表現、残量、更新単位、権限、lock、power-fail semantics。
2. 実機のfuse lifecycleとpublicな残量/能力情報。生の秘密fuse dumpは不要。
3. eMMC搭載の有無と型番、RPMB対応/容量、key provisioning、認証I/O経路。
4. OP-TEE/secure processorの正規HUK・storage backend・SM専用アクセス制御。
5. protected counter stateとAK blobの一貫したcommit・recovery・rollback拒否。

eFuse/RPMB keyのprogrammingは不可逆になり得る。今回の調査では実施していない。
文書だけの確認、QEMUでのfile-backed復旧、Jupiter2実機での耐久性・権限試験を分けて記録する。

## 5. bit消費型かの追加調査

2026-11-10、公開manualとK3 branchの実コードを照合した。
**通常の書換可能な256-bit整数counterではなく、eFuseの不可逆bitを消費する方式として扱う根拠がある。**
ただし、BootROMが使うcounter値の符号化と、正式な1段更新の手順までは公開コードから確認できていない。

### 5.1 K3 manualで領域の内訳を確認

[公式K3 user manual第15.3節](https://github.com/spacemit-com/docs-chip/blob/3ea42a6af47f2595428e2870122829dc5e8fc856/en/key_stone/k3/k3_docs/k3_usermanual/15_security.md#153-efuse)
はeFuse内のNV Counterについて次の内訳を明記している。

```text
Supports 256-bit non-volatile counter (NV counter):
  224 bits for non-secure modules, 32 bits for secure modules
```

256-bit幅の汎用加算器という説明ではなく、secure/non-secure用のeFuse資源の割当てである。

### 5.2 K3 U-Boot branchでbankと用途を確認

対象は[ベンダーU-Bootのk3-br-v1.0.y](https://github.com/spacemit-com/uboot-2022.10/tree/289dc2b7bd25bf9d88ce9e7e263e95234868ca14)。
同repoのdefault branchはK1用なので、K3 branchの固定commitを取得して確認した。

[drivers/misc/spacemit_k1x_efuse.c](https://github.com/spacemit-com/uboot-2022.10/blob/289dc2b7bd25bf9d88ce9e7e263e95234868ca14/drivers/misc/spacemit_k1x_efuse.c)
には以下が定義されている。ファイル名はk1xだが、K3 branchにも存在する。

```c
#define EFUSE_ARCN_OFFSET (0x2C0) /* bank 5, Anti-rollback Counter Number */
/* struct efuse_data_core */
uint8_t arcnns[28]; /* bank 5: 224-bit Anti-Rollback Counter, Non-Secure Image */
uint8_t arcnse[4];  /* bank 5: 32-bit Anti-Rollback Counter, Secure Image */
```

従って、資料の256 bitと一致するbank 5のARCN（Anti-Rollback Counter Number）を確認できる。
Secure/Non-secure **image** のrollback防止用であり、自由に使えるTPM NV領域とは確認できない。
register offset 0x2c0と、softwareのflat bank offset空間は異なる。両者を混同しない。

同driverの汎用`efuse_write_bank()`は、既存データへ書込み要求bitをORしてからburnする。

```c
efuse_data[byte_offset + j] |= ((uint8_t *)buf)[i + j];
```

`efuse_write_bank_core()`は高電圧・FUSE_BURN_ENABLEを使ってprogramし、burn完了を待つ。
このコードは、書換可能RAM/flash counterの通常の加算・上書きではなく、
既存1 bitを消さずに新しいbitを立てるeFuse書込みを示している。

**ただし、汎用write入口にはbank_start < 8を拒否する条件がある。**
そのため、この関数をbank 5へそのまま使ってcounterを更新できるとは言えない。
ARCN専用のincrement/read-value関数、popcount、thermometer-code decoderは確認できなかった。
汎用OR/burn処理はeFuseの性質の根拠であり、bank 5の正式な更新APIの根拠とは分ける。

### 5.3 OP-TEEとLinuxから確認できること

K3 OP-TEEの[k3_efuse.h](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/k3_efuse.h)と
[k3_efuse.c](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/k3_efuse.c)
は、fuseはmanufacturing時にU-Bootからprogramし、secure worldはread-onlyという方針を明記する。
同コードのbank offset表にはbank 5が0x2c0として含まれるが、counter更新を実装してはいない。

[本repoのLinux driver](../linux-k3/drivers/nvmem/spacemit-efuse.c)も
`read_only = true`と`reg_read`を登録する読出し用。NV Counter更新の根拠にはならない。
OP-TEEの[汎用nv_counter.c](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/kernel/nv_counter.c)
のweak APIは未実装errorを返す。このAPI名の存在をK3 ARCN更新の実装済み証拠にしない。

### 5.4 何回使えるかと設計への影響

0→1にしか更新できないN個のbitを使い、counterを進めるたびに少なくとも1個の未使用bitを
消費するなら、全未使用状態からの有効な更新回数は最大N回である。
二進整数として256 bitあることから2^256回の更新ができる、とはならない。

- 1段＝1 logical bit消費の方式なら、Non-secure側は最大224段、Secure側は最大32段。
- この数は**条件付き理論上限**。K3が1段1 bitとすることや、全bitが使用可能なことは未確認。
- redundancy/ECC、複数bitの1段消費、既存provision、lockにより利用可能段数は減り得る。
- `count = popcount(fuse)`なのか、連続1の長さなのか等の正式な変換規則は未確認。
- manufacturing済み実機の使用済みbit・残量を調べたわけではない。

例えば`0000 → 0001 → 0011 → 0111 → 1111`はbit消費型の説明例であり、
K3の実際の符号化を確認した例ではない。

TPMの高頻度NV_INCREMENT、bootごとのresetCount、Clock checkpointを直接この領域で
更新するbackendには採用しない。低頻度のfirmware anti-rollback／世代anchorの候補として
残すが、既存boot chainのARCNとの所有権・用途衝突を先に解消する必要がある。
通常counterの永続化には、別の書換可能な認証storage（例：実機で利用可能と確認できたRPMB）を調査する。

### 5.5 残る確認事項

ベンダーへ確認すべきなのは「256 bitありますか」ではなく、次の仕様である。

1. ARCN-NS/ARCN-Secのvalue decoderと1段incrementの具体的なbit更新規則。
2. 各領域の最大段数、ECC/冗長化、blank/invalid pattern時の動作。
3. bank 5の正規更新API・権限・lock・電源断時の原子性と復旧。
4. 出荷時の使用済み段数と、顧客による利用が許可される範囲。
5. BootROM/secure bootがこの領域を消費する条件、software counterとの共有可否。

実機のburn、lock変更、raw secret fuseのdumpは実施していない。

## 6. 追加調査の取得内容hash

固定commitへのリンクは第5節に記載。2026-11-10取得、改行を含むraw bytesのSHA-256。

| 対象 | SHA-256 |
| --- | --- |
| K3 manual 15_security.md | `7543a07e2ac1302a342400fbdae65a5f008aeb98861bb6d3f9d74ddfd83cda84` |
| K3 branch U-Boot spacemit_k1x_efuse.c | `1e4f3b5b43b2588d6a1197e14a89752bce47a9dac2996c9e3ed1772f4fd9127f` |
| K3 OP-TEE k3_efuse.c | `82b4f01a79a2e79e5f48a858d9f656e49d70528b4f203f960bd1adb5e48b922a` |
| K3 OP-TEE k3_efuse.h | `5df6f367fe6fe8e111cba2ca081387dcef3f1053bc8834c0f68e8d0207aac772` |
