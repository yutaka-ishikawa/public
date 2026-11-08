# K3 Keystone未移植機能の調査と移植計画

作成日：2026-11-08（日本時間）  
調査対象：`8b5e1d870`時点のリポジトリ  
上流比較対象：Keystone `b8c2385355b2afb5b94a4e5dc503936f1502b972`

## 目的と進め方

現在のhelloが動く状態を維持しながら、元のKeystoneが提供する機能をK3/OpenSBIへ順に接続する。まずQEMUで機能単位の完走を確認し、実機向けの基盤整備も並行して進める。

現在の実装は**PMPによる基本実行を中心とする構成**であり、乱数・attestation・sealingは明示的に無効化されている。これらの暗号アルゴリズムやSDK APIを一から作る必要はなく、既存コードを利用してK3の鍵・乱数・起動時情報と接続することが主な作業になる。

本計画は移植を進めるための機能調査であり、脆弱性調査ではない。「上流にあるがK3で無効」「ビルドされていない」「実装はあるが実行未確認」「上流にも制限がある」を区別する。実機の隔離保証に必要な追加作業を、すべてのQEMU機能移植を始める前の条件にはしない。

この変更では調査・計画の文書化までを行い、APIの有効化や実機への書き込みは行わない。

## 1. 現在地：SBI機能の一覧

根拠は[`sm_call.h`](../keystone/sdk/include/shared/sm_call.h)、[`sm-sbi-opensbi.c`](../keystone/sm/src/sm-sbi-opensbi.c)、[`sm-sbi.c`](../keystone/sm/src/sm-sbi.c)。対象のKeystone SBI機能は10個である。

| SBI ID | 機能 | K3の状態 | 判定 |
| --- | --- | --- | --- |
| 2001 | CREATE | 作成・PMP更新・enclave測定へ接続済み。helloで使用。 | 基本経路あり。異常系・繰り返しの網羅確認が必要。 |
| 2002 | DESTROY | `destroy_enclave()`へ接続済み。 | 未移植ではない。runner終了・異常終了後の解放と再利用を明示的に確認する。 |
| 2003 | RUN | トラップフレーム切替へ接続済み。 | QEMUでhelloの動作報告あり。 |
| 2005 | RESUME | フレーム切替とホスト側再開経路あり。 | OCALL・タイマー中断を明示的に起こす試験が必要。 |
| 3001 | RANDOM | adapterとSM helperの両方でNOT_IMPLEMENTED。platformの乱数供給も未実装。 | **未移植・優先度高。** |
| 3002 | ATTEST | adapterとSM helperの両方でNOT_IMPLEMENTED。report生成・署名コード自体はある。 | **未移植・優先度高。** |
| 3003 | GET_SEALING_KEY | adapterとSM helperの両方でNOT_IMPLEMENTED。鍵導出コード自体はある。 | **未移植・優先度高。** |
| 3004 | STOP | ホストへの復帰処理あり。 | 未移植ではない。タイマー中断・OCALLの復帰コードを試験する。 |
| 3006 | EXIT | 終了値を渡してホストへ復帰する処理あり。 | 基本経路あり。0以外の終了値も確認する。 |
| 4000 | CALL_PLUGIN | dispatcherは接続済み。K3ビルドで有効なSMプラグイン指定なし。 | optional機能。multimemは未接続であり、上流側の制限もある。 |

helloが動くことから、すべてのライフサイクル・エラー処理が確認済みとはしない。一方、ソース上接続されているSTOP/RESUME等を「未実装」として書き直すこともしない。

## 2. SDK・Eyrie・example・実機対応の分類

