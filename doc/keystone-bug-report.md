# Keystone関連不具合報告書・継続管理台帳

登録・更新日：2026-11-09（日本時間）  
初版対象：本リポジトリ `c6aa79100` までに確認・記録し、その後追記したSDK、Eyrie、SM、driverと移植接続部分。
過去の発見日を新しく推測せず、下記IDの登録日は本書の作成日とする。

## 取り扱いと状態

本書の「Keystone関連」は対象システムの分類であり、すべてを上流Keystoneの責任と
断定する意味ではない。元ソース、ビルド環境、K3/OpenSBI移植、周辺ツールを区別する。
上流最新版との照合がない項目を「上流で現在も再現する」と報告しない。
これはリポジトリ内の報告書であり、上流issue trackerへ提出済みではない。

- **修正済**：修正コードがこのリポジトリにある。全条件の実機検証完了とは限らない。
- **一部修正**：対象の一部で修正済みだが、旧経路等に残る。
- **未修正**：ソースで問題を確認したが、修正は未実装。
- **要確認**：契約・制約上の課題。単独で確定したバグとは扱わない。
- 確認方法は「実ゲスト観測」「生成機械語」「ソース確認」「ユーザー報告」を区別する。

ユーザーから既存verificationsの全PASS報告を受領済み。その全PASSは新実装前の報告であり、
新しい3002/3003の成功経路や異常系が試験済みという意味ではない。
新実装は通常profileで未対応、明示的qemu-dev profileで有効となる。PMP失敗注入、競合、実機の全条件も
全PASS報告から推定しない。過去のホスト模擬試験は削除済みで、現行試験の証拠には使わない。

## 一覧

| ID | 問題 | 主な対象・由来 | 状態 |
| --- | --- | --- | --- |
| KSB-001 | memset/strlenの自己再帰化 | SDK／ツールチェーン依存 | 修正済 |
| KSB-002 | 1352-byte reportを2048 bytesコピー、失敗時もコピー | Eyrie取込コード | 修正済 |
| KSB-003 | attestation入力の事前長検査・コピー結果検査不足 | Eyrie取込コード | 修正済 |
| KSB-004 | sealing出力サイズ・コピー結果の検査不足 | Eyrie取込コード | 修正済 |
| KSB-005 | sealing引数のSM検査・可変長作業領域 | SM取込コード | 一部修正 |
| KSB-006 | report検証前の入力長・data_len検査不足 | SDK verifier取込コード | 修正済 |
| KSB-007 | random statusを未検査、getrandom成功数・flags等 | Eyrie取込コード | 修正済 |
| KSB-008 | freestandingコードのstack protector依存 | build／移植環境依存 | 修正済 |
| KSB-009 | raw loaderのPIE/GOT・再配置依存 | loader／移植環境依存 | 修正済 |
| KSB-010 | Eyrie固定入口にbuild-id note | Eyrie／既知上流にも同じ前提 | 修正済 |
| KSB-011 | CMA開始PAの2 MiB整列を仮定 | Eyrie／配置制約 | 修正済 |
| KSB-012 | 明示DESTROY後の二重DESTROY | SDK取込コード | 修正済 |
| KSB-013 | MAX_PAGE_ORDERの最大値を除外 | driver／Linux版互換 | 修正済 |
| KSB-014 | 4 GiB超RAMに32 bit DMA制約 | driver／K3移植制約 | 修正済 |
| KSB-015 | FINALIZE前EID=-1のunsigned判定 | driver取込コード | 修正済 |
| KSB-016 | 64 bit EIDの切り詰め | SM取込・移植コード | 修正済 |
| KSB-017 | DESTROYの所有・PMP失敗時の解放 | SM取込・移植コード | 修正済 |
| KSB-018 | RUN/RESUMEでPMP失敗後も切替 | SM/OpenSBI移植 | 修正済 |
| KSB-019 | trap frame/status/PCの二重更新 | SM/OpenSBI移植 | 修正済 |
| KSB-020 | TOR PMPを読み戻し検査で拒否 | K3 PMP移植 | 修正済 |
| KSB-021 | HSM停止禁止とshutdownの不整合 | K3/OpenSBI統合 | 修正済 |
| KSB-022 | config変更でcarrayドライバ表が更新されない | OpenSBI build統合 | 修正済 |
| KSB-023 | report未使用data末尾の未初期化 | SM取込コード | 修正済 |
| KSB-024 | HKDF最大出力で8 bit loop counterがwrap | SM暗号ライブラリ | 修正済 |
| KSB-025 | hosted verifierをnative eappへリンクしてcanary参照が未解決 | 今回のverification統合 | 修正済 |
| KSB-026 | native入口でgp未初期化のまま緩和済み命令を実行 | SDK/native build統合 | 修正済 |
| KSB-027 | STOPの要求検査前の状態変更・未使用a1の不定値 | SM取込／OpenSBI移植契約 | 修正済 |
| KSB-028 | QEMUでM_TIMER方針のplatform hookが未適用 | OpenSBI移植 | 修正済 |
| KSB-029 | 共有上のmakeselfパッケージで断続的MD5不一致 | 試験環境・原因未確定 | 要確認 |
| KSB-030 | trap_vector_enclaveが4-byte未整列でmtvecへ設定されない | SM/OpenSBIリンク移植 | 修正済 |

