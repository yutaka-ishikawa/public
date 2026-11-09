# K3 Keystone移植状況と残作業計画

実装基準：Linux Sstc／SM M_TIMER共存に加え、FID 4000の領域登録・照会・解放まで。
FID 4000実装完了：[2f87a9d57](https://github.com/yutaka-ishikawa/public/commit/2f87a9d5736d7653c512e050e944f17b787098e5)。今回の実QEMU全suiteはSstc on/offともPASS。
検証状況更新：2026-11-09、`bb84fa987`の展開・診断修正後、off再確認手順に対する全verification PASSをユーザーから受領。
初回調査は `8b5e1d870`、当時の上流比較対象はKeystone
`b8c2385355b2afb5b94a4e5dc503936f1502b972`。
旧計画の「乱数・attestation・sealingが未実装」は現在には当てはまらない。

本書の「実装済み」は指定した環境・範囲についての判定。
QEMUでの実装完了、ゲスト試験成功、実機での動作確認は分けて記録する。
以下のコミットリンクは、このリポジトリの実装履歴を指す。

## 1. 現状と完了コミット

### SBI機能

Keystone拡張IDは `0x08424b45`。表の番号はFID。
根拠は [定義](../keystone/sdk/include/shared/sm_call.h)、
[OpenSBI adapter](../keystone/sm/src/sm-sbi-opensbi.c)、
[SMサービス](../keystone/sm/src/sm-sbi.c)。

| FID | 機能・現在の状態 | 実装・修正の到達コミット | 検証範囲・残作業 |
| --- | --- | --- | --- |
| 2001 | CREATE：測定、PMP設定、EPM確保へ接続済み。 | 初回調査時点ですでに接続済み。大容量対応は [6c32be5b3](https://github.com/yutaka-ishikawa/public/commit/6c32be5b3)、全hartのTOR PMP対応は [59a848b72](https://github.com/yutaka-ishikawa/public/commit/59a848b72)、Eyrieの非NAPOT EPM対応は [87efe630c](https://github.com/yutaka-ishikawa/public/commit/87efe630c)。 | QEMUで2/256 MiB free-memory構成を試験済み。並行実行・資源枯渇の網羅試験は残る。 |
| 2002 | DESTROY：接続済み。所有権・PMP失敗処理・ID幅を修正。 | [cc885e6ad](https://github.com/yutaka-ishikawa/public/commit/cc885e6ad)。実ゲストでの明示DESTROYと再利用試験は [0977df6be](https://github.com/yutaka-ishikawa/public/commit/0977df6be)。 | QEMU suiteで繰り返し作成・解放を確認。競合するDESTROY等は追加対象。 |
| 2003 | RUN：OpenSBIフレームからenclaveへ切替可能。 | trap return・status ABI・timer方針の修正は [75f5c0377](https://github.com/yutaka-ishikawa/public/commit/75f5c0377)、ID幅等は [cc885e6ad](https://github.com/yutaka-ishikawa/public/commit/cc885e6ad)。 | QEMU suiteで実行確認。任意ISAの全拡張状態対応を意味しない。 |
| 2005 | RESUME：OCALL後の再開を接続済み。 | 同じフレーム切替基盤 [75f5c0377](https://github.com/yutaka-ishikawa/public/commit/75f5c0377)。現行の実ゲスト回帰試験は [0977df6be](https://github.com/yutaka-ishikawa/public/commit/0977df6be)。 | OCALLおよび実タイマーによるSTOP→RESUMEを含む現行suite全PASSのユーザー報告を受領。migration・競合の網羅確認は残る。 |
| 3001 | RANDOM：Zkr、Jupiter2 WTM、他機種用interface＋開発用counterを実装済み。 | 機能・QEMUモデル・7ケース試験は [867a23e57](https://github.com/yutaka-ishikawa/public/commit/867a23e57)。native SDK停止修正は [2c99fb066](https://github.com/yutaka-ishikawa/public/commit/2c99fb066)、native入口修正は [9455ab462](https://github.com/yutaka-ishikawa/public/commit/9455ab462)。 | ユーザー環境のQEMUで7ケースPASS。実機WTMと実機Zkrの利用可否・品質は未確認。counterは暗号学的乱数ではない。 |
| 3002 | ATTEST：共通provider、report生成、署名チェーン、外部pin付きverifierを実装済み。 | 初回実装は [9bdd68447](https://github.com/yutaka-ishikawa/public/commit/9bdd68447)。native crypto archive分離は [08f0ac69c](https://github.com/yutaka-ishikawa/public/commit/08f0ac69c)、起動不具合修正まで含む到達点は [9455ab462](https://github.com/yutaka-ishikawa/public/commit/9455ab462)。 | qemu-devの正常系と通常unavailableの拒否をQEMUで確認。実機鍵providerと認証済みbootは未実装。 |
| 3003 | GET_SEALING_KEY：既存HKDF・署名を共通基盤へ接続済み。 | 3002と同時実装 [9bdd68447](https://github.com/yutaka-ishikawa/public/commit/9bdd68447)、native実行修正の到達点 [9455ab462](https://github.com/yutaka-ishikawa/public/commit/9455ab462)。 | QEMUで既知値・識別子分離・再起動整合性等を確認。実機永続鍵・更新運用・暗号化保存サンプルは残る。 |
| 3004 | STOP：0/1の有効要求、無効要求の事前拒否、host status/a1と切替処理を実装済み。 | 既存の切替基盤は [75f5c0377](https://github.com/yutaka-ishikawa/public/commit/75f5c0377)。要求検査・返却ABI修正と専用実ゲスト試験の追加は [9b7285ae5](https://github.com/yutaka-ishikawa/public/commit/9b7285ae5)。 timer経路修正は [e4a736519](https://github.com/yutaka-ishikawa/public/commit/e4a736519)、Sstc共存・独立MTIMERと11 OCALL試験は [bff3e8dcb](https://github.com/yutaka-ishikawa/public/commit/bff3e8dcb)。 | 共存対応後のLinuxを含むQEMU全verification PASSをユーザー確認済み。直接yield・不正要求・通常timer区間・guest timer取消し後の中断・host sleep/wakeupを含む現行suiteの範囲。実機・競合の網羅確認は残る。詳細は [3004実装記録](keystone-sbi-3004-implementation.md)。 |
| 3006 | EXIT：終了値を伴うホスト復帰を接続済み。 | フレーム切替基盤 [75f5c0377](https://github.com/yutaka-ishikawa/public/commit/75f5c0377)、実ゲスト試験 [0977df6be](https://github.com/yutaka-ishikawa/public/commit/0977df6be)。 | suiteの正常終了を確認。全終了値・異常終了条件の網羅試験は残る。 |
| 4000 | CALL_PLUGIN／multimem：K3で登録・照会・解放を統合済み。 | [2f87a9d57](https://github.com/yutaka-ishikawa/public/commit/2f87a9d5736d7653c512e050e944f17b787098e5)。 | 実QEMUで有効/無効、Sstc on/off、256 MiB超CMA/TOR、異常系・回収を検証。pagingは別工程。 |
| 2006 | K3拡張CREATE_ENCLAVE_V2：version付きbacking登録。 | [2f87a9d57](https://github.com/yutaka-ishikawa/public/commit/2f87a9d5736d7653c512e050e944f17b787098e5)（4000と同時実装）。 | 旧2001を維持し、不正header・範囲・他enclaveの重複登録を拒否。 |

**2004は定義済みの機能ではない。** 未実装機能として追加せず、未定義番号への拒否応答を試験する。
初回調査以前の接続については、今回確認した修正コミットを原始実装コミットとは表記しない。

### 共通基盤・開発環境

| 項目 | 到達点とコミット | 残る範囲 |
| --- | --- | --- |
| BianbuとKeystoneの別イメージ・起動選択 | [03c05080f](https://github.com/yutaka-ishikawa/public/commit/03c05080f)。 | 実機のboot image生成とは別。 |
| SDK／Eyrieの入口とnative起動 | build-id入口修正 [345f9d2bd](https://github.com/yutaka-ishikawa/public/commit/345f9d2bd)、gp初期化 [9455ab462](https://github.com/yutaka-ishikawa/public/commit/9455ab462)。 | 独自linker scriptを使う全上流exampleへの適用を意味しない。 |
| 大容量EPM | DMA/CMA [6c32be5b3](https://github.com/yutaka-ishikawa/public/commit/6c32be5b3) → TOR [59a848b72](https://github.com/yutaka-ishikawa/public/commit/59a848b72) → Eyrie map [87efe630c](https://github.com/yutaka-ishikawa/public/commit/87efe630c)。 | 256 MiB free-memory指定時のEPMはruntime等を含むため256 MiBより大きい。 |
| 共通鍵・測定・暗号サービス | [9bdd68447](https://github.com/yutaka-ishikawa/public/commit/9bdd68447)。 | 開発用qemu-devとunavailableのみ。hardware providerなし。 |
| 実QEMU verification | 模擬試験を削除し実ゲストへ移行 [0977df6be](https://github.com/yutaka-ishikawa/public/commit/0977df6be)、暗号suite統合 [9bdd68447](https://github.com/yutaka-ishikawa/public/commit/9bdd68447)、SBI番号表示 [d96b39cbd](https://github.com/yutaka-ishikawa/public/commit/d96b39cbd)。 | 全分岐・全競合の網羅試験ではない。 |
| build／installとコンポーネント別make | [12f9ca522](https://github.com/yutaka-ishikawa/public/commit/12f9ca522)。 | `make`はbuildのみ、配置は`make install`。 |
| Makefile版helloサンプル | 作成 [136207442](https://github.com/yutaka-ishikawa/public/commit/136207442)、`samples/helloworld`配置 [069dd0d05](https://github.com/yutaka-ishikawa/public/commit/069dd0d05)。 | sealing／attestation版は未追加。 |
| QEMU shutdown／reboot | [4011d1c9d](https://github.com/yutaka-ishikawa/public/commit/4011d1c9d)。 | 実機reset controllerでの確認は別途必要。 |

## 2. 現在の検証結果と再現方法

2026-11-09、展開・診断修正 `bb84fa987` の後、直前に案内した
`run-qemu.py --all --sstc off` の再確認手順に対し、ユーザーから「全パス」との報告を受領した。
**現行のLinuxゲスト・driver・OpenSBI・SM・Eyrie/enclaveを通る全verificationは
ユーザー再実行でも成功確認済み**。以前の明示offログの失敗は現行環境では再発していない。

クラウドでもSstc on/off各3 boot全suite＋乱数7ケース、通常trace=0版のon/off全suiteを
確認済み。3004の11 OCALL、通常／timer取消し後の中断・RESUME、host sleep/wakeup、
cryptoと2/256 MiBのedgecall・PMP参照拒否を含む。

今回のユーザー報告は成功の申告であり、新しいserialログ・manifestは未添付。
過去のtimer取消し後0回の原因・当時のartifact一致までは確定しないが、
現行の移植作業を止める未解決の実行失敗としては扱わない。
実機、全hart固定実行・migration・競合の網羅検証は引き続き別の残作業である。

履歴：`9455ab462`後の全PASS、3004追加後のtimer失敗、`e4a736519`の修正、
`bff3e8dcb`のSstc共存、`bb84fa987`の展開・診断修正を経て、今回の再実行全PASSに到達した。

再現手順（既定のSstc有効profile）：

```bash
git pull
make build JOBS=4
# 起動中のQEMUを正常終了してから
make install
python3 verifications/crypto/run-qemu.py --all
```

`verifications/crypto/results/manifest.json`、各serialログ、`results/rng/`と
実行時の `git rev-parse HEAD` を保存する。
実試験のソース配置は [verification README](../verifications/README.md) を参照。
Linuxゲスト→driver→OpenSBI→SM→Eyrie/enclaveを通る試験であり、SMの模擬実行ではない。
ただしQEMU成功をMilk-V Jupiter2実機の成功とは扱わない。

### 明示的なSstc offログとクラウドでの追加実行

その後、ユーザーの `--all --sstc off` ログではSTOP timer区間とpackage展開で失敗した。
従来の全PASS申告を、このoff実行の成功へは広げない。
[bb84fa987](https://github.com/yutaka-ishikawa/public/commit/bb84fa987)で展開のpipe短読取りとfirmware診断を修正し、新規Linux環境でon/offそれぞれ3 boot全suite＋乱数7ケースを
全PASS確認した。通常trace=0版のon/off全suiteも確認済み。
その後、ユーザー環境でも再実行全PASSを確認。元のtimer取消し後0回の原因・artifact一致は
未確定の履歴としてKSB-031に残し、現行の実行待ち項目からは外す。
[調査・hash付き結果](keystone-verification-off-log-followup.md)を参照。

## 3. 再利用方針：KeystoneとOpenSBI

今後も最初に同梱OpenSBIを調べ、同じ契約で利用できる機能を優先する。
同様の機能がなければKeystoneの既存実装を移植し、暗号アルゴリズム等を再作成しない。

- 3001はOpenSBIのZkr検出、timer API、DT処理・同期処理を再利用済み。
  WTMはvendor OP-TEE K3のCPU MMIOアクセスを参考に移植した。
  セキュアプロセッサへのmailbox RPCを実装したものではない。
- 3002/3003は同梱OpenSBIにKeystone互換report・sealingサービスや
  SHA3／Ed25519／HKDFライブラリが見つからなかったため、Keystoneの暗号実装を再利用。
  OpenSBIのtrap対応load/store、spinlock、メモリ操作・ecall基盤を利用する。
- pagingのenclave固有の所有権・測定・swap管理はOpenSBIの一般的なメモリ操作では代替しない。
- HSM／timer／IPI／resetの拡張は既存OpenSBIの仕組みに接続し、並行した独自管理系を増やさない。

この調査結論は同梱版についてであり、将来版や他社実装全体の不存在を主張しない。

## 4. 未実装・未統合・検証待ちの区別

| 項目 | 分類 | ソース上の根拠・現在の制約 |
| --- | --- | --- |
| 実機の3002/3003用鍵provider | 未実装 | `security/security.c`はqemu-dev条件以外unavailable。`k3_b1_platform_provision()`は失敗を返すstub。descriptor検査だけでは鍵供給は完成しない。 |
| Jupiter2 WTM RNG | 実装済み・実機未確認 | `rng/rng.c`にCPU MMIO経路あり。DT追加、clock・RNGのLinux/OP-TEEとの所有権調整が必要。QEMUは部分レジスタモデル。 |
| 実機boot／全hart PMP・IPI・timer | 検証待ち・適合作業が必要 | K3系OpenSBIが共通でも、FSBL/FIT、入力DT、実PMP/PMACFG、実割り込み構成との一致は未確認。 |
| CALL_PLUGIN／multimem | K3統合済み・QEMU検証済み | P0〜P2完了。[実装・検証](../verifications/sbi-4000/README.md)。実機・PMP失敗注入・同時実行等の網羅試験は残る。 |
| paging | コードあり・K3未統合 | FID 4000計画P3。runtimeのPAGINGは既定OFF。 |
| PAGE_HASH／PAGE_CRYPTO | コードあり・未統合 | pagingとswap領域、乱数供給への接続が前提。乱数移植だけでpaging完了とはならない。 |
| 上流attestor.ke／tests.ke | ビルド・組み込み未統合 | 現スクリプトの対象はhelloとhello-native。専用crypto verificationの成功とは別。 |
| samples/sealing・samples/attestation | 未追加 | 現在のsamplesはhelloworldのみ。 |
| IO／LINUX／NET syscallオプション | optional・範囲確認待ち | Eyrieに既存実装あり。全Linux syscallの移植という要求とは分け、必要なAPIを選ぶ。 |
| CPU hotplug／suspend | 現在の固定オンライン方針の対象外 | 拒否解除だけではPMP状態とHSMの整合が保証されない。 |
| Linux Sstc／SM M_TIMER共存 | [bff3e8dcb](https://github.com/yutaka-ishikawa/public/commit/bff3e8dcb)で実装済み、現行QEMU全verificationはユーザーPASS確認済み | [共存実装](keystone-sstc-coexistence.md)。最小実行でon/offを確認し、その後Linux全suiteの全PASS報告を受領。追加のクラウドLinux実行でon/off全suiteを確認済み。提供offログの失敗後、ユーザー再実行でも全PASS。過去原因は未確定。実機とenclaveへのSstc直接公開は対象外。 |
| FPU／Vectorの状態管理 | 対応範囲の調査・拡張待ち | 汎用整数contextの動作からFP/Vectorの保存・復元成功は推定しない。 |
| DMA／IOPMP | 実機向け未整備 | CPU PMPの試験ではデバイスDMA隔離を検証できない。 |
| 単一enclaveの多スレッド | 上流にもある制約 | `MAX_ENCL_THREADS=1`。複数enclaveの実行と別の拡張で、K3だけの移植漏れとはしない。 |

## 5. 今後の実装計画

以下は残作業の計画。現行suiteの全PASSは達成済みで、未着手の拡張・追加検証を分けて記載する。
各段階で実装コミット、対象profile、実行結果と未確認範囲を本書に追記する。
QEMU作業列と実機作業列は並行可能だが、実機資料がない部分を推測で完成扱いしない。

### N0：現行機能の回帰・残る検証を先行

1. 現在の全suiteを基準として、HEADとartifact manifestを保存する。
2. 現行3004／Sstc共存suiteはユーザー全PASS確認済み。
   クラウドで両profileのLinux実行記録を追加済み。ユーザーのoff再実行も全PASS。次は全hart・migration等の範囲拡充を進める。
   続いて非0 EXIT、複数enclaveの交互実行、RUN中DESTROY拒否、資源枯渇後の回収を追加する。
3. 暗号providerの片側ready・初期化失敗、鍵/root変更・SM更新時の鍵導出の期待動作、
   Zkr WAIT/DEAD等、既存matrixが通さない異常経路を専用profileで確認する。

完了条件：各条件で期待status・データ・復帰・資源再利用を検査できること。
対象SBI番号と試験名を表示し、未実行をPASSにしない。模擬SM試験は追加しない。

### N1：アプリ開発用サンプルと上流exampleの統合

1. 既存crypto API・外部pin verifierを使い、`samples/attestation`へMakefile版を追加する。
2. `samples/sealing`へ取得鍵を使ったデータ保存・復元例を追加する。
   認証付き暗号ライブラリの利用可否を先に調べ、暗号方式を独自設計しない。
3. 上流attestorの旧鍵・旧fw_jump.bin・旧測定padding前提を現在の公開manifest入力へ変更する。
4. 上流tests.keの生成スクリプト・retval判定を確認し、非暗号から順にbuild対象へ追加する。

完了条件：各READMEのbuild／実行手順が通り、正常系・改変拒否・再起動後の復元を
検査する。開発用qemu-dev identityであることを結果に明示する。
SBI 3002/3003自体をもう一度新規実装する段階ではない。

### N2：SBI 4000／multimemの登録・照会・解放（完了）

[FID 4000実装計画](keystone-fid-4000-plan.md)のP0〜P2を実装した。
SDK/driverで別backing領域を確保し、拡張CREATE 2006でSMへ固定登録する。
4000は登録値をprivate EPMへ返し、DESTROY/close時に解除・解放する。
OpenSBIのecall/domain検査と既存IPI/PMP機構を再利用する。

実QEMUで不正呼出し、他enclaveへの重複登録拒否、OCALL/RESUME・timer中断・CPU切替、
256 MiB + 4 KiB CMA/TOR、反復破棄・プロセス終了後のdriver保持量回復を確認。
結果とhashは[verification記録](../verifications/sbi-4000/results.md)を参照。
SDKのfd等のhost資源解放不足はKSB-035として別記し、全資源の回収完了とはしない。

### N3：paging→PAGE_HASH→PAGE_CRYPTO

N2は完了した。次工程として着手する。既存Keystone paging／Merkle／swap実装を再利用し、
まず基本evict／swap-in、次にhash検査、最後に暗号化を有効化する。
PAGE_CRYPTOには利用可能な乱数profileと鍵・counter管理が必要。

完了条件：実EPMを超えるデータを読み書きし、swap-in後の一致、外部データ改変の拒否、
乱数失敗時の処理、終了後の回収をQEMUゲストで確認する。
上流にあるテストの再利用と実enclave経路の試験を区別する。

### H0：Jupiter2実機の起動条件を確定

既提供の [実機資料](jupiter2-hardware-compatibility.md) と
[インベントリ](jupiter2-hardware-inventory.json) を再利用する。
追加で必要なものはOpenSBI以前からの起動ログ、OpenSBI入力DTB、実際のvendor commit・
defconfig・build方法、FSBL/FITのロード仕様、M-modeでのPMP/PMACFG等の値。

作業：QEMUと実機のbuild profileを分け、bootコンテナ・領域・DT overrideを整合させる。
完了条件：意図したfirmwareでLinuxと全hartが起動し、実機でhelloが完走すること。
既存OpenSBIが同じK3ブランチであることだけではこの条件を満たさない。

### H1：全hart・WTM・shutdownの実機適合

H0に続き、各CPU種類でPMP/TOR更新とACK、IPI、timer中断・復帰、resetを測定する。
WTMは既存MMIO実装を使い、DTとclock所有権を確定する。
OpenSBI既存のIPI／timer／reset機構を優先して利用する。

完了条件：実機で基本suiteと3001の適用可能なケースが通ること。
Zkrは実機の拡張検出・CSRアクセス結果を根拠に選択する。
WTMでデータを読めたことと乱数品質・隔離の保証は分けて記録する。

### H2：実機用鍵・測定providerと3002/3003

1. 先行boot段階と信頼起点、永続device鍵の所在、鍵の受渡しと消去を確定する。
2. 共通security基盤へhardware providerを追加する。B1 descriptorを使う場合も、
   認証済みhandoff・領域保護・全hart所有権を満たす実処理へ接続する。
3. boot imageの認証と測定範囲を実機load/relocation方式に合わせて定義し、外部pinを配布する。
4. 3002/3003の正常系・未準備・再起動・更新時の鍵と署名の整合を試験する。

完了条件：qemu-devを使わず実機identityでreportを検証し、永続鍵の期待する寿命で
sealingデータを復元できること。情報不足時はunavailableを維持する。
QEMUにもproviderの正常／失敗profileを用意できるが、実ハードウェアの信頼性は別途確認する。

### N4／H3：要求する用途に応じた拡張

- IO／NET syscall：必要APIを選び、既存Eyrie実装を有効化して成功・エラー・OCALL復帰を試験。
- FPU／Vector：対象ISAを決め、host/enclave間の保存・復元、初期値、中断後の計算結果を試験。
- hotplug／suspend：OpenSBI HSMとPMP世代replay、offline hartの復帰を整合させてから有効化。
- enclaveへのSstc直接公開：Linuxとの共存は実装済み。Eyrieにstimecmpを使わせる場合は、独立した状態管理・割込み配送を追加設計する。
- DMA／IOPMP：実機デバイスの利用モデルを先に決め、IOPMP設定と実DMAアクセスで確認。
- 単一enclave多スレッド：上流制約を超える追加設計として、通常の移植から独立させる。

これらの完了条件は各設計時に具体化する。現在の全PASSはこれらの対応完了を意味しない。

## 6. 推奨する次の順序

```text
QEMU: N0 回帰拡充 → N1 サンプル統合 → N2 multimem → N3 paging
実機: H0 起動条件 → H1 全hart/WTM/reset → H2 鍵・測定/3002/3003
用途別: N4/H3 syscall・拡張状態・電源管理・DMA（必要な項目から設計）
```

各実装では通常のMakefile・既存build/install構成と承認済みK&Rスタイルを使う。
発見した不具合は [不具合報告書](keystone-bug-report.md) に登録し、実装コミットと
検証範囲を追記する。コミットがあることだけを実行成功の証拠にしない。

## 関連資料

- [SBI 3001実装](keystone-sbi-3001-implementation.md)／[3方式設計](keystone-sbi-3001-three-backends.md)
- [3002/3003共通計画](keystone-sbi-3002-3003-joint-plan.md)／[実装記録](keystone-sbi-3002-3003-implementation.md)
- [累積修正](keystone-cumulative-changes.md)／[不具合報告書](keystone-bug-report.md)
- [verificationとソース配置](../verifications/README.md)／[build/install](make-build-install.md)
- [実機適合性](jupiter2-hardware-compatibility.md)／[WTM調査](jupiter2-security-engine-rng.md)

### SBI 3004の追加ログ対応

[e4a736519](https://github.com/yutaka-ishikawa/public/commit/e4a736519)で、QEMUでのSstc抑止未適用とtrap vectorの4-byte整列不足を修正。
元の全3bootではSTOP試験で停止しており、後続crypto/edgecallのPASSではない。
最小の実QEMU SM/Eyrie経路で修正後のM_TIMER→STOP→RESUME→EXIT→DESTROYを確認したが、
当時はLinux suiteの再実行が未確認だった。現在は共存対応後の全PASS報告を受領済み。[実装記録](keystone-sbi-3004-implementation.md)を参照。

### Linux Sstc／SM M_TIMER共存の実装到達点

[bff3e8dcb](https://github.com/yutaka-ishikawa/public/commit/bff3e8dcb)でLinuxのSstcを維持する方式へ更新した。
独立した10msのSM timer、host状態の保存／復元、enclave TIMEの振分け、
QEMU `--sstc on|off` と11 OCALLの専用試験を実装。
最小の実SM/Eyrie経路で両profileの中断とhost期限復元を確認した。
[設計・検証範囲](keystone-sstc-coexistence.md) を参照。共存対応後のLinux全suiteはユーザー全PASS確認済み。
その後、off再確認手順に対するユーザー全PASS報告も受領済み。実機は未確認。
