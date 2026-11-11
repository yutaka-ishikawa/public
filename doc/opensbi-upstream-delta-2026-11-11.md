# 現行K3/Keystone OpenSBIと上流最新版の差分・バックポート計画

調査日：2026-11-11（JST）。比較はソース・Git履歴に基づく。
今回は文書と計画を更新し、firmwareの修正・ビルド・実QEMU/実機実行はしていない。

## 1. 結論と差分の規模

現行は**OpenSBI v1.6を起点とするSpacemiT K3派生版＋Keystone独自変更**。
上流の最新リリースは **v1.9**、最新開発版はその後の **127 commit** を含む。
バージョン名だけで未適用件数を数えると、SDKが既に取り込んだ修正まで重複する。

| 指標 | 結果 | 意味 |
| --- | --- | --- |
| 上流v1.6→v1.7 | 126 commit | 履歴上の増分 |
| v1.7→v1.8 | 115 commit | 同上 |
| v1.8→v1.9 | 133 commit | 同上。v1.6→v1.9は374 commit |
| v1.9→最新master | 127 commit | リリース後の変更も含めて比較 |
| v1.6→最新master | **501 commit** | 本調査の全件棚卸し範囲 |
| 現行opensbi/→v1.9 | **357 files、+15,761 / -10,519 lines** | 上流へ置き換えた場合のtree差分。rename検出あり |
| 現行opensbi/→最新master | **390 files、+20,155 / -11,784 lines** | 同上。独自機能の削除も含み、未適用patch量ではない |
| SDK履歴とのpatch-id一致 | 18 commit | 501件中。現行の全機能が18件だけという意味ではない |

501件のうち修正・防御強化として分類した172 commitは、以下のように扱う。
commit数であり、172個の現行不具合・脆弱性を発見したという意味ではない。

| 判定 | 件数 | 計画上の扱い |
| --- | --- | --- |
| バックポート対象 | **111** | 現行コード向けの採用審査・実装対象。後続修正/依存を組にする |
| 条件付きバックポート | **37** | 対象ISAや先行機能を導入する時に同梱。現行に不要な修正も含む |
| 取込済 | **5** | 二重適用しない |
| K3対象外 | **19** | 他SoC専用。K3には取り込まない |

機能・基盤変更158件、整理・仕様更新152件、初期抽出後に非不具合/依存へ分けた19件も含め、
**全501件**を[commit inventory CSV](opensbi-upstream-commit-inventory.csv)へ記録した。
修正172件の個別SHA・初出release・採否は[バックポート全件台帳](opensbi-upstream-backport-catalog.md)にある。
これは移行判断の棚卸しであり、全不具合の不存在を保証する監査ではない。

## 2. 固定した比較元・比較方法

| 対象 | 固定ref / SHA |
| --- | --- |
| 本リポジトリ | `main` / `779fb85a764e678a3c9bbbac1450e4c434cb319d` |
| 実際に比較した部分 | 上記commitの `opensbi/`。稼働中実機のBINを採取した調査ではない |
| SpacemiT比較元 | `k3-br-v1.0.y` / `ad6091fd16c4d4a445168f6a479e1b8aadb4931c` |
| 上流v1.6 | `bd613dd92113f683052acfb23d9dc8ba60029e0a` |
| 最新release v1.9 | `cbf9f6734dd85a982c63e3cb5db7ffe09da839ca`（2026-07-01公開） |
| 最新master | `ed70256dd22c09962d146e433834239825ef0616`（2026-11-10 commit） |

上流とSDKのmerge-baseはv1.6だった。現行のversion headerも1.6だが、
SDKからMPXY新仕様、OP-TEE dispatcher、RPMI/K3固有driver等が入っている。
現行とSDK先端のtree差分は35 files、方向を「現行→SDK」として+62/-2,012 lines。
これにはKeystoneの保護処理・timer/HSM変更の除去が含まれる。

比較手順は、全commitの件名・本文・変更path、上流 `Fixes:`、SDKとのstable patch-id、
現行コードの該当条件を照合した。原patchのforward/reverse適用確認も補助的に実施した。
文脈が一致しなくても同じ欠陥が別pathに残る例があるため、適用失敗を「対象外」の根拠にはしない。
例：`3baca100`の修正先は上流headerだが、現行では同じ計算が `sbi_domain.c:is_region_subset()` にある。
CSVの適用確認欄は元patch単独の結果であり、compile/guest PASSではない。

再現用コマンド（上流repoと本repoを別checkoutに用意）：