| 項目 | 状態 | 次の作業 |
| --- | --- | --- |
| SHA3、Ed25519、HKDF | コードあり。`crypto.c`は比較した上流と同一。 | ベクトルと機能試験で再利用。暗号実装の再作成は不要。 |
| enclave測定 | CREATE内の`validate_and_hash_enclave()`あり。`attest.c`は上流と同一。 | SDKが求める期待ハッシュとの一致を確認。attestation無効と測定未実装を混同しない。 |
| SDK attestation/sealing API | 存在する。SM側で無効。 | SMの有効化に合わせ、戻り値・データ形式まで通す。 |
| report verifier | SDKコードあり。`Report.cpp`は上流と同一。 | K3の信頼する公開鍵とSM測定方式へ接続。 |
| UTM／OCALL | ランタイム・SDK・ドライバのコードあり。hello-nativeもビルド対象。 | 明示的なOCALL往復とSTOP/RESUME試験を追加。 |
| `hello.ke` | ビルド・ゲスト組み込み済み、ユーザーの動作確認あり。 | 常時回帰対象として維持。 |
| `hello-native.ke` | ビルド・ゲスト組み込み済み。明示的OCALLを含む。 | 今回の調査では全経路のゲスト完走は未確認。回帰試験へ追加。 |
| `attestor.ke` | 上流exampleあり。現スクリプトのビルド・組み込み対象外。 | SM有効化、署名鍵、SM測定、期待鍵の移植と一緒に対応。 |
| `tests.ke` | 上流テストあり。現スクリプトの対象外。 | 最初に非暗号テストを動かし、後でattestation/sealingを追加。 |
| PAGING／PAGE_HASH／PAGE_CRYPTO | Eyrieに実装あり。デフォルトOFF。 | multimem、backing store、乱数を先に整備。 |
| NET_SYSCALL等 | Eyrieにoptional実装あり。hello用構成では未指定。 | 要求に応じて有効化・実行確認。全Linux syscallの移植と同一視しない。 |
| CPU hotplug／suspend | K3構成でSTOP/SUSPENDを明示的に拒否。 | 固定オンライン制約として維持し、必要なら後の独立フェーズで拡張。 |
| 実機PMP・起動・タイマー・IPI | QEMU向け固定条件あり。実機資料との未解決差分あり。 | 実機基盤の作業列で対応。 |
| DMA／IOPMP | K3 platform hooksに実処理なし。 | 実機での隔離保証・デバイス利用を目標にする場合のプラットフォーム作業。上流APIの未接続とは分ける。 |

空のplatform hooksだけから欠落機能とは判断しない。比較した上流のgeneric platformも、create/destroy/switch hooksの多くは何もしない実装である。

### 2.1 上流と同一のコードを確認した範囲

以下は今回、比較対象コミットのファイル内容と一致することを確認した。

- `sm/src/crypto.c`、`sm/src/attest.c`、`sm/src/plugins/plugins.c`
- `runtime/mm/paging.c`、`runtime/mm/page_swap.c`、`runtime/util/rt_util.c`
- `sdk/src/verifier/Report.cpp`、`examples/tests/CMakeLists.txt`

一致はK3での実行成功を意味しない。再利用できる部分と、プラットフォーム接続を分けるための根拠である。

## 3. 重要な未接続箇所

### 3.1 乱数：SMだけでなくruntimeの戻り値処理も必要

[`platform.c`](../keystone/sm/src/platform/spacemit_k3/platform.c)の`k3_platform_random_checked()`は失敗を返す。`platform_random()`は呼び出し互換用に値0を返すが、K3 adapterはRANDOMをその前で拒否している。

一方、[`runtime/call/sbi.c`](../keystone/runtime/call/sbi.c)の`sbi_random()`はa1をデータとして取り出し、a0のエラーを検査していない。[`rt_util_getrandom()`](../keystone/runtime/util/rt_util.c)も要求したバイト数を成功として返す。ENV_SETUPのAT_RANDOMやページ暗号化からも使われるため、**helloが動くことは乱数の正しい供給を意味しない**。

移植では、成功時の既存ABI（a0=status、a1=乱数）を維持し、失敗をruntimeまで伝播できるchecked APIを用意する。インラインasmでもa1を正しい出力として扱い、最適化ありで検証する。エラー値と有効な乱数値を値だけで区別してはいけない。

### 3.2 鍵供給：開発鍵オプションだけでは有効化されない

K3の`sm_init()`は`sm_copy_key()`を呼ばず、鍵・SM証明書を初期化しない。さらにadapterとSM helperの二重の無効化がある。

開発鍵のコピー処理は存在するが、以下の制限がある。

- 通常の`build_keystone_sm.sh`から開発鍵オプションを渡す経路がない。
- 低レベルcompilerの`--dev-embedded-key`は生成済み鍵includeを必要とする。
- これを指定してもK3の初期化スキップとSBI拒否は解除されない。
- 開発鍵をコピーする関数自体も、SM hash・SM signature・device public keyを0にするため、元のverifierが期待する証明書チェーンは完成しない。

[`k3_b1_handoff.c`](../keystone/sm/src/platform/spacemit_k3/k3_b1_handoff.c)にはdescriptor検査はあるが、実際のprovision処理は`-1`を返す未実装である。descriptorを作っただけで鍵供給が完成したと扱わない。