「取込コード」は本リポジトリで修正前のコードを確認できるという意味。
KSB-010以外の上流所在・上流最新の修正状態は、この台帳で新たに照合したものではない。

## KSB-001: SDK memset/strlenが自身を呼ぶ機械語になる

- **対象**：[SDK app CMake](../keystone/sdk/src/app/CMakeLists.txt)、[string.c](../keystone/sdk/src/app/string.c)。
- **症状・条件**：最適化buildの乱数eappがOCALL前に終了。store page faultの
  障害VAは `0x3ffdfff0`、128 KiBユーザースタックの下端 `0x3ffe0000` より16 bytes低い。
- **原因・影響**：自前のbyte loopをGCCがmemset/strlen呼び出しへ変換。
  特に4098 bytesのstorage初期化はmemsetのbyte loop側へ入り、同じ引数の再帰でスタックを消費する。
- **根拠**：実SDKの修正前string.c.oとリンク済みeappに自己呼び出しを確認。
  ユーザーの厳密なPC→命令対応にはそのELFが必要だが、ログのスタック下端超過と整合する。
- **修正**：`2c99fb066`。eapp archiveにfreestanding/no-builtin、GCCのstring.cへ
  loop pattern変換禁止を設定。[機械語チェック](../scripts/check-keystone-eapp-string.py) をbuild時に追加。
- **検証**：修正前objectはチェック失敗、修正後は成功。SDKとverification再build成功。
  その後ユーザーから全verification PASS報告あり。標準的な全GCC/Clang版を網羅した結果ではない。
- **詳細**：[停止原因と再試験](keystone-sdk-string-recursion.md)。

## KSB-002: report出力の固定2048-byteコピー

- **対象**：[Eyrie syscall](../keystone/runtime/call/syscall.c) のATTEST_ENCLAVE。
- **確認**：SM/SDK reportは1352 bytesだが、`copy_to_user(..., 2048)` を固定実行している。
  このコピーはSBIの成否にも依存しない。ソース確認、異常ケースのゲスト再現は未実施。
- **影響**：仕様どおり1352 bytesを用意したアプリでは後続696 bytesへの書き込みになる。
  作業バッファのreport外や以前の内容まで返す可能性がある。
- **発見時の状態**：未修正。K3側で3002は無効だが、Eyrie側のコピー処理は別に存在する。
  SMが未対応を返すことだけで、このコピーが実行されないとは扱わない。
- **当初の修正計画**：成功時だけ共通型の正確なsizeofをコピー。戻り値確認、buffer初期化・消去。
- **受入試験**：1352 bytes前後のcanary、失敗status時の出力不変更、コピーfaultのerror伝播。
  [同時実装計画C4](keystone-sbi-3002-3003-joint-plan.md) で対応する。

更新（3002/3003共通実装）：成功時のみ共通reportサイズ1352 bytesをコピーし、copy結果も確認。
実装箇所とbuild結果は [実装記録](keystone-sbi-3002-3003-implementation.md) を参照。
新しいゲスト正常・異常系はコード作成済みだが、統合実行は未確認。修正commitは本更新の履歴を参照。

## KSB-003: attestation入力コピー前の検査不足

- **対象**：同じEyrieの3002経路。
- **確認・影響**：arg2の1024上限等を検査する前にcopy_from_userを呼び、その結果も確認しない。
  runtimeの作業bufferに対する過大入力、部分コピーや不正VAを正常な入力として処理し得る。
  SM側の後段の長さ検査では、既に行ったEyrieのコピーを保護できない。
- **発見時の状態**：未修正、ソース確認。SMで3002が無効でも前処理は別途修正が必要。
- **対応・試験**：長さ・overflow・全VA範囲を先に検査し、copy結果を確認。
  0/1024/1025、NULL、不正VA、境界跨ぎと後続正常要求の復帰を実QEMUで確認する。

更新（3002/3003共通実装）：runtime共通入口で長さ・VAを先に検査し、copy errorを伝播。
実装箇所とbuild結果は [実装記録](keystone-sbi-3002-3003-implementation.md) を参照。
新しいゲスト正常・異常系はコード作成済みだが、統合実行は未確認。修正commitは本更新の履歴を参照。

## KSB-004: sealing出力capacityとコピー結果の検査不足

- **対象**：EyrieのGET_SEALING_KEY、[SDK型](../keystone/sdk/include/app/sealing.h)。
- **確認**：必要出力は192 bytes。現行runtimeはcapacityの上限しか確認せず、小さい値も受け付け、
  成功後に渡されたcapacityだけコピーする。入力・出力copyの戻り値も未確認。
