# SBI 3001 RANDOMの移植計画

調査日：2026-11-08。実装前の計画であり、乱数機能の動作確認結果ではない。
既存のOpenSBI・QEMU機能を優先して使い、SMに新しい乱数アルゴリズムを自作しない。

## 推奨構成

```text
アプリ getrandom / AT_RANDOM
 → Eyrieのchecked乱数API
 → ecall: Keystone拡張 0x08424b45 / FID 3001
 → OpenSBIの既存dispatcherとKeystone handler
 → SMのchecked provider
 → OpenSBIのZkr検出 + CSRアクセスhelper
 → RISC-V seed CSR
 → QEMUの既存qemu_guest_getrandom / qcrypto_random_bytes
```

3001はKeystone拡張のfunction IDであり、OpenSBI標準の汎用乱数extensionではない。
今回のOpenSBIソースには、呼ぶだけで乱数バッファを得られる汎用APIは見当たらない。
一方、次の組み合わせなら既存コードを再利用できる。

| 層 | 再利用するもの | 追加・修正するもの |
| --- | --- | --- |
| OpenSBI | 拡張登録・ecall dispatch、`sbi_hart_has_extension(..., SBI_HART_EXT_ZKR)`、CSR helper、既存エラー復帰処理 | 独立した新SBI extensionは追加しない。 |
| QEMU | Zkr、seed CSR、ホスト乱数backend | 起動CPUで `zkr=true` を明示する。原則として新規RNGデバイスは作らない。 |
| SM | 既存FID 3001、platform providerの境界 | statusと値を分離してseedへ接続する。 |
| Eyrie | syscall、AT_RANDOM、既存データコピー処理 | a0/a1の正しい取得とエラー伝播。 |

これは既存OpenSBIの単一関数への置き換えではなく、拡張検出・CSRアクセス・
既存ecall経路と、QEMUの既存乱数実装を組み合わせる移植である。

## Zkr非対応機の設計

[Zkr非対応機の供給元・DRBG設計](keystone-random-fallback-design.md)を追加した。
Zkrを優先し、TRNG/既存管理firmwareサービス、次いで信頼できるSM専用boot seedを
候補とする。機種別entropy providerとSM内の共通DRBGを分離する案を最終形とし、
以下のQEMU直接取得案は先行する最小実装として位置づける。
Linux由来seedを通常の自動fallbackにはせず、供給元がなければ明示的に失敗する。

## 現在の問題

- `sm-sbi-opensbi.c` はK3の3001を先にNOT_IMPLEMENTEDとして拒否する。
- `sm-sbi.c` の `sbi_sm_random()` もK3では未対応。
- `platform/spacemit_k3/platform.c` のchecked providerも未対応。
- Eyrieの `sbi_random()` はa0のstatusを確認せずa1を返す。
  asmもa1を出力として宣言していない。
- `rt_util_getrandom()` は供給失敗を検出せず要求バイト数を成功として返す。
- ENV_SETUPのAT_RANDOM、Linux getrandom、paging/page cryptoがこの経路に依存する。

したがって、SMのNOT_IMPLEMENTEDだけを外す変更では完了しない。

## 1. QEMU/OpenSBIのZkr経路を確定する

既存の `rva23s64` プロファイルだけでZkrの利用可を保証しない。
Keystoneの乱数対応プロファイルで `-cpu ...,zkr=true` を明示し、
QEMUが生成するDTのISA情報と、OpenSBI側の検出結果が一致することを確認する。
`zkr=false` の非対応試験も実行できる起動オプションを用意する。

QEMUの `riscv_new_csr_seed()` は `qemu_guest_getrandom()` で2 bytesを取り、
成功時にES16、失敗時にDEADを返す。通常の非決定的実行では
`qcrypto_random_bytes()` に接続される。
決定的seed設定やrecord/replayの結果を通常の乱数品質評価として扱わない。

OpenSBIはZkr検出時にmseccfg.SSEEDを設定し、S-modeにもseedへのアクセスを許す。
M-modeのSMから利用するために、この権限方針を変更する必要はない。
Linuxのseed利用と排他的所有が必要だという前提も置かない。

## 2. SMにchecked providerを接続する

既存 `k3_platform_random_checked(uint64_t *value)` を実装の境界として使う。

- 現在のhartでZkrを検出してからCSRにアクセスする。
- seedはread-write命令で読む。`csr_swap(CSR_SEED, 0)` 等のCSRRWを使用する。
  通常の `csr_read()` / CSRRSによる読み取り専用アクセスは使わない。
- OPSTを検査し、ES16のデータだけを採用する。
- WAIT/BISTは有限回・有限時間で打ち切り、供給待ちとして返す。
- DEADは供給元の故障、未対応はNOT_IMPLEMENTEDとして区別する。
- 失敗時は出力値をクリアし、途中まで集めた値を成功として返さない。

QEMU初期移植では、既存crypto backend由来のES16を4回集めてRV64の64 bit値を返す
薄いproviderを第一候補とする。新しいPRNGやDRBGをSMへ追加する必要はない。
この判断はQEMU実装の供給元を確認した上でのもの。
実機のseed/TRNG生出力にも同じ品質・conditioning条件を仮定しない。

既存の乱数未対応分岐をこのprovider呼び出しへ置き換える。
handlerは既存のenclave context検査を通してから3001を処理し、
通常Linuxからの直接要求はSBI_PROHIBITEDとして拒否する。

## 3. ABIとEyrieのエラー処理を修正する

成功時のABIは維持する。

```text
a0 = status（0で成功）
a1 = RV64の乱数値
```