### 3.3 Attestation：鍵以外にSM測定とverifierの移植が必要

reportの生成・Ed25519署名・SDKの検証コードはある。欠けているのは、SM鍵、device側の信頼する公開鍵、SM測定値とその署名、K3構成に合う期待値の作成である。

既存attestation exampleには次の上流構成への依存がある。

- `--sm-bin`のデフォルトが`images/fw_jump.bin`等の旧配置。
- 期待するSMハッシュ計算が、旧Sanctum bootloaderの`0x1ff000`バイトのゼロ埋め領域を前提とする。
- verifierが上流の`test_dev_key.h`の公開鍵を使用する。

K3ではSMがOpenSBIと一体化しているため、ファイル名の変更だけでは一致しない。測定対象・長さ・パディング・再配置の扱いを定義し、起動側とverifierで共有する必要がある。実行後の可変data/BSSや鍵バッファを含めて、その場で`_fw_start.._fw_end`を単純にハッシュする方法は採用しない。

### 3.4 Sealing：既存の導出処理を接続する

`sm_derive_sealing_key()`は既存のHKDFを使用し、SM秘密鍵、enclave hash、key identifierから鍵を導出する。`get_sealing_key()`とSDK・runtimeの経路も存在する。

主な作業は、安定した鍵供給、初期化・機能可否判定、二重の拒否の整理、出力・署名と再起動後の整合性の確認である。毎回異なる一時鍵では、再起動後のsealed data復元を実現できない。鍵の寿命と更新時の扱いを先に決める。

元のKeystoneと同じキー導出APIを提供することと、暗号化ファイルシステムや汎用の永続ストレージを新設することは別の作業である。

### 3.5 Paging：multimemをONにするだけでは不足

SMプラグインdispatcherはあるが、K3 compilerは`PLUGIN_ENABLE_MULTIMEM`を指定しない。さらに、multimemは`REGION_OTHER`の領域を参照する一方、通常のCREATEはEPMとUTMしか登録しない。これは比較した上流の通常CREATEにもある制限である。

したがって、paging全体を「K3で落とした既存完成機能」とは扱わない。backing storeをどこから確保し、どのABIで登録し、誰が解放するかを設計する追加統合作業が必要である。`PAGE_CRYPTO`と一部のswap処理には乱数も必要になる。

## 4. 推奨する実装順序

機能の実装と実機適合性確認を二つの作業列で進める。各段階は独立してレビュー・回帰確認できる変更にする。新機能が未完了でも既存のhelloを使える状態を保つ。

```text
P0 基本実行の回帰・機能一覧
  → P1 checked乱数APIとQEMU供給
      → P2 鍵・測定providerと初期化
          → P3 sealing
          → P4 attestationとverifier
      → P5 optional paging／syscall拡張

H0 実機ブート形式・領域・DTの整合
  → H1 実機全hartのPMP／IPI／タイマー
      → H2 実機の乱数・鍵provider
          → 実機でP3/P4の完了確認

P6 hotplug／suspend等は固定オンライン構成の完成後に別途判断
```

### P0：基準となる回帰試験と機能プロファイル

**作業**

- 現在のPMP-only状態の機能一覧をビルド情報へ記録する。将来、乱数・鍵・attestation等の可否を個別に表せる形にする。
- `hello`、`hello-native`、上流の非暗号テストをビルド・選択実行できるようにする。
- `tests.ke`の生成スクリプトを点検する。現状の`retval`探索先が各テストディレクトリ名と一致するか、生成するシェルの改行・終了コードを実際に確認する。
- 手作業の出力確認だけでなく、runnerの初期化失敗・戻り値を検出する。
- `samples/`に追加するアプリは既存方針に従って通常のMakefileを使う。上流テストの既存CMakeは再利用してよい。

**完了条件**

- CREATE/RUN/EXIT/DESTROYを繰り返しても領域が再利用できる。
- 明示的OCALLでSTOP/RESUMEを往復し、データと戻り値が一致する。
- 長時間計算でタイマー中断・再開が発生しても終了する。
- 失敗ケースと未実装機能が成功扱いにならない。ゲスト実行結果を機械的に判定できる。

### P1：乱数をエンドツーエンドで移植

採用設計は [SBI 3001の3方式](keystone-sbi-3001-three-backends.md)。
①Zkr、②OP-TEEを参考にしたJupiter2 WTM RNG、③他機種用interfaceと
counterによる開発用生成器を用意する。③は暗号学的乱数ではないことを明示する。