- **影響**：不完全なkey structを成功として返す、大きいcapacityでstruct外までコピーする、
  copy失敗を呼び出し側へ伝えない。実際の残留内容・異常実行は未検証。
- **発見時の状態**：未修正。現K3 SMでは3003が無効。
- **対応・試験**：最低192のcapacity検査、成功時192だけコピー、copy error伝播と作業buffer消去。
  capacity 0/191/192/193、末尾canary、不正VAを確認する。

更新（3002/3003共通実装）：capacity>=192を検査し、成功時192 bytesだけ返却。両作業bufferを消去。
実装箇所とbuild結果は [実装記録](keystone-sbi-3002-3003-implementation.md) を参照。
新しいゲスト正常・異常系はコード作成済みだが、統合実行は未確認。修正commitは本更新の履歴を参照。

## KSB-005: SM sealingの引数検査とVLA

- **対象**：[get_sealing_key](../keystone/sm/src/enclave.c)、[sm_derive_sealing_key](../keystone/sm/src/sm.c)。
- **確認**：入力identifier長に応じて `info[MDSIZE + key_ident_size]` をSMスタックへ確保する。
  helper内部には長さ・加算overflow・当該EPMの範囲を十分に検査する契約がない。
  sealing経路は渡されたポインタを直接使って導出・出力する。
- **影響**：無検査で有効化すると、過大な作業領域や契約外アドレスを処理する可能性。
  3003有効版での異常入力実行は未実施。現行K3では手前で拒否される。
- **発見時の状態**：未修正。Eyrieの制限だけをSM自体の引数保証とはしない。
- **対応・試験**：4096-byte上限、EPM所有権、PA契約、overflow、固定上限workspaceを共通実装。
  最大長・超過・UTM・他EPM・SM領域と後続要求の復帰を検証する。

更新（3002/3003共通実装）：K3の3003は新共通サービスへdispatchし、上限・所有権・固定workspaceを実装。汎用platform向けの旧helper/VLAは残るため一部修正。
実装箇所とbuild結果は [実装記録](keystone-sbi-3002-3003-implementation.md) を参照。
新しいゲスト正常・異常系はコード作成済みだが、統合実行は未確認。修正commitは本更新の履歴を参照。

## KSB-006: report verifierの入力境界検査

- **対象**：[Report.cpp](../keystone/sdk/src/verifier/Report.cpp) のfromBytes/checkSignaturesOnly等。
- **確認**：fromBytesには入力byte数がなく固定sizeofを読む。署名検証でreportのdata_lenを
  使う前に1024上限を確認する処理もない。stringfy側の検査は署名検証入口の検査とは異なる。
- **影響**：不正な生reportやdata_lenを渡した場合の範囲外読み取りが懸念される。
  fromBytesだけなら呼び出し元のcapacity契約も関係するため、APIの契約と検査の両方を整備する。
- **発見時の状態**：未修正、ソース確認。クラッシュ等の実行再現は未実施。
- **対応・試験**：長さ付きchecked入口、data_len上限、JSONの負数/過大値の検査。
  短い入力・不正長は署名処理や表示前に拒否する。

更新（3002/3003共通実装）：長さ付きfromBytesChecked/fromJsonCheckedとdata_len上限、hex形式、外部pin検査を追加。旧fromBytesの呼び出し元には従来のcapacity契約が残る。
実装箇所とbuild結果は [実装記録](keystone-sbi-3002-3003-implementation.md) を参照。
新しいゲスト正常・異常系はコード作成済みだが、統合実行は未確認。修正commitは本更新の履歴を参照。

## KSB-007: random/getrandomの戻り値・引数処理

- **対象**：Eyrieのcall/sbi.c、util/rt_util.c、call/linux_wrap.cとpage_swap利用箇所。
- **修正前**：a0 statusを確認せずa1を乱数として取得し、asmのa1出力指定も不十分。
  rt_util_getrandomはコピーやSMの成否を要求byte数へ反映しない。直接uintptr_t書き込みに
  非整列への依存があり、linux_getrandomはflagsを無視していた。
- **影響**：RNG失敗を成功値として扱う、書き込み失敗を成功扱いする等。
  非整列が必ず全CPUでtrapになるという意味ではない。
- **修正済**：`867a23e57`。checked a0/a1 API、端数コピー、status/errno伝播、flagsとVA検査。
  AT_RANDOMとswap鍵取得も失敗を無視しない。
- **検証**：実ゲストmatrixを含む全verification PASSのユーザー報告あり。
  ソース未提供の全hardware RNGエラー・全partial-copy経路まで網羅したとはしない。
  [3001実装記録](keystone-sbi-3001-implementation.md) を参照。

## KSB-008: freestandingコードとstack protector

- **由来**：通常Linuxコンパイラの既定設定への依存。すべての上流buildで発生するとは限らない。
- **影響**：Eyrie/native app等が提供しないstack-canary/libc支援を要求してbuild/linkに失敗。
- **修正済**：`fc7fe2044`。該当freestanding targetだけに-fno-stack-protectorを指定。
- **根拠・確認**：[累積記録第1節](keystone-cumulative-changes.md)。後続のクロスbuild成功。
  ホストrunner全体の保護を無効にする対応ではない。