```bash
# upstream checkout内
git rev-list --count v1.6..v1.9
git rev-list --count v1.9..ed70256dd22c09962d146e433834239825ef0616
git log --reverse --format='%H %s' v1.6..ed70256dd22c09962d146e433834239825ef0616
# SDK履歴の同一patchはSHAの一致ではなく git patch-id --stable でも照合する
```

tree差分は、本repoの `779fb85a:opensbi` treeと上流treeを同じobject検索範囲で
`git diff --shortstat` した値。metadata、docs、build、platform追加/削除を含む。
行数を変更の危険度やバックポート工数へ直接換算しない。

## 3. 機能面の違い

| 分野 | 現行K3/Keystone | 上流v1.9・masterで増えたもの | 採否 |
| --- | --- | --- | --- |
| SBI/MPXY | MPXY新仕様の一部とdomain別状態、K3固有RPMI、旧OPTEEDを既に持つ | mailbox属性/通知、追加service、channel owner/domain notifier等 | 既存との対応を確認し、共通修正を先行。service追加は要件別 |
| hart/domain基盤 | v1.6系APIと独自PMP管理。Keystone有効時domain切替は停止 | hart protection abstraction、PMP/ePMP backend分離、domain初期化順・notifier・固定region配列 | 一括更新しない。Keystone所有権adapterを設計する |
| PMP | K3独自slot割当・全hart同期・TOR readback・quarantine | 共通encoding/CSR access helper、FW slot保護、protection reconfigure/ID対応 | 共通計算は候補。上流のslot解除をそのまま入れない |
| FP/Vector context | KeystoneはGPR/CSR主体でFP/Vは未完了 | v1.9でdomain間のeager FP/V保存・復元 | 前回計画P2/P3を継続。SM独自switchへの接続が必要 |
| trap/ISA | K3用preflightと旧trap実装 | double-trap/CTR、ZaamoなしAMO、Smrnmi/RNMI、hart別expected trap | 機能追加と既存trapの修正を分離。異種hart対応を評価 |
| IRQ | 既存APLIC/IMSIC/PLIC、Keystone mtvec/mideleg | wired/MSI handler登録、line sensing、affinity/reservation、IMSICの新framework統合 | 必要性を決めて段階導入。RISE VIRQそのものとは別 |
| timer | 独自のLinux SstcとSM M_TIMER共存 | v1.9でhart別timer stateとevent list/callback | 既存共存を置換しない。導入時はevent listの後続修正を必須化 |
| PMU | PMU/SSE、K3固有アクセス制限 | 正常系・エラーコード・counter管理の改善、各種境界修正 | バグ修正を優先。Linux perfとSM境界を回帰試験 |
| firmware/build | 独自SM object/linker/mtvec整列検査 | stack protector、Zkr guard、UBSan、TSA、firmware header、build検出改善 | linker/boot引数の互換性を確認。Zkr guardはSM RNGとは別 |
| driver/platform | ベンダーK3、WTM/PMU/idle/suspend等の独自処理 | K1、K3、Andes/SiFive/MIPS等の追加、cache/I2C/PMIC driver | 上流K3対応があってもvendor K3全機能の代替とはみなさない |
| OP-TEE/Keystone | 旧OPTEED、Keystone EID/FID、HSM admission等を持つ | 上流FP/V等はあるが、今回のKeystone統合やRISE topic全体はない | 上流BINへの交換では現行機能を維持できない |

主な機能導入commitは、`809df05c`/`42139bb9`（protection abstraction/API変更）、
`21461156`/`718e1d19`/`22d7e99e`（FP/V）、`00fec20b`（RNMI）、
`b7fa8a24`（timer events）、`1854901a`/`f83d6a73`（firmware header）、
`cca12105`（上流K3 platform）。個別SHAとpathはCSVで追跡できる。

上流K3追加はベンダー実装と同じファイル名・構成ではない。
ベンダーのclock/電源管理、RPMI/WTM、保護領域、boot引数とKeystone linkを
失わないことを差分レビューの条件にする。

## 4. バグ修正：現行へ優先するもの

以下の「残存」はソース上の未修正条件を確認したという意味。
実機で悪用・破壊・漏出が再現したという意味ではない。
設定・ISA・DT・通信相手によって発火条件が異なる。