共通のchecked APIとa0/a1 ABI、Eyrieのgetrandom/AT_RANDOMと
全呼び出し元のエラー伝播を実装する。
②のQEMU用MMIOデバイスを追加し、3方式をそれぞれ実SBI・実SM経路で試験する。
模擬providerや製品関数の抽出試験は追加しない。
共通DRBGやboot seed方式は将来候補で、今回の必須実装ではない。

### P2：鍵・SM測定providerと機能初期化

**作業**

- `sm_is_ready()`の基本実行準備と、乱数・sealing・attestationの準備状態を分ける。鍵がないだけでCREATE/RUNまで止めない。
- QEMUの開発用providerを明示的な設定として作り、永続する開発鍵、device public key、SM測定値・証明書を一貫して供給する。
- K3の統合ファームウェアで測定する正規化済みバイト列を定義する。測定範囲・サイズ・パディング・relocation処理と、鍵や可変領域の除外を仕様化する。
- ビルド出力へ公開情報のmanifestを作る。秘密鍵をレポートやmanifestへ出さない。
- 実機側は先行ブート段階またはハードウェアの鍵供給方式に接続できるinterfaceを用意する。providerが証明書を供給できない場合はattestationを未準備のままにする。
- adapter/helper双方の一律K3拒否を、機能ごとの準備状態による判定に置き換える。

**完了条件**

- providerの有無と失敗を各機能が正しく判定する。
- 開発providerで鍵・公開鍵・測定値・証明書が対応する。
- 同じ入力から起動側とverifierが同じ測定値を求める。
- 開発用の署名動作確認と、実機のデバイス由来の証明をビルド・結果表示で区別できる。

### P3：sealing key取得と復元サンプル

**作業**

- 既存`sm_derive_sealing_key()`／`get_sealing_key()`をproviderへ接続し、SBI 3003を有効にする。
- runtime・SDKの引数、返却構造体、status、署名の取り扱いを確認する。
- 上流`test-data-sealing`を通し、`samples/sealing/`に鍵取得・暗号化・復元を試せるMakefileサンプルを追加する。

**完了条件**

- 同一SM鍵・同一enclave hash・同一identifierで同じ鍵が得られる。
- identifierまたはenclave hashを変えると導出結果が変わる。
- 再起動後も同じprovider鍵を使う場合に復元できる。鍵更新時の期待動作を記録する。
- 上流サンプルの成功表示だけでなく、出力長・署名・復元したデータを検査する。

### P4：attestationとホストverifier

**作業**

- 既存report生成へproviderのSM hash・SM公開鍵・SM証明書を接続し、SBI 3002を有効にする。
- runtimeはSMの失敗時にreportを成功出力としてコピーしないようにする。
- 旧Sanctumサイズ・旧鍵・旧`fw_jump.bin`パスへの依存を解消する。
- `attestor.ke`をビルド・パッケージ化し、`samples/attestation/`へMakefile版の最小例を追加する。
- verifierは期待するenclave測定、SM測定、device公開鍵、nonceを明示的に入力できるようにする。

**完了条件**

- 上流形式のreportがSDK verifierで検証できる。
- enclave内容、SM測定、nonce、署名、期待公開鍵の不一致をそれぞれ検出する。
- QEMUの開発用証明書での機能完了と、実機providerでの完了を別に記録する。

### P5：optional機能を必要順に追加

**通常のアプリ機能**

- IO/LINUX/NET syscallオプションの利用範囲を列挙し、必要なものから小さいアプリで検証する。
- OCALL、UTM、複数enclaveの並行実行と資源上限の確認を拡充する。未実装と判明したものだけ実装する。

**ページング**

- 先にbacking storeの所有・割り当て・登録・解放を設計し、ドライバ／SDK／SM間のABIを定義する。
- multimemを有効にして`REGION_OTHER`へ実際の領域を登録する。
- PAGE_HASH、PAGE_CRYPTOを段階的に有効化する。後者はP1の乱数供給を前提とする。
- 上流の`runtime/test`にあるpage swap/Merkleテストを再利用し、さらにゲスト内で実際にEPMを超えるデータを扱わせる。

**完了条件**

- syscallごとの成功・失敗・OCALL復帰を確認する。
- pagingは実際にevict／swap-inを発生させ、データが一致し、終了後に資源を回収できる。
- pluginをONにしただけでpaging完了とはしない。

### H0–H2：実機向け作業列