## KSB-009: raw loaderのPIE/GOT依存

- **条件・影響**：raw imageはELF再配置を受けず任意EPMへコピーされるが、GOT経由の
  stack取得や絶対アドレス依存が残り、正しいstackを設定できない。
- **由来**：loaderと移植ツールチェーンの組み合わせ。上流最新版との再照合なし。
- **修正済**：`f1c0678e5`。PC相対lla、PIE/PIC/GP relaxation依存の除去、no-relax/GOT ASSERT。
- **確認**：入口配置チェックと後続build。詳細は[累積記録第2節](keystone-cumulative-changes.md)。

## KSB-010: 固定入口でbuild-idを実行

- **観測**：GDBでmepc=0xffffffffc0000000、mcause=2、mtval=4。
  入口にはGNU build-id noteのヘッダーが置かれていた。
- **原因**：固定入口へ飛ぶloaderと、開始コードを先頭へ保証しないlink配置の組み合わせ。
  上流 `b8c2385355b2afb5b94a4e5dc503936f1502b972` にも同じ前提があったことは過去に確認済み。
  build-id生成条件によるため、上流の全環境で常時発生するとはしない。
- **修正済**：`345f9d2bd`。専用entry section、先頭配置・ENTRY・ASSERT、note除去。
- **確認**：実ゲストhello成功報告、リンク検査。[詳細とGDB証拠](keystone-eyrie-entry-build-id.md)。

## KSB-011: CMA開始位置と2 MiB leafの整列制約

- **観測**：256 MiB級EPMのPA=0x104f00000でmm.cの整列assert、OCALL前に終了。
- **原因**：大きいEPMで2 MiB leafを選ぶが、CMAはその開始整列を保証しない。
- **由来**：Eyrieの既存配置制約が新しいCMA確保方法で表面化した互換性問題。
- **修正済**：`87efe630c`。非整列部は4 KiB leaf、必要なpage tableの確保とfree_base更新。
- **確認**：build/入口検査。後の全verification PASS報告には256 MiB正常系を含むが、
  すべてのPA配置・大きさを検証した意味ではない。[累積記録第9節](keystone-cumulative-changes.md)。

## KSB-012: SDK DESTROY後のEID残存

- **対象**：[KeystoneDevice.cpp](../keystone/sdk/src/host/KeystoneDevice.cpp)。
- **原因・影響**：明示destroy成功後もeidが有効なままで、destructor等が同じDESTROY ioctlを再発行する。
- **修正済**：`0977df6be`。成功後だけeid=-1にし、失敗時はIDを保持する。
- **確認**：実guestの明示DESTROYを使う正常系。詳細は累積記録第7節。

## KSB-013: 最大buddy orderの境界判定

- **対象**：[keystone-page.c](../keystone/linux-keystone-driver/keystone-page.c)。
- **原因**：Linux 6.8以降のMAX_PAGE_ORDERはinclusiveだが、旧MAX_ORDER同様に `<` で判定していた。
- **影響**：確保可能な最大orderを試さず、4 MiB級EPMの確保に失敗し得る。
- **修正済**：`6c32be5b3`。版ごとにinclusive/exclusiveを区別する。
- **確認**：4 MiB buddy確保の実ログと後続試験。[大容量EPM](keystone-large-epm.md)。

## KSB-014: K3高位RAMとDMA制約

- **原因・影響**：K3 RAMは4 GiB以上だが、32 bit DMA mask/DMA32前提の領域確保では
  CMA予約があっても使用できない。CPU PAとDMA handleも別の概念として扱う必要がある。
- **由来**：既存driverのplatform制約で、全platformでの上流不具合とはしない。
- **修正済**：`6c32be5b3`。64 bit DMA、dma_alloc_pages、CPU PA/解放handleの分離。
  確保・解放の状態初期化も同時整備。CMAの明示予約は起動側の設定要件。
- **確認**：256 MiB級CMA確保の実ログ。[大容量EPM](keystone-large-epm.md)。

## KSB-015: FINALIZE前のunsigned EID sentinel

- **対象**：[keystone-ioctl.c](../keystone/linux-keystone-driver/keystone-ioctl.c)。
- **原因**：未作成のeid=-1をunsignedへ格納しているのに、`eid >= 0` でSM DESTROYの要否を判定。
- **影響**：FINALIZE前のcleanupでもSMへ不正EIDを渡し、driver資源解放を妨げ得る。
- **修正済**：`6c32be5b3`。unsigned -1との明示比較。
- **検証範囲**：ソースと修正差分で確認。全初期化失敗位置の実guest網羅は未確認。

## KSB-016: EID全幅検査前の32 bit切り詰め