データ値0や全bitが1であることをエラー判定に使わない。
未対応・一時的な供給待ち・供給元故障に対応するstatusを明文化し、
必要な追加statusは既存コードと衝突しない値で定義する。

Eyrieに `sbi_random_checked()` を用意し、a0とa1を同じinline asmの出力として取得する。
旧 `sbi_random()` の利用箇所も更新し、失敗したa1を消費する経路を残さない。
対象は `call/sbi.c`、`util/rt_util.c`、`call/linux_wrap.c`、`sys/env.c`、
`mm/paging.c`、`mm/page_swap.c` と対応するheader。

内部の乱数バッファ充填とユーザーメモリへのコピーを区別し、
AT_RANDOM用のruntime内バッファと、getrandomのU-modeバッファを適切に扱う。
getrandomは成功バイト数・途中までの取得・未取得時のエラーを区別する。
非整列バッファ、コピー失敗、長さ0も扱い、flagsを無条件に無視しない。

AT_RANDOMの16 bytesを得られなければ、未初期化データで起動を続けず、原因を表示して
失敗する。乱数未対応環境で従来のENV_SETUPが見かけ上動いていた場合、
正しいエラー伝播を入れると起動失敗として表面化する点を明記する。
paging/page cryptoでも鍵・IV等の乱数取得失敗を成功として使わない。
3002 attestationや3003 sealingを、この変更だけで有効化しない。

## 4. 実QEMUゲスト統合試験を追加する

`verifications/random` を新設し、既存run-allへ組み込む。
製品関数の抽出試験や模擬SBI/PMP/CSRは追加しない。

### Zkr有効の起動

- 実enclave内から3001のchecked経路とgetrandomを使う。
- 0、1、7、8、9、15、16、17、32、4096 bytes、非整列バッファ、前後canaryを検査する。
- 取得データをOCALLで転送し、RESUME後にも内容が保存されていることを確認する。
- ENV_SETUPありのELFでAT_RANDOMを検査する。
- 繰り返し実行、複数enclave、Linux runnerのCPU affinityを使ったhartごとの実行を確認する。
- 既存2 MiB/256 MiB edgecallの正常経路も維持する。
- ゲストの検証モジュールから3001を直接呼ぶとSBI_PROHIBITEDとなることを確認する。

返った値の変化だけで乱数品質や暗号学的強度を証明したとはしない。
正当な0、同じ値の偶然の再出現も、単純な値検査でエラーにしない。

### Zkr無効の起動

- ENV_SETUPなし、必要なsyscallだけを有効にしたruntimeで、未対応statusが
  アプリのgetrandomエラーまで届くことを確認する。
- ENV_SETUPありの場合は、AT_RANDOM供給不能を明示して起動失敗になることを確認する。
- 非対応CSRへのアクセスでSMがillegal instructionや無限待ちにならないことを確認する。

既存QEMUのseedモデルは通常ES16、backend失敗時DEADなので、WAIT/BIST/DEADの
全状態を通常実行で再現できるとは限らない。再現できない分岐は未検証と記録し、
ソース確認だけを実ゲストPASSに置き換えない。

## 5. 実機Jupiter 2へ接続する

[実機Zkr対応の追加調査](jupiter2-zkr-investigation.md)では、公式製品資料、
K3の全16 CPU定義、SpacemiT公式OpenSBIも照合したが、対応の根拠は確認できなかった。
**実機はZkr非対応として計画し、仕様・M-mode実測で確認された場合だけ有効化する。**
RVA23のZkrはoptionalなので、RVA23準拠だけで利用可能とは判断しない。

提供済みの実機の共通ISA情報には `zkr` が含まれていない。
これはハードウェアに存在しないことの証明ではないが、利用可能とは仮定できない。
X100/A100の両CPU群について、M-modeでの機能情報とベンダーのTRNG実装を確認する。

Zkrが使えれば同じprovider境界を再利用する。
使えない場合は、ベンダーOpenSBI/RPMIサービスまたはM-modeからアクセス可能な
TRNG/Security Engineの既存実装を優先して接続する。
アドレス、clock/reset、初期化、ready/error bit、排他、必要なconditioningが分かれば
Linuxドライバ等のレジスタ処理も参考にできるが、LinuxのAPI自体をSMから呼ぶことはできない。

QEMU向けにZkrを有効にすることと、実機の機能追加は別。
ゲストLinuxのgetrandomやvirtio-rngへOCALLするだけでは、SM自身の乱数供給と
同じ信頼境界にはならないため、この移植の既定経路にはしない。

実機側で確信度を上げる情報は以下。

- ベンダーのTRNG/RNGサービスのソースまたは仕様、MMIO/呼び出しABI。
- X100/A100のZkr有無とseedアクセス条件。
- TRNG出力の品質・conditioning・health/error処理の仕様。

既存OpenSBIに汎用APIがない以上、供給元なしでCSR helperやhashを組み合わせても
乱数は作れない。実機の供給元確認は必要条件として残す。

## 実装順序と完了判定

1. QEMUのZkr有効/無効を起動設定から切り替え、OpenSBIの検出を確認。
2. SMのchecked providerと既存3001 adapterを接続。
3. Eyrieのa0/a1、getrandom、AT_RANDOM、paging呼び出しを修正。
4. 実QEMU統合試験を実行し、有効/無効の両起動の結果を保存。
5. 実機の供給元を調査し、同じprovider境界へ追加。

QEMU版の完了条件は、実ecallでの成功・未対応の伝播、AT_RANDOM、既存enclave試験の
回帰が確認されること。実機対応やattestation/sealingの完成とは分けて報告する。