| 優先 | 修正・上流SHA | 現行の状態・必要な対応 | 台帳/試験 |
| --- | --- | --- | --- |
| P0 | MPXY byte丸め `2de7dfb6`、属性範囲overflow `2afa7775` | page数格納、u32 endpoint計算が残る。既存map/unmapとエラー処理を保持して移植 | KSB-045/046、BP01 |
| P0 | PMU index overflow `0dfe6be0`、class mismatch `9f37e25f` | `cbase + fls(mask)`のoverflow防止なし。SKIP_MATCHにも上限/class検査を追加 | KSB-047、BP02 |
| P0 | PMU event infoサイズoverflow `f45b9433` | 検査前に `num_events * sizeof(...)`。乗算前検査を移植 | KSB-048、BP02 |
| P0 | DBTR range overflow `a14e4d24`と終端修正 `ed70256d` | 加算比較と `>=`境界が残る。2件を組にし、最終要素の正当な要求も許可 | KSB-049、BP03 |
| P0 | RPMI queueサイズ `95c79808`、index `9bc84122` | header分の減算前容量検査なし、`queue_id == queue_count`を受理 | KSB-050/051、BP04 |
| P0 | ISA/SBI extension文字列境界 `d2166a9d`/`f95648d3` | 別関数の容量検査修正。ISA列はsbi_hart.c、SBI列はsbi_ecall.c。補助test部分は現行規約に合わせる | KSB-052、BP05/07 |
| P0 | PMU未割当counterのindex検査 `ac16c6b6` | FW counter探索失敗時、負のindexを検査する前に配列へ書ける。現行の配列所有関係に合わせて移植 | KSB-056、BP02 |
| P0 | HSM start context検査 `131bda94` | STOPPED確認前にnext_addr/arg1/modeを書いている。K3 admission/cancel処理を保持 | KSB-053、BP06 |
| P1 | domain subset shift `3baca100` | 現行 `is_region_subset()`に `BIT(order)` が残る。order=XLENを特別扱い | KSB-054、BP08 |
| P1 | MPRV cleanup `fbb5fe81`、hart CSR reset `009f77a9`、direct xtvec `6e0338f4`等 | expected trapとhstatus/stateenの初期化を見直す。CSR検出・64bit helperも揃える | BP07 |
| P1 | PMU start/stop・read・error codes | 返却値無視、FW counter状態、counter31符号shift等の一連の修正を採用審査 | BP02 |
| P1 | DBTR CSR読出し・tdata検査 | `6a1f53bc`、`3afe63d4`、`548518e6`、`27d95453`。未実装CSR/slot capabilityを扱う | BP03 |
| P1 | RPMI CPPC field order `33279821`、warm reboot `fde5dfe9` | server側との互換性確認が必須。構造体/番号だけ先に変えない | KSB-055、BP04 |
| P1 | FDT/regmap/serial | ranges変換、disabled/non-contiguous hart、FDT空間、APLIC MSI/lock、PLIC signed context、syscon stride等 | BP09・全件台帳 |
| P1/P2 | string/CSR macro/build/ABI | string copy未初期化部分、strchr(NUL)、swap式、FW payload alignment等 | BP05/07/12 |

P0は採用着手の優先順位。修正を急ぐために依存・guest検証を省略する意味ではない。
debug/PMUのAPIをSMからもどう制限するかは、既存のK3 policyを保持して確認する。

### 4.1 既に取り込み済み：重複させない5件

| 上流SHA | 修正 | K3 SDK側SHA / 現行確認 |
| --- | --- | --- |
| `30437eb2` | MPXY capability bit | `e522eba9`、同一patch＋逆適用確認 |
| `033e0e23` | DBTR shared memory layout | `5b966295`、union layoutが存在 |
| `32402142` | DBTR UPDATE ABI | `a778a435`、trig_count引数が存在 |
| `9595829a` | domain範囲overflow | `d8ff6346`。現行は `!size || max <= addr` でzero-sizeも拒否 |
| `69a0f024` | PMU zero mask | `8bd2cbdf`、zero mask拒否が存在 |

patch-id一致だけで「現行も有効」とせず、後続ローカル変更が取り消していないことも確認した。

### 4.2 新機能と必ず組にする修正

- FP helper導入時：`718e1d19`に加え `7774b82e`（FS enable）を同梱する。
- Vector misaligned emulator更新時：`a8be5e94`と `663786ed`を組にする。
  **後者のmask overflowは現行の旧実装では同じ形では存在しない**。
  現行はmask読出しに `vlenb` bytesを渡し、新実装のbit数誤用を持っていない。
- timer event導入時：`b7fa8a24`に `5930665a`（restart list）と
  `bcf587a9`（期限順挿入）を同梱する。現行SM timerをこれらの不具合と混同しない。