- **対象**：SM DESTROY/RUN/RESUME helper。
- **影響**：64 bit SBI引数を狭いenclave_idへ変換すると、範囲外の値が別の有効IDになり得る。
- **修正済**：`cc885e6ad`。型変換前に元の値全体で検査しINVALID_IDを返す。
- **確認**：ソース・修正差分、現行実ecallの無効ID試験。
  高位bitを変えた全値が現行guest試験に含まれるとはしない。

## KSB-017: DESTROYの所有権とPMP解除失敗

- **原因**：ALLOCATED/DESTROYINGも破棄対象に含め、global PMP解除失敗後でも資源解放・成功復帰し得た。
- **影響**：破棄処理の重複と、不明な保護状態の物理ページ再利用。
- **修正済**：`cc885e6ad`。許可状態とDESTROYINGへの移行を同じlockで管理し、
  PMP解除失敗時は未解放資源を保持してerrorを返す。
- **確認**：現行guestでは正常CREATE/DESTROYと再作成を実行。失敗注入・競合の網羅は未確認。
  [2002/2003修正報告](keystone-sbi-2002-2003-fixes.md)。

## KSB-018: PMP失敗を無視したcontext切替

- **原因・影響**：host context等を交換した後にPMP設定し、失敗でも切替を完了し得る。
- **由来**：SMとOpenSBI接続・PMP APIの失敗契約を含むため、上流への一括帰属はしない。
- **修正済**：`cc885e6ad`。設定成功後だけcontextを交換し、失敗時はstate/thread予約を復旧。
  権限復旧不能なら不明な状態でhostへ戻らず停止する。
- **確認**：正常RUN/STOP/RESUME/EXITは現行guest試験。PMP失敗・復旧失敗の全注入は未確認。

## KSB-019: OpenSBIの復帰フレームとKeystone ABI

- **条件・影響**：RUN/STOP/RESUME/EXITで交換したフレームを共通dispatcherが再更新すると、
  PC二重加算やa0/a1破壊になる。正のKeystone statusも標準SBI error処理へそのまま渡せない。
- **由来**：今回のOpenSBI移植接続部分。
- **修正済**：`75f5c0377`。skip_regs_update等で復帰処理を一元化しstatus ABIを保持。
  同時のK3タイマーポリシー調整は全実タイマーケースの証明ではない。
- **確認**：後続正常lifecycle/OCALL試験。詳細は累積記録第4節。

## KSB-020: K3のTOR読み戻し検証が無条件拒否

- **観測**：CMA確保成功後、CREATEが100003(PMP_FAILURE)で失敗。
- **原因**：K3移植のverified PMP設定がTORを明示拒否。OpenSBI pmp_getのNAPOT向け
  比較をTOR区間の検証へそのまま使えなかった。
- **修正済**：`59a848b72`。実CSRの上下境界・mode/権限・UNSET状態を検証する。
- **確認**：後続CREATE到達と全verification PASS報告。全hartの全CSR異常注入は未確認。
  [TOR原因と修正](keystone-tor-pmp.md)。

## KSB-021: HSM停止禁止とsystem resetの衝突

- **観測**：Keystoneを実行していなくてもshutdownでhart invalid stateとFailed stop hart。
- **原因**：全hart維持のK3ポリシーとOpenSBI終了順序が不整合。QEMU syscon reset driverも未設定。
- **修正済**：`4011d1c9d`。reset許可判定後platform resetへ直接委譲し、sysconを有効化。
- **確認**：buildとdriver収録を確認。現行run-allはshutdown/rebootの成功を検査するsuiteではない。
  実際の電源断・再起動は別に確認する。[reset報告](keystone-system-reset.md)。

## KSB-022: carray生成とconfig依存関係

- **観測**：defconfigを更新しても既存buildのdriver一覧が更新されずsysconが登録されなかった。
- **対象・由来**：OpenSBIのcarray生成規則。Keystone SM自体の不具合とは分類しない。
- **修正済**：`4011d1c9d`。生成規則にKCONFIG_AUTOHEADER依存を追加。
- **確認**：過去のbuild調査と修正差分。[reset報告](keystone-system-reset.md) に詳細。

## KSB-023: SM reportの未使用data末尾が未初期化

- **対象**：[attest_enclave](../keystone/sm/src/enclave.c)。
- **確認**：ローカルstruct reportを初期化せず、入力size bytesだけdataへコピーしている。
  出力はstruct全体なので、size<1024では未使用data末尾の内容も渡される。
- **影響**：SMスタックの残留内容をreportへ含める可能性。未使用末尾は署名対象でもない。
  Eyrie側の2048-byteコピーKSB-002とは別で、SM内で1352-byte reportを完成させる段階の問題。
- **発見時の状態**：未修正、ソース確認。現行K3では3002が無効のため実際の漏出内容は未観測。
- **対応・試験**：reportを全体ゼロ初期化し、入力sizeだけを署名する既存形式を維持。
  0/短いdata/1024の取得、連続要求での未使用末尾0を実QEMUで確認する。