既に提供された情報は[実機調査](jupiter2-hardware-compatibility.md)と[インベントリ](jupiter2-hardware-inventory.json)に保存してある。再収集せず利用する。

| 段階 | 作業 | 完了条件 |
| --- | --- | --- |
| H0 | FSBL/FIT・NOR領域・ロード先・OpenSBI入力DTを照合。実機向けビルド構成をQEMU向け固定条件から分ける。 | 実際の起動用コンテナが領域に収まり、正しいK3 overrideが選ばれる。 |
| H1 | X100/A100全hartのPMP・PMACFGと割り込み・タイマーを確認し、固定16 PMP等の仮定を測定値に基づいて調整。 | Linuxと全hart起動、各CPU種類でのhello、中断・再開、PMP更新・ACKが成功する。 |
| H2 | M-modeで使える実機の乱数・鍵供給・SM測定を実装。リセット先とのプロトコルを確認。 | 実機でP1/P3/P4とshutdown/rebootが完了する。 |

追加で必要なのは、Linux以前のOpenSBI起動ログ、実際のOpenSBI入力DTB、動作中のベンダーコミット・defconfig・ビルド方法、FSBL等のロード仕様、M-modeのCSR実測値である。

実機DTBにefuseノードがあることだけで、秘密鍵や乱数を供給できると判断しない。ハードウェア資料やベンダー実装が必要な箇所は、その情報が揃うまでQEMU側の機能移植と分離する。

### P6：初期の完了範囲に含めない拡張

- **hotplug／suspend復帰**：PMP世代のreplayとHSM状態を整合させてから対応。通常のHART_STOPの拒否を外すだけでは実装しない。
- **Sstcの直接利用**：当面はM_TIMERモデルを維持。必要になった場合にstimecmp・割り込み状態の切替を独立して設計する。
- **DMA／IOPMP**：実機でenclaveと周辺デバイスをどう利用するかに応じたプラットフォーム作業。元のgeneric Keystoneでplatform hooksが空であることと区別する。
- **FPU／Vector等の拡張状態**：現在のcontext構造から任意のFP/Vectorアプリの完動を判断しない。対応ISAを明記し、状態保存・復元の調査と計算テストを独立して行う。
- **単一enclaveの多スレッド化**：現在の`MAX_ENCL_THREADS=1`は上流にもある制限。複数enclaveの実行とは別であり、今回の移植欠落には数えない。

## 5. 最初に着手する変更

次の実装は**P0とP1**を推奨する。

1. 非暗号の既存機能をゲストで判定できる回帰テストと、未実装機能の戻り値確認を追加する。
2. checked乱数API、QEMU用provider、ENV_SETUP/getrandomへの伝播を実装する。
3. その後P2の鍵・測定providerを整備し、sealing、attestationの順に接続する。P3とP4はP2完了後なら順番を入れ替えられる。

この順序なら、既に残っている暗号・report・SDKコードを活用しながら、普通のアプリが依存する乱数を先に完成できる。ハードウェア鍵情報の待ち時間によって、QEMUでの機能移植全体が止まることも避けられる。

## 6. 完了の判定と成果物

各段階で以下を残す。

- ソース変更、再現可能なビルド設定、使用したprovider・runtime optionの一覧。
- ホスト単体テスト、ゲストの機能テスト、実機テストを区別した結果。
- 既存hello、hello-native、終了処理への回帰結果。
- `samples/<機能>/README.md`にビルド・実行・期待結果。
- `doc/`に実装済み機能表と残る制約を更新。

「ビルドできた」「SBIを有効化した」「上流サンプルが終了した」「実機で動いた」を別々の到達点として記録する。全機能の完了とは、計画した機能についてSDK→Eyrie→SM→provider→verifierまで必要な経路が動き、その対象環境が明記された状態を指す。

## 関連資料

- [実機適合性の調査](jupiter2-hardware-compatibility.md)
- [実機情報](jupiter2-hardware-inventory.json)
- [Eyrie入口の修正](keystone-eyrie-entry-build-id.md)
- [shutdown修正](keystone-system-reset.md)
- [Keystoneビルドスクリプト](../scripts/build-install-keystone.sh)
- [SM初期化](../keystone/sm/src/sm.c)
- [K3 platform provider](../keystone/sm/src/platform/spacemit_k3/platform.c)
- [attestation／sealing処理](../keystone/sm/src/enclave.c)
- [SDK report検証](../keystone/sdk/src/verifier/Report.cpp)
- [上流テストの構成](../keystone/examples/tests/CMakeLists.txt)
