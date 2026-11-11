# RISC-V OP-TEEのOpenSBI変更とK3 Keystoneへの取り込み計画

調査日：2026-11-11（JST）。本書はソース調査と実装・試験の計画である。
今回の変更は文書のみで、OpenSBI/SMの機能マージ、ビルド、実QEMU試験、実機試験は実施していない。

## 1. 結論

**RISC-V OP-TEEのためのOpenSBI変更はある。本リポジトリにもその一部が既に存在する。
Keystoneへは機能単位のバックポートと接続変更が可能だが、OP-TEEブランチの一括マージは推奨しない。**

- SpacemiT K3 SDKのMPXY OP-TEE dispatcher、domainごとのMPXY状態、domain切替APIは既に取り込み済み。
  dispatcherをもう一度追加する必要はない。
- 優先候補は、既存MPXYのサイズ丸め修正、次にOpenSBI上流のFP/Vector状態保存・復元部品。
  FP/VectorはKeystone独自のcontext切替へ接続し直す必要がある。
- RISEのVIRQ/SYSTEM_IRQ/Yield、HWISO/WorldGuard、OP-TEE用RPMI TEE/REQFWDは
  別の目的を持つ。Keystoneの通常enclave実行の必須条件ではなく、必要性と依存を分けて判断する。
- **現在のKeystoneとOP-TEEの同時稼働は不可と判定する。**
  `CONFIG_K3_KEYSTONE_SM`ではOpenSBI domain切替をPMP所有権のため明示停止する。
  この制限の削除は単純な競合解消ではなく、隔離方式の再設計になる。

ここでいう「取り込み可能」は実装方針の判定であり、パッチ適用成功や動作確認済みを意味しない。
OP-TEE本体を書き換える話と、OP-TEEを支えるOpenSBIの共通機能を再利用する話を区別する。

## 2. 調査対象と再現点

更新されるブランチ名に加え、取得したcommitを固定する。

| 対象 | 調査時のref / commit | 用途 |
| --- | --- | --- |
| 本リポジトリ | `main` / `8ce25d283e19c02cdf2e08727460473b8408882a` | 今回のKeystone移植 |
| SpacemiT OpenSBI | `k3-br-v1.0.y` / `ad6091fd16c4d4a445168f6a479e1b8aadb4931c` | K3 SDK版OP-TEE対応の比較元 |
| OpenSBI上流 | `master` / `ed70256dd22c09962d146e433834239825ef0616` | マージ済み共通機能・修正の確認 |
| RISE OpenSBI | `dev-optee-mpxy-v9` / `a1cdb6f4c7f511e3d0d41fc89495aefbd559e0b7` | 新しいTEE/REQFWD dispatcher系列 |
| RISE OpenSBI | `rp016_m4` / `cfd13a06b1bba51a1e120c89e389b48a7daaa092` | HWISO |
| RISE OpenSBI | `rp016_m5` / `f2b8264d71d98fbf64f4bc423de07bab5ee34739` | WorldGuard |
| RISE OpenSBI | `rp016_m6_v2` / `d881e53c0c1dbe8a6a52fe0f3db804477b6c81a7` | 初期FP/Vector系列との対応 |
| RISE OpenSBI | `rp022-m1` / `3762b42346d0804721766c1c2a1a3bac2bc788c9` | SYSTEM_IRQ / Yield / VIRQ |
| RISE OpenSBI | `rp022-m8-pmp-data-perms` / `e7eb5436b70de3693147200a41da56830b0bbc2c` | OP-TEE core領域のDT記述 |

本リポジトリの `opensbi/include/sbi/sbi_version.h` は1.6。
ただしベンダー変更とKeystone変更を含むため「素の上流v1.6」とは扱わない。
Git履歴に別リポジトリのOpenSBI一式を持つ構成なので、RISEブランチを本リポジトリの
rootへ直接 `git merge` する方法は適さない。`opensbi/` 配下への選択的バックポートを基本とする。

## 3. OP-TEE向けには何が変更されているか

### 3.1 K3 SDK系列：MPXY経由のOP-TEE dispatcher（既存）