更新：旧attest_enclaveのstruct初期化と新K3共通workspaceの全体初期化を実装。
build確認済み、report末尾0の実guest試験は作成済み・実行未確認。

## KSB-024: HKDF最大出力でloop counterがwrapする

- 登録日：2026-11-09。状態：修正済（本共通実装の変更）。
- 対象：hkdf_sha3_512.cのhkdf_expand。
- 原因：最大出力255 blocksを許可する一方、loop変数もunsigned charだったため、
  i=255の後に0へwrapし、loopを終えられない。i-1を使うコピー位置も範囲外になり得る。
- 根拠：最大長検査、div_ceil、loop変数とコピー式のソース確認。今回の3003出力128 bytesは2 blocksで、
  この最大長ケースには該当しない。上流最新版との比較は未実施。
- 修正：loop変数をintとし、HMACへ渡す1 byte counterを別にした。
- 検証：全SMのcompile/link、通常の32/128-byte KDFを使う実QEMU初期化を確認。
  最大16320-byte KDFの実行試験は未実施。3003のAPIは任意のKDF出力長を公開しない。

## KSB-025: hosted verifier archiveのnative eappへの流用

- 登録日：2026-11-09。状態：修正済（本更新）。
- 対象：SDK verifier CMakeとverifications/crypto/Makefile。今回の3002/3003統合で導入。
- 症状：ユーザー環境のGCC 13でcrypto eappのlink時に__stack_chk_guard/__stack_chk_failが未解決。
- 原因：通常Linux用libkeystone-verifier.aをnostdlib eappへリンクした。
  archive内のverify/sha3/ge/fe等にはstack protector参照があり、最終linkの
  -fno-stack-protectorではコンパイル済みobjectの参照を除去できない。
- 根拠：ユーザー添付linkログ。上流Keystoneの不具合とは帰属しない。
- 対応：hosted verifierは維持し、C暗号primitiveだけをnative設定で別archiveへbuildする。
  強制stack-protector設定で再buildし、archiveの参照と最終nostdlib linkを検査する。
- 修正：libkeystone-verifier-eapp.aを新設し、C暗号primitiveを
  -ffreestanding/-fno-builtin/-fno-stack-protectorでコンパイル。
  crypto eappだけが新archiveを使い、Linux runnerは従来archiveを使う。
  SDK build時に新archiveのcanary/C++ runtime依存を検査する。
- 検証：強制-fstack-protector-strong設定でもSDKとcryptoパッケージをbuildできた。
  hosted archiveにcanary参照が残ること、新archiveと最終eappには残らないことを確認。
  同じeappを旧archiveへlinkすると未解決が再現し、新archiveへ差し替えると成功する対照確認も実施。
  QEMU試験より前のbuild問題であり、実行時PASSとは別に扱う。
- 再実行：make cryptoでSDK・eapp・既知値を更新し、install-crypto/install-eyrie後にQEMU試験。
  本件の修正commitはこの報告項目とbuild設定の変更履歴を参照。

## KSB-026: native main直入とglobal pointer未初期化

- 登録日：2026-11-09。状態：修正済（本更新）。
- 対象：native eappの入口、SDK起動コード、verificationのlink設定。
- 症状：cryptoの両qemu-dev bootでload page fault、障害VA=0xfffffffffffffe28、
  unavailableでstore page fault、障害VA=0xfffffffffffff808。report返却は0件。
  同じログで3001の7ケースとhost側の3002/3003権限検査はPASS。
- 原因：nostdlibでmainを入口にし、CRTによるgp初期化を行わない一方、
  linker relaxationはgp相対アクセスを生成していた。
- 根拠：実際のcrypto ELFにlw variant,-472(gp)、addi output,gp,-2040等を確認。
  gp=0なら-472は報告のfe28と一致し、unavailableで出力VAが負になる症状にも一致する。
- 対応：SDKに専用native startを追加し、norelaxのPC相対命令でgpを初期化してからmainへcallする。
  検証用native eappの入口を変更し、ELF入口と実命令をbuild時に検査する。
- 修正：SDKのcrt.Sにkeystone_eapp_startを追加。norelax/norvcのllaでgpを初期化する。
  root build対象のhello-nativeと全native verificationのentryとarchive強制取り込みを指定。
  libc付きeappは既存CRTのまま。旧nativeサンプルで独自linker scriptを使うものは未変更。
- 検証：実ELF入口とAUIPC/ADDIの計算値が__global_pointer$に一致することをbuild時に検査。
  QEMU user-modeのSDK起動probeでmain直入はgp不一致(41)、新入口は正常終了(0)を確認。
  このprobeはLinux exitを使う独立CPUテストで、OpenSBI/SMの模擬試験や統合PASSではない。
  SDK、hello-native、全guestパッケージと既知値の再生成は成功。ゲストsuite再実行は未確認。
  ユーザー環境のELFの正確なPC対応はそのELFで再確認が必要。
  上流最新版の発生状況は未照合で、今回のnative build統合問題として管理する。