- IPI検査：`37eaca4a`だけではSTOPPED hartを含むRFENCEが回帰する。
  `110f2851`と組にしてdomain所属とinterruptible状態を分けて判定する。
- PMU reserved-bit検査 `d97cfb33`を入れる時は `0ce50ae8` のunmap cleanupも同時に入れる。
  error-code変更 `c5077d49`には `827f8bf2` のSTOP/RESET回収も含める。
- HSM wait変更 `d844dead`には `7dd09bfe`、hartid検査 `5e90e54a`には
  `9f64f061`が必要。先行patchだけを採用しない。
- RNMI、irqchip MSI、RPMI MM/logging等の新機能修正は、その機能を入れる時の条件付き対象。
  K1/MIPS/Andes/OpenPiton/ESWIN/JH7110等の19件はK3向けバックポートから除外する。

## 5. バックポート計画への追加

前回の[OP-TEE/Keystone計画](optee-opensbi-keystone-merge-plan.md)のP1を、
MPXY丸め1件から下記の共通修正群へ拡張する。FP/VのP2/P3やOP-TEE併用設計とは別branchで進める。
全件台帳の111件を実装候補、37件を機能導入時の必須審査対象として登録する。

| 工程 | 内容 | 完了条件 |
| --- | --- | --- |
| B0：基準固定 | 現行全artifact/config/DTB/hash、通常K3・Keystoneのsuite、robustness既知FAILを保存 | 新規回帰とKSB-042/043/044等の既知FAILを識別できる |
| B1：入力・容量検査 | BP01/02/03/04/05のP0。overflow/range/stringを小commitに分け、各後続修正を同梱 | 不正入力拒否と境界正常入力、mapping復元、既存suite成功 |
| B2：hart/保護契約 | BP06/07/08。HSM scratch、MPRV、CSR reset、domain計算を移植 | admissionとPMP所有権が維持され、失敗時の状態が一致 |
| B3：driver/ABI整合 | 残るPMU/DBTR/SSE、FDT/APLIC/regmap/serial、RPMI protocol互換性 | 対応driverの実guest試験。実serverが必要な項目は実機pendingを明示 |
| B4：buildと機能依存 | BP12、条件付き37件、FP/V/timer/IRQ/RNMI等 | 先行機能＋修正を一単位で試験し、機能未導入の対象はdeferredのまま保持 |
| B5：累積検証・取り込み | Sstc on/off、2/16 hart、2/256 MiB、provider各profile、stress | 実guestログ、差分レビュー、bug台帳・採用SHAを更新して取り込む |

`バックポート対象`は全部を無条件で入れるという意味ではない。
各patchの先行API・後続修正、実際の発火条件、K3側変更を確認して最終採否を記録する。
不採用にする場合も理由と代替防御を全件台帳に残す。
現行にない機能を直すためだけに大規模frameworkを導入しない。

### 保持するローカル契約

- OpenSBI domain gateを解除しない。通常PMP再構成でKeystone SM/EPMを消さない。
  `ea459237`の「残PMPをdisable」をK3へそのまま適用しない。
- HSM begin/cancel/finish、全hart PMP generation/ACK、quarantineを保持する。
- `sbi_hart_map_saddr()`失敗処理と対になるunmap/MPRV cleanupを保持する。
- `skip_regs_update`と正のKeystone status、mepc/a0/a1の更新回数を保持する。
- Linux SstcとSM M_TIMER共存、trap vector整列、SM link/boot引数を維持する。
- CPPC等のwire ABIはfirmware server側を確認する。上流準拠だけを理由に稼働互換性を仮定しない。

各採用commitに上流SHA、必要な依存SHA、変更path、競合解決理由、対応test IDを記録する。
一括merge・ディレクトリ置換は行わず、`opensbi/`配下へ意味を移植する。
失敗時は該当commit群をrevertし、一致したfirmware/Image/rootfsで再試験する。

## 6. テスト計画

実Linux guest→driver/検証module→実OpenSBI/SM→Eyrie/enclaveを使用する。
製品関数をhostへ抽出する試験や、mock SBI/PMP/CSRは追加しない。
firmware内部helperの確認が必要なら専用検証buildで実処理を呼ぶ。製品APIとして残さない。