[SDK固定ソース](https://github.com/spacemit-com/opensbi/tree/ad6091fd16c4d4a445168f6a479e1b8aadb4931c)と
本リポジトリを比較した。以下のdispatcherは `cmp` で内容一致を確認した。

- [fdt_mpxy_opteed.c](../opensbi/lib/utils/mpxy/fdt_mpxy_opteed.c)：
  `riscv,sbi-mpxy-opteed` をDTから検出。TEEメッセージCOMMUNICATE/COMPLETEを扱い、
  trusted/untrusted domain間の共有メモリー転送と実行切替を行う。
- [sbi_domain_mpxy_state.c](../opensbi/lib/sbi/sbi_domain_mpxy_state.c)：domain/hartごとのMPXY状態。
- [sbi_domain_context.c](../opensbi/lib/sbi/sbi_domain_context.c)：GPR、S-mode CSR、trap frameとdomain割当の切替。
- [k3_defconfig](../opensbi/platform/generic/configs/k3_defconfig)と
  [Keystone構成](../opensbi/platform/generic/configs/k3_keystone_audit_defconfig)には
  `CONFIG_FDT_MPXY_OPTEED=y` がある。

configが有効でも、DTの対応node、domain、payload、Linux側の対応がなければ
OP-TEEサービスの成立は保証されない。既存のK3導入手順は
[Jupiter2のOP-TEE資料](optee-milk-v-jupiter2.md)を参照する。

### 3.2 RISEの新dispatcher系列：RPMI TEE / Request Forward

[固定系列](https://gitlab.com/riseproject/riscv-optee/opensbi/-/tree/a1cdb6f4c7f511e3d0d41fc89495aefbd559e0b7)では、
TEE service、REQFWD、汎用dispatcher層、OP-TEE固有層へ分離する。
代表commitは `065e429`（RPMI TEE）、`47efc0f`（REQFWD）、
`794ed9d`（OP-TEE dispatcher）、`1ef1a0c`（domain handling）。
`lib/utils/tee/optee_dispatcher.c`、`tee_dispatcher.c`、
`lib/utils/mpxy/fdt_mpxy_rpmi_tee.c`、`fdt_mpxy_rpmi_reqfwd.c`と
`docs/optee_dispatcher_support.md`を確認した。

これはK3 SDKの旧 `fdt_mpxy_opteed.c` を追加するだけの変更とは異なる。
Linux/U-Boot/OP-TEEとDTの通信契約も更新する必要がある。
本リポジトリのLinux/SDKをそのまま使えるとは判定していない。

### 3.3 RISE RP016：割込み、隔離、FP/Vector

| 機能 | 実際の変更と今回の意味 |
| --- | --- |
| M2 wired IRQ/APLIC | IRQのclaim/complete/mask/unmaskを抽象化し、M-modeでAPLICの外部割込みを処理。K3の既存APLICドライバーがあるだけで同機能とはみなさない。 |
| M3 VIRQ | domainごとの割込みルーティング・通知・courierによるdomain切替。KeystoneのM_TIMERによる中断とは別経路。 |
| M4 HWISO | domainのinit/enter/exit/cleanupに、PMP以外の隔離機構を接続する枠組み。 |
| M5 WorldGuard | WorldGuard checkerのDT/MMIO設定と、domain切替時のWID状態設定。K3に同じハードウェアがある根拠にはならない。 |
| M6 FP/Vector | domain切替時にFPレジスター/FCSRとVectorレジスター等をeager保存・復元。上流に共通部品が存在し、Keystoneへの再利用候補になる。 |

M6について上流のcommitを確認した：

- [21461156](https://github.com/riscv-software-src/opensbi/commit/21461156da2be4633f53a60a77fe70da1a517db4)：Vector保存・復元。
- [718e1d19](https://github.com/riscv-software-src/opensbi/commit/718e1d194f5a41c657dfe094392abdeb02356680)：FP保存・復元。
- [22d7e99e](https://github.com/riscv-software-src/opensbi/commit/22d7e99ed2f9c66012a05a82318785d80dd451a0)：domain切替への接続。
- [7774b82e](https://github.com/riscv-software-src/opensbi/commit/7774b82e4aaa6fa98d1430b02d614eadd339a8a2)：後続FP state enable修正。

古いRP016 topic branchだけを採用せず、後続修正を含む上流を採用元とする。
ただしdomain切替への接続commitをそのまま入れても、Keystoneの切替には適用されない。
また調査したVector context構造は `vcsr`、`vstart`、`vregs[]` を持つが、
`vl`、`vtype`用のfieldはない。Keystoneで要求する全状態の仕様を先に定義し、
これらを含む追加保存の要否を確認する。上流コードがあることを完全隔離の証明にしない。

### 3.4 RISE RP022：SYSTEM_IRQ / Yieldとcore領域権限

[rp022-m1固定ソース](https://gitlab.com/riseproject/riscv-optee/opensbi/-/tree/3762b42346d0804721766c1c2a1a3bac2bc788c9)の
`2058dc9`はRPMI SYSTEM_IRQ、`e35ade1`はYield ECALL、`d3af875`はVIRQ本体。
`f797082`はcourier用domain context切替を追加する。
YieldはEID `0x0800594c` / FID `0`で、SYSIRQ batchを完了する。
KeystoneのSTOP FID 3004への単純な置換ではなく、Keystone EID `0x08424b45`とも別物。

プロジェクトの[公開状況](https://riscv-optee-docs-c9fea0.gitlab.io/rp022/project_status/)では
この系列は提出済みとされる。今回確認した上流checkoutには `sbi_virq.c` はなく、
上流マージ済みの機能としては扱わない。

RP022 M8の固定branchの先端 `e7eb543` は、OP-TEE coreのtext/rodata/dataを分けるDT説明の変更。
OP-TEE本体・DT・ビルド構成との組合せであり、Keystoneの動的EPM権限を自動的に改善するパッチではない。

## 4. Keystone側との競合と採否

| 対象 | 判定 | 理由・保持する契約 |
| --- | --- | --- |
| 既存K3 OPTEED | 再取り込み不要 | dispatcher本体はSDKと一致。Keystone profileでの利用はdomain制限に従う。 |
| MPXYサイズ丸め修正 | 優先バックポート候補 | 現在のコードに同じ誤計算を確認。KSB-045を参照。 |
| FP/Vector低層helper | 条件付き採用 | ISA/toolchain依存と後続修正を揃え、SM独自switchへ接続する。 |
| OpenSBI domain FP/Vector接続 | そのまま採用しない | KeystoneのRUN/RESUME/STOP/EXITはdomain switchを通らない。 |
| VIRQ/SYSTEM_IRQ/Yield | 将来の別工程 | 外部IRQをenclave/別domainへ配送する要件がある場合に実施。現行timerの代替にはしない。 |
| HWISO枠組み | 将来の設計候補 | PMP所有権を統一するadapterと失敗時契約が必要。 |
| WorldGuard backend | 現時点では不採用 | K3のWorldGuard checker/WID対応を確認できていない。IOPMPと同一視しない。 |
| 新TEE/REQFWD dispatcher | OP-TEE更新・併用の別工程 | 通信ABI、domain、DT、全コンポーネントの整合確認が必要。 |
| OP-TEE coreのRX/R/RW分割 | 設計の参考 | Keystoneの測定・EPM layout・TOR slot消費とruntime書込み条件を別途設計する。 |

### 4.1 PMP/domain切替：最も大きい障害

本リポジトリの `switch_to_next_domain_context()` はKeystone有効時に
`sbi_hart_hang()` し、通常のPMP解除・再設定も除外する。
目的はKeystoneのSM/EPM保護をOpenSBI側のPMP再構成で消さないことである。
RISEのdomain切替処理でこの関数を上書きすると制限が失われ得る。

併用するなら、PMP全slotの所有者、静的OP-TEE領域と動的EPMの非重複、
全hart適用、HSM admission、TLB同期、部分失敗時の隔離と回収を共通管理する。
domain割当やcontextを公開する前に保護を確定し、復旧不能時はS-modeへ戻さない。
既存の停止制限は、その管理方式を実ゲストで検証するまで維持する。

### 4.2 Keystoneの独自切替とFP/Vector

[enclave.c](../keystone/sm/src/enclave.c)の `context_switch_to_enclave/host()` と
[thread.c](../keystone/sm/src/thread.c)、[thread.h](../keystone/sm/src/thread.h)が主経路。
GPR/CSRの交換はあるが、K3 platformのswitch hookは空で、thread構造にFP/Vector保存領域はない。
これは既存[移植計画](keystone-porting-plan.md)にも未完了項目として記録されている。
実ゲストでの漏出・破壊を今回再現したとは主張しない。

追加する場合は、hostは実行hartごと、enclaveはEID/threadごとに状態を所有する。
別hart RESUME時に移動元のhost状態を復元してはならない。
S-modeはFS/VSを変更できるため、信頼しない相手のDirty表示だけで保存を省略しない。
初回enclaveには別contextの値を見せず、DESTROYと失敗cleanupで保存領域も消去する。

### 4.3 既存ABI・timer・未解決障害

- [sm-sbi-opensbi.c](../keystone/sm/src/sm-sbi-opensbi.c)の
  `skip_regs_update`、正のKeystone status、mepc/a0/a1の一度だけの更新を保持する。
- [Sstc共存](keystone-sstc-coexistence.md)に従い、Linuxのstimecmp/STCE保存・復元と
  enclave用M_TIMERを保持する。古いSstc全体抑止へ戻さない。
- [KSB-042/043/044](keystone-bug-report.md)は未解決の既知障害。
  特にPMP/domain/IRQ経路を拡張する前に042の終了状態、043の競合時回収、
  044の失敗cleanup契約を解消し、実ゲストで確認する。
  既知FAILを新パッチのPASSへ読み替えない。

## 5. マージ計画

2026-11-11追補：OpenSBI全体の比較に基づき、P1の共通修正候補を拡張した。
[現行版と上流最新版の差分・バックポート計画](opensbi-upstream-delta-2026-11-11.md)の
B0〜B5および[全件台帳](opensbi-upstream-backport-catalog.md)を本計画の一部とする。
111 commitを現行向けバックポート対象、37 commitを先行機能/ISA条件付き対象として登録し、
既取込5件とK3対象外19件は重複適用しない。件数は不具合の実証数ではなく修正commitの分類数。
MPXY丸めだけでなく、入力範囲、PMU/DBTR/RPMI、HSM/MPRV/CSR等を段階的に確認する。
FP/V・domain併用の設計前提とPMP gateは維持する。

今回の成果物は本計画。以下の実装は今後の作業とする。
各段階は独立commit/レビュー単位とし、前段階の合格を次の開始条件にする。

| 段階 | 内容・変更先 | 完了条件 |
| --- | --- | --- |
| P0：固定・基準採取 | component SHA、toolchain、config、DTB、ELF/BIN/Image/rootfsのhashを保存。既存suiteとrobustnessのFAILを採取し、042/043/044を先行修正。 | 基準ログと修正後再試験があり、未解決FAILの対象が明示される。 |
| P1：小さな共通修正 | `2de7dfb6813facba8e376897290bedb18ef38401`相当のMPXY byte丸めを `opensbi/lib/sbi/sbi_mpxy.c` へバックポート。既存のMPRV/map/unmap変更を保持。KSB-045を更新。 | T-MPXYと既存Keystone suite成功。通常/Keystone configの両方を確認。 |
| P2：FP対応 | 上流helperと後続修正、header、objects.mk、MakefileのISA/toolchain検出依存を抽出。SMにhost-hart/enclave-thread保存領域と全switchの接続を追加。 | T-FP、異常系、既存suite成功。実行時F/D未対応で命令を実行しない。 |
| P3：Vector対応 | P2と分離。VLENB検出、容量/整列/上限、全Vector CSR、異VLEN hartの移動方針、初期化/消去を実装。 | T-V成功。未対応ISA/異VLENは安全に拒否し、偽の対応表明をしない。 |
| P4：外部IRQ（要件確定後） | SYSTEM_IRQ→Yield→VIRQ→irqchip/FDT routingの依存を列挙。必要ならAPLIC wired IRQ層を先に導入。 | T-IRQ。Keystone内のmtvec/midelegと整合し、domain gateを回避しない。 |
| P5：OP-TEE同時稼働（別設計） | HWISO/PMP所有者を一本化し、新TEE/REQFWDを関連Linux/U-Boot/OP-TEE/DTと揃えて導入。 | T-COEX、実機隔離試験、起動・復旧手順を完了するまで無効。 |

採用パッチごとに上流SHA、依存SHA、ローカル対応ファイル、競合解決理由を記録する。
`[TEST]`、`[TEMP]`、QEMU virt用DT、semihosting無効化等を製品変更へ一括で持ち込まない。
BSD等のライセンス表示と由来を保持し、新規C変更はroot AGENTS.mdのK&R規約に従う。

### 5.1 パッチ作成手順

1. P0で固定した本リポジトリHEADから作業branchを作る。
2. 採用元の各patchを `git format-patch` で抽出し、`opensbi/`配下へ適用する。
   必要なら `git apply --directory=opensbi --check` で機械的適用可否を確認する。
   SM側への接続変更は別commitにし、採用元のdomain実装と混同しない。
3. 依存のない既存K3ファイルを丸ごと置換しない。競合ではPMP/domain gate、
   HSM admission、MPRV mapping、timer、trap復帰契約を逐一比較する。
4. buildと実ゲスト試験を実施し、該当段階の結果表とbug台帳を更新する。
5. 合格した段階のみ取り込む。FAIL時は該当段階のcommitをrevertし、固定済みの
   firmwareと一致するImage/rootfsを使って再確認する。保護不明の資源はquarantineを維持する。

## 6. テスト計画

**実Linux guest → driver → 実OpenSBI/SM → Eyrie/enclaveで確認する。**
模擬SBI/PMP/CSR、製品関数をhostへ抽出する試験は使わない。
RISEのSBIUNITやQEMU virt/bare-metal試験は採用元の補助証拠であり、K3統合PASSに代用しない。

### 6.1 構成行列

| 軸 | 必須構成 |
| --- | --- |
| firmware | 修正前基準、各段階単独、累積。通常K3とKeystone config。 |
| timer | `--sstc on` と `--sstc off`。enclave timer取消し後もSM中断を確認。 |
| hart | 1、2、既定16。並行試験は2→16。migrationは対応を明示した同VLEN hart間から。 |
| EPM | free-memory 2 MiB / 256 MiB、TOR、4 GiB超RAM、FID 4000 backing有/無。 |
| ISA | F/D/Vの有無を分ける。QEMUの指定が実際に反映されたことをISA情報・実命令で確認。 |
| Vector | QEMUで表現できる複数VLEN、VL/SEW/LMUL、vstart/vcsr、異VLEN移動の拒否条件。 |
| provider | unavailableを基準、明示的qemu-devも検証。開発鍵は実機鍵の証明に使わない。 |

full suiteは少なくともSstc on/off × 2/16 hartで実行する。
FP/Vector固有試験は対応ISA構成と未対応構成の双方で実施する。

### 6.2 試験項目と合格条件

| ID | 試験・刺激 | 合格条件 |
| --- | --- | --- |
| T-BASE | 既存 `run-all.sh`：正常/不正SBI、2/256 MiB、edgecall、RNG、crypto、3004、4000 | 全suiteの終了statusと各caseを記録。直接EPM probeも実行し、UTM/通常RAMを正の対照とする。 |
| T-ROB | `robustness` のEXIT後RESUME、同/異FID並行、CREATE/DESTROY失敗 | 042/043/044の修正を実証。資源台帳・所有権・再利用/隔離が整合し、FAILをskipで隠さない。 |
| T-MPXY | 実firmwareに検証用channelを登録し、maxlen=4096/4097/8192 bytes、重複ID、shmem設定・転送を実ECALL | 4097→8192 bytes、8192→8192 bytes。advertised size、境界、過大要求拒否が整合し、通常channelにも回帰なし。検証channelは専用buildのみ。 |
| T-FP | Linux側の検証moduleでkernel FPU規約を守り、eappと異なるf0〜f31/FCSRのpatternを設定。RUN/STOP/RESUME/EXIT、timer/IPI中断、初回/再作成 | 全レジスターと丸め/例外flagが各所有者の期待値に一致。別enclave/hostの残値が見えない。Linux user processだけの比較でkernelが保護した結果をSM保存成功としない。 |
| T-V | Vector全レジスターとvl/vtype/vstart/vcsrを検証。複数SEW/LMUL、timer/OCALL、対応hart間migration | 全状態と計算結果が一致。allocation/容量検出失敗は進入前拒否。未対応ISA/異VLENで不正命令や破壊を起こさない。 |
| T-FAIL | FP/Vector領域確保、PMP適用、全hart IPI ACK、timer準備の実経路にone-shot故障を注入 | host frame/PC/status/timerの破壊なし。初期化途中の状態で進入しない。復旧不能ならfail-stop、未保護ページを解放しない。 |
| T-ABI | 全FID、無効/未定義FID、正のstatus、switch成功/失敗、a0/a1/mepc | PC加算は一度だけ。2004未定義、2006拡張、3004/3006、4000の既存契約を維持。 |
| T-TIMER | Linux sleep/wakeup、enclave TIME要求・取消し、OCALL中期限切れ、割込み負荷 | Sstc on/offともLinuxとenclaveが進行。stimecmp/STCE復元、SM中断、RESUMEに欠落/無限割込みなし。 |
| T-IRQ | P4導入時に実irqchipから物理IRQ注入、claim/complete/yield、不正domain/route、連続IRQ | 二重完了/取りこぼし/誤domain配送なし。Linuxの既存IRQとSM timerが進行し、domain gateを踏むなら未対応として止める。 |
| T-COEX | P5専用：OP-TEE xtest/Hello WorldとKeystone suiteを同時実行し、双方から相手のprivate領域へ実アクセス | 正しいアクセス例外で拒否、PMP所有権維持、TA/enclave双方の再実行が成功。CPU隔離とDMA隔離は別々に確認。 |
| T-SOAK | 決定的case完了後に並行workerとtimer/IRQ/メモリー圧迫を併用 | 初期目標各Sstc構成で1万lifecycleかつ1時間。異常終了、漏出、資源増加、quarantine理由を記録。 |
| T-PERF | 修正前/後でswitch時間、OCALL往復、timer中断、SM heap消費を同一条件で採取 | median/p95/maxとVLEN/件数を報告。性能基準はP0で合意し、数値を測らずPASSにしない。 |

FP/Vector試験のpattern設定・読出しはcompilerの通常ABI保存に任せず、専用asmで確認する。
ただしCSR/レジスターの実処理は置き換えない。fault injectionの詳細は
[異常系・競合試験計画](keystone-abnormal-concurrency-verification-plan.md)へ合わせる。

### 6.3 既存のbuild・guest実行手順

以下は将来のマージ検証用。今回の調査で実行済みとはしない。
依存・完全な対応kernel出力・SDKが準備されたホストで、本リポジトリrootから実行する。

```bash
make opensbi
bash scripts/build-install-keystone.sh --only sdk
make -C verifications guest
make -C verifications kernel-modules KERNEL_OUT="$PWD/.build-k3/linux"
```

配置は[build/install手順](make-build-install.md)に従い、停止済みguestの検証用prefixへ行う。
起動ごとに実際にロードしたfirmware/Image/rootfsを確認する。

```bash
bash scripts/run-k3.sh --prefix "$PWD/install/keystone" --image keystone --smp 2 --sstc on
# guest停止後、別runとして --sstc off、--smp 16 を検証する
```

実QEMU guestのrootシェルで：

```bash
mount-hostshare
cd /mnt/host/verifications
bash run-all.sh
```

`--data-only`は隔離の最終合格に使わない。robustnessは
[専用README](../verifications/robustness/README.md)の手順でも別途実行する。
FP/Vector/MPXY専用caseは今後追加するもので、既存run-allに含まれるとは主張しない。

### 6.4 保存する証拠と最終判定

各runに日時、commit、採用patch SHA、config/DTB、toolchain、全artifact hash、
実QEMUコマンド、guest uname、ISA/VLEN、test ID、期待値、実測値、exit statusを記録する。
失敗ログも保存し、前後比較できる同条件の基準runを残す。秘密鍵は記録しない。

段階ごとにbuild結果、実ゲスト結果、実機結果を別列で管理する。
今回の全段階は **未実施**。リンク・ソース比較の確認を統合試験PASSとは表記しない。
K3実機ではPMP実装、Sstc、hart topology、DMA/IOPMP、IRQ controller、鍵provider、
boot imageと復旧経路を再確認する。WorldGuard対応QEMUの成功をK3のDMA保護成功に置き換えない。

## 7. 参考資料

- [RISE OpenSBI Git repository](https://gitlab.com/riseproject/riscv-optee/opensbi)
- [RP016 build](https://riscv-optee-docs-c9fea0.gitlab.io/rp016/build/)
- [RP016 M2](https://riscv-optee-docs-c9fea0.gitlab.io/rp016/milestone2/)、[M3](https://riscv-optee-docs-c9fea0.gitlab.io/rp016/milestone3/)、[M4](https://riscv-optee-docs-c9fea0.gitlab.io/rp016/milestone4/)、[M5](https://riscv-optee-docs-c9fea0.gitlab.io/rp016/milestone5/)、[M6](https://riscv-optee-docs-c9fea0.gitlab.io/rp016/milestone6/)
- [RP022 M1](https://riscv-optee-docs-c9fea0.gitlab.io/rp022/milestone1/)
- [上流MPXY丸め修正](https://github.com/riscv-software-src/opensbi/commit/2de7dfb6813facba8e376897290bedb18ef38401)
- [本リポジトリの移植計画](keystone-porting-plan.md)、[累積変更](keystone-cumulative-changes.md)、[bug台帳](keystone-bug-report.md)