- 追加のユーザー実行結果：添付log.txt（今回受領）では修正後の
  `run-qemu.py --all` がqemu-dev 2 boot、unavailable 1 boot、乱数7ケースすべてPASS。
  この報告範囲でシステムゲスト統合の成功を確認。実機の検証結果ではない。

## 要確認事項・対象外との境界

- **PA/VA契約**：3002の既存copyはMPRVでVAを扱える場合があるため、
  「PAへ変換していない」という事実だけでは単独の確定バグとしない。
  3002/3003をEPM内PAへ統一する移植計画に対する適合性課題として管理する。
  satp/MPRV/PMPの意味と復帰を実QEMUで確認してから共通化する。
- 3002/3003の未実装provider、実機endorsement、paging追加機能、CMA容量不足、
  未マウント/procや古いdriverのロードは、それだけでKeystoneコードのバグとはしない。
- matrixがFAILの理由を表示しなかった点は試験ツールの診断不足で、`e1cbdabca`で改善済み。
  乱数eapp本体の停止原因KSB-001とは区別する。
- 本書は既知の発見と修正記録の整理。新たな網羅的な不具合調査を完了したものではない。

## KSB-027: STOP要求の事前検査と戻り値の明確化

- 状態：修正済（SBI 3004実装完了作業で登録）。
- 対象：SM `stop_enclave`、`sbi_sm_stop_enclave`、Eyrie `sbi_stop_enclave`。
- 根拠：不正requestもSTOPPEDへ変更しcontext_switch_to_host後にUNKNOWN_ERRORを返す。
  helperはこれも切替成功として扱い、SDKは回復不能なエラーとして終了する。
  ホスト復帰のa1は明示設定されず、Eyrieのinline asmもa1出力を宣言していない。
- 由来：取込STOP処理とOpenSBI接続の契約。現上流最新版での再現は未確認。
- 方針：0/1以外は切替前にILLEGAL_ARGUMENTで同じenclaveへ返す。
  2（STOP_EXIT_ENCLAVEという旧定義）もSTOPの有効な要求ではない。終了は3006。
  STOP成功時のホストa1は0。既存のtimer/OCALLフレーム保存方式は維持する。