| ID | 試験刺激 | 合格条件 |
| --- | --- | --- |
| T-MPXY | maxlen=4096/4097/8192、末尾属性/overflow/zero-count、不正shared address、init失敗 | 正しいbytes通知、範囲外拒否、driver未到達、正常転送維持。異常後もmap/unmap整合 |
| T-PMU | ULONG_MAX付近のbase/count、zero mask、reserved event bits、SKIP_MATCH HW/FW不一致、counter31、FW read失敗、反復perf start/stop/RESET | OOB/CSR trap/未初期化返却なし。エラー・回収契約が一致し、Linux perfが継続 |
| T-DBTR | total_trigs=0/1/N、最終要素、exact-end、ULONG_MAX、未実装tdata CSR、不正slot/type | 正常境界を許可、overflowを拒否、BAD_RANGE/INVALID_PARAM/NOT_SUPPORTEDが仕様通り |
| T-RPMI | 小さなqueue DT、queue_id=count、最小有効queue、warm reboot、CPPC probe/read/write | 起動時拒否または正常動作。OOB/div0なし。CPPCはserver側wire fieldも照合 |
| T-CORE | extension列が容量を越える実firmware構成、string終端/短いcopy、scratch上限 | 実M-modeメモリーのguard破壊なし、NUL/返却値が正しい、正常起動維持 |
| T-HSM | STARTED/START_PENDING/STOPPEDへのstart、並行start、失敗後再試行、停止hartを含むRFENCE、warm boot | 拒否時のscratch非破壊、admission取消し、deadlock/漏れなし。許可したstopだけを使う |
| T-CSR | H/Smstateen有無、CSR probe失敗、MPRV access fault、異種hart expected handler | 未実装CSRを無条件操作せず、MPRV/mtvec復元、低privilege状態の初期化が正しい |
| T-PMP | order=XLEN、包含/被覆region、PMP slot不足、失敗rollback、Sstc on/off | shift UB回避、SM/EPM保護保持、全hart整合。保護不明の資源を再利用しない |
| T-FDT | disabled/non-contiguous hart、階層ranges、tight FDT、APLIC lock、syscon stride、UART | FDT修正が成功し、DTの範囲が正しい。Linux起動・IRQ・resetが正常 |
| T-SSE | 不正event/状態/属性、Hの有無、割込み注入 | 規定のerror、正しいSPV/status。追加機能の要否を区別 |
| T-EMU | scalar/FP/Vのmisaligned load/store、mask、複数VLEN、VS-mode | 正常命令結果と正しいtrap redirect、M-mode stack破壊なし。新Vector実装＋後続fixを組で検証 |
| T-BUILD | Clang/GCC、通常K3/Keystone、verbose/silent、payload/link/mtvec | ビルドとELF確認。機能無効構成の不要CSR/命令なし。buildだけでguest PASSにしない |

基準と累積検証は既存 `verifications/run-all.sh` と専用robustnessを実guestで実行する。
Sstc on/off × 2/16 hart、2/256 MiB EPM、4000 backing有/無、unavailable/qemu-devを確認する。
HSMやPMPの危険条件は専用guestを分け、既存のfail-stopを無効化して試験を通さない。
K3実機固有のRPMI/CPPC/suspend/IRQはQEMUで代替できない項目として残す。

ホストのbuild手順とguest内の実行手順は[既存検証方法](keystone-verification-methods.md)に従う。
採用前後のSHA、全artifact hash、config/DTB、toolchain、実QEMUコマンド、uname/ISA、
test ID、期待/実測、終了statusを記録し、機能無効・未実施・既知FAILをPASSへ換算しない。
新規固有caseは今後実装するもので、現行run-allが全候補をカバーするとは主張しない。

## 7. 今回の確認結果と資料

- 完了：上流/SDK履歴取得、501件の分類、tree差分、同一patch照合、優先箇所のコード確認、文書の参照・整合性確認。
- 未実施：候補patchの実装、build、実QEMU、実機試験。原patchの適用確認は読み取り専用の `--check` のみ。
- bug登録は[Keystone bug台帳](keystone-bug-report.md)のKSB-045以降を参照。
  上流不具合とKeystone固有の不具合・互換性制約を区別する。

一次資料：

- [最新リリースv1.9](https://github.com/riscv-software-src/opensbi/releases/tag/v1.9)、[v1.7](https://github.com/riscv-software-src/opensbi/releases/tag/v1.7)、[v1.8](https://github.com/riscv-software-src/opensbi/releases/tag/v1.8)
- [固定master tree](https://github.com/riscv-software-src/opensbi/tree/ed70256dd22c09962d146e433834239825ef0616)
- [固定SDK tree](https://github.com/spacemit-com/opensbi/tree/ad6091fd16c4d4a445168f6a479e1b8aadb4931c)
- 個別修正の一次資料は[全件台帳](opensbi-upstream-backport-catalog.md)のcommitリンクを参照。