- 修正：[9b7285ae5](https://github.com/yutaka-ishikawa/public/commit/9b7285ae5)「Complete SBI 3004 STOP validation and real guest verification」。
  hostへのa1を0とし、Eyrie asmのa1入出力も宣言。専用試験は実statusを観測する。
- 検証：SM/OpenSBI trace版、SDK、通常／専用runtimeと全verificationパッケージをbuild。
  sbi-errorsのkernel objectもbuild。通常runtimeから専用hookが除外されることをnmで確認。
- 未確認：対応Linux Image/rootfsと完全なModule.symversがないため、追加試験の実ゲスト実行、
  .koリンクは未実施。timer区間の非同期statusだけでM_TIMERとM_SOFTは区別できず、
  原因特定はSM traceのcauseを確認する。既存全PASS報告を新試験のPASSには流用しない。

- 2026-11-09追記：Sstc共存対応後の現行Linux全verification PASSをユーザーから受領。
  両Sstc profileの実行有無は未明示。実機・競合の網羅成功は含めない。

## KSB-028: QEMUでSstc抑止が適用されない

- 状態：修正済（[e4a736519](https://github.com/yutaka-ishikawa/public/commit/e4a736519)）。対象はOpenSBI generic初期化とK3 timer hooks。
- 根拠：ユーザーログで3004直接yieldは3回成功、timer区間は全3bootで0回。
  QEMU root compatibleはriscv-virtio,qemuだが、Sstc抑止はspacemit,k3等のoverrideのみ。
  同じ構成を実QEMU起動するとBoot HART ISA Extensionsにsstcが残ることを再現。
- 原因：OpenSBIがSstcでstimecmpを設定する一方、SMはmidelegを0にしM_TIMER経路を前提にする。
  enclaveへの切替時にSTIEが無効になり、enclaveのsie操作では委譲されていないbitを有効化できない。
- 由来：本リポジトリのK3/OpenSBI移植。上流Keystoneのバグとはしない。
- 方針：CONFIG_K3_KEYSTONE_SM時、全generic platformで既存のSstc抑止とDT修正を実施。
  QEMUに実機用のearly/final initを強制しない。
- 検証：修正後の実QEMU起動でBoot HARTのsstc非表示を確認。全hartのfeature初期化と
  渡すDTの両方に既存hookを接続。Linux suiteの再実行は未確認。

- 現在の方式：[bff3e8dcb](https://github.com/yutaka-ishikawa/public/commit/bff3e8dcb)で全体抑止の対策を、Linux Sstcを維持する
  [共存方式](keystone-sstc-coexistence.md) に置換。enclaveのTIME要求だけMTIMERへ振り分け、
  RUN/RESUMEで独立期限を設定し、STOP/EXITでhost状態を復元する。
  最小の実SM経路でon/off・host期限復元を確認。Linux全suiteと実機は未確認。

- 2026-11-09追記：Sstc共存対応後の現行Linux全verification PASSをユーザーから受領。
  両Sstc profileの実行有無は未明示。実機・競合の網羅成功は含めない。

## KSB-029: 共有パッケージの断続的MD5不一致

- 状態：要確認。対象はゲストのmakeself展開と9p共有。
- 根拠：最新ユーザーlogの乱数7ケース中wtm-not-readyだけが、SBI実行前のMD5不一致で終了2。
  他6ケースは同じrandom.keでPASS。ホスト側ファイルの同時更新や読み取り条件は不明。
- 原因：未確定。makeselfのMS_ddはpipe上のddに固定block数を指定しfullblockを指定しない。
  短いreadの扱いや共有上の変更を調べる必要があり、MD5不一致だけでSM故障とはしない。
- 対策実装済み：4つのguest runnerでパッケージをゲストローカルへsnapshotし、
  既存checksum検査を維持して展開する。共有からのコピー後も不一致なら失敗する。
  不一致を無視したり、成功へ置き換えたりしない。
- 未確認：元の断続的現象の再現と解消。ローカルsnapshotだけで全原因を解消したとはしない。

## KSB-030: enclave trap vectorの4-byte整列不足

- 状態：修正済（[e4a736519](https://github.com/yutaka-ishikawa/public/commit/e4a736519)）。対象はSM trap.Sと最終link。
- 根拠：実firmwareのtrap_vector_enclave=0x33d56（低2bit=2）。mtvecはBASEが4-byte境界、
  低2bitはMODEのため、このアドレスは正しいDirect handlerを指定しない。
  実QEMUでenclave実行中のmtvecは通常OpenSBI handlerの0x1020005f0のまま。
  M_TIMER後にmie.MTIE=0、mcause=interrupt7となり、STOP traceも中断statusも発生しない。
- 由来：本リポジトリの圧縮命令を含むOpenSBIとSMのリンク配置。
  trap.Sのnorvc指定だけではsection先頭の4-byte整列を保証しない。
- 方針：vectorへ.balign 4を指定し、最終firmware ELFのsymbol整列をbuild時に検査する。
- 修正：.balign 4と最終ELF symbolのbuild時検査。修正後vector=0x33d58。
- 検証：実QEMUに既存のSM/Eyrieとnative eappをロードし、調査用S-mode runnerから
  CREATE/RUNを実行。修正前は非同期中断0（F0）。整列修正後はcause=7→STOP→2005 RESUME
  →3006 EXIT→2002 DESTROY成功、中断1（P1）を観測。Linux/driverを使わない調査経路であり、
  9回OCALL・3回timer等を含むLinux専用verificationのPASSではない。
  記録は [timer evidence](../verifications/sbi-3004/evidence/timer-preemption.md)。

- 2026-11-09追記：Sstc共存対応後の現行Linux全verification PASSをユーザーから受領。
  両Sstc profileの実行有無は未明示。実機・競合の網羅成功は含めない。

## 追加のユーザー検証報告（2026-11-09）

Sstc共存実装 `bff3e8dcb` の対応後、現行verification全PASSとのユーザー報告を受領。
KSB-027／028／030に関連するSTOP・timer共存の現行Linux suiteについて、
従来の実行待ちをユーザー成功確認済みへ更新する。
KSB-029のchecksum不一致も現行試験での再発報告はないが、根本原因は未確定のため
要確認の分類は維持する。全PASSだけを根本原因の解消証明とは扱わない。
今回の報告にはコマンド／ログ添付がなく、Sstc on/off両profileの実行有無は未明示。
実機・競合の網羅試験やクラウド側での独立再実行の成功へは広げない。

## 今後の必須更新手順

新たに同様の不具合を発見したら、修正前でも本書へ登録する。
連番を末尾へ追加し、既存IDは変更・再利用しない。調査途中なら未確認事項を明示する。
修正時はそのIDの状態、修正コミット、検証結果と残る未確認範囲を更新する。
ユーザーから結果を受領した場合も、報告範囲を超えてPASSを広げず追記する。

再利用する記入様式:

```markdown
## KSB-NNN: 短い題名

- 登録日・最終更新日：YYYY-MM-DD（日本時間）
- 状態：未修正／修正済／要確認
- 対象：component、source、影響版またはcommit
- 由来：取込コード／上流比較済（commit指定）／移植／build環境／未確認
- 症状・再現条件：実行手順、必要な設定
- 原因・影響：確認できた事実と推測を分ける
- 根拠：ログ、機械語、source差分、関連報告
- 修正：方針と実際のcommit。未修正なら予定・対応計画
- 検証：build／バイナリ検査／実QEMU／実機／ユーザー報告を区別
- 未確認：未実施の条件・残る制約
```

秘密鍵やsealing keyを報告書へ掲載しない。既存の累積変更記録と個別解析文書は根拠として
リンクし、本書を不具合ID・現在の状態を確認する入口にする。
