# Eyrie・Keystone側の累積修正記録

現在の不具合ID・状態は [継続管理台帳](keystone-bug-report.md) を参照してください。
本書の過去の検証状況は、その記録時点のものです。

更新日：2026-11-09（日本時間）。対象はこのリポジトリで行ったEyrie/runtime、raw loader、
SDK、Keystone Linuxドライバ、Keystone SMの修正。
以前のソース一式の取り込みに含まれていた移植コード全体の履歴ではなく、
取り込み後に確認・修正した内容をまとめる。

## 構成と起動段階

```text
Linux runner / SDK
 → Keystone Linuxドライバ
 → ecall → OpenSBI内のKeystone SM
 → raw loader（まだ物理アドレスで実行）
 → ページテーブルを設定しSATPを切り替える
 → Eyrie runtime（S-mode）
 → enclaveアプリ（U-mode）
 → OCALL / STOP / RESUME / EXIT
 → SDKからDESTROY
```

Eyrieはenclave内でアプリを支えるruntimeで、SMはOpenSBIに組み込んだM-mode側の
処理である。同じKeystone側でも、どの段階の変更かを区別する。

## 修正一覧

| 対象 | 問題・変更 | 関連コミット |
| --- | --- | --- |
| Eyrie・SDK eapp/edge・hello-native | freestandingコードでstack protector用のlibc支援を要求しない。 | `fc7fe2044` |
| raw loader | PIE/GOT依存のスタック設定を廃止し、任意のEPM配置で実行できるようにする。 | `f1c0678e5` |
| Eyrie入口 | 固定入口へGNU build-id noteが置かれ、データを命令として実行する問題を修正。 | `345f9d2bd` |
| SM/OpenSBI接続 | trap frame、PC加算、正のKeystone status、STOP/RESUME復帰を調整。 | `75f5c0377` |
| SMトレース | ビルド時指定でCREATE/RUN/PMP同期等の進行を表示。 | `2c7ab9496` |
| SM CREATE/RUN/RESUME/DESTROY | EID全幅検査、状態・資源の所有、PMP失敗と復旧処理を修正。 | `cc885e6ad` |
| Linuxドライバ | 最大buddy order、64 bit DMA、CMA確保、失敗時とFINALIZE前の解放を修正。 | `6c32be5b3` |
| SDK DESTROY | 成功後にIDを無効化し、destructorによる二重DESTROYを防ぐ。 | `0977df6be` |
| SM TOR PMP | TORを無条件拒否せず、全対象hartで実CSRの上下境界・権限を検証。 | `59a848b72` |
| Eyrie/raw loaderの物理メモリマップ | CMAの2 MiB非整列開始位置を4 KiBページで扱う。 | 本記録と同時の修正 |

これらを一括して「元のKeystoneの不具合」とは扱わない。
ツールチェーンの既定値への依存、今回のOpenSBI移植コードの問題、
元のEyrieが前提にしていたメモリ配置の制約を含む。

## 1. freestandingビルドとstack protector

対象：`runtime/CMakeLists.txt`、`sdk/src/app/CMakeLists.txt`、
`sdk/src/edge/CMakeLists.txt`、`examples/hello-native/CMakeLists.txt`。

通常のLinux向けコンパイラはstack protectorを既定で使うことがある。
Eyrieやnative enclaveには通常のlibcやstack-canary支援がないため、
対象のfreestandingコードへ `-fno-stack-protector` を指定した。
Linux側runner全体のstack protectorを無効化する変更ではない。

## 2. raw loaderのPIE/GOT・再配置

対象：[loader.S](../keystone/runtime/loader-binary/loader.S)、
[loader.lds](../keystone/runtime/loader-binary/loader.lds)、
[loader CMake](../keystone/runtime/loader-binary/CMakeLists.txt)、runtime共通設定。

raw loaderはELFローダによる再配置処理を受けず、EPMの任意の物理位置へコピーされる。
従来のスタックアドレス取得がGOT経由になると、raw imageに必要なGOTがなかったり、
リンク時のアドレスを実行時に参照したりして、正しいスタックを作れなかった。

PC相対の `lla` を使い、PIE/PIC・GP relaxationへの依存を取り除いた。
リンク時は `--no-relax` とGOTが空であることのASSERTを使い、
再配置されていない絶対GOTを含むraw imageを生成しない。

## 3. 固定入口のbuild-id note問題

対象：[entry.S](../keystone/runtime/sys/entry.S)、
[runtime.ld.S](../keystone/runtime/runtime.ld.S)。

SM/raw loaderから渡るEyrieの入口は `0xffffffffc0000000`。
GNU build-id noteが入口側へ配置されると、noteの先頭データを命令として実行する。
実ゲストで `mepc=0xffffffffc0000000`、`mcause=2`、`mtval=4` を観測した。

開始コードを専用sectionにし、linker scriptで先頭へ配置・保持する。
ENTRYと配置ASSERTを設定し、不要なnoteを破棄する。
詳細は [入口の停止原因](keystone-eyrie-entry-build-id.md) を参照。

## 4. OpenSBIとのtrap frame・status・タイマー経路

対象：[sm-sbi-opensbi.c](../keystone/sm/src/sm-sbi-opensbi.c)、
[sbi_trap_hack.c](../keystone/sm/src/sbi_trap_hack.c)。

RUN/RESUME/STOP/EXITは、単に元のecallへ戻る処理ではなく、復帰先のフレームを交換する。
交換後のPCやa0/a1をOpenSBIの共通処理が再度更新しないようにし、
Keystoneの正のstatusを互換ABIとして扱う。
割り込みSTOPのPC調整も、hostフレームの二重加算を避けるよう修正した。

関連するOpenSBI側のK3タイマーポリシーではSSTCとM-modeタイマー配送の整合を調整した。
これらは移植接続部分の変更であり、全hart・実タイマー中断の全ケースが
実ゲストで検証済みという意味ではない。

## 5. EID・状態・PMP失敗時の処理

対象：[enclave.c](../keystone/sm/src/enclave.c)、
[sm-sbi.c](../keystone/sm/src/sm-sbi.c)。

DESTROYは許可された状態だけを同じlock内で破棄中状態へ移す。
PMP解除が失敗したらメモリを再利用できるものとして成功返却しない。
RUN/RESUMEはPMP設定が成立してからcontextを交換し、失敗時の復旧と
state/thread数の扱いを調整した。復旧不能時は不明な保護状態でLinuxへ戻らない。
EIDは32 bitへ切り詰める前に、渡された全幅で範囲検査する。

詳細は [SBI 2002/2003の修正](keystone-sbi-2002-2003-fixes.md) を参照。
過去の模擬試験と結果表は削除済みで、現在の実QEMU試験のPASSへ流用しない。

## 6. Linuxドライバの大容量EPM確保

対象：[keystone-page.c](../keystone/linux-keystone-driver/keystone-page.c)、
[keystone.c](../keystone/linux-keystone-driver/keystone.c)、
[keystone-ioctl.c](../keystone/linux-keystone-driver/keystone-ioctl.c)。

EPMはEnclave Private Memory。SMへ渡す物理連続領域である。
Linux 6.8以降のMAX_PAGE_ORDERは有効な最大値なので、比較をinclusiveに修正した。
K3のRAMは4 GiB以上にあるため、32 bit DMA maskとDMA32指定を廃止した。
大容量は `dma_alloc_pages()` でCMA経由のページを確保し、CPU PAとDMA handleを
分けて管理する。初期化失敗後の構造体と解放状態もクリアする。
FINALIZE前のEID=-1はunsigned型に格納されるので、明示比較で扱う。

起動スクリプト側の512 MiB CMA予約も必要。
詳細・更新手順は [大容量EPM](keystone-large-epm.md) を参照。

## 7. SDKの明示的DESTROY

対象：[KeystoneDevice.cpp](../keystone/sdk/src/host/KeystoneDevice.cpp)。

統合試験ではdestructor任せにせず、DESTROYの戻り値を確認する。
成功後はSDK内部のIDを-1にし、その後のdestructorが同じDESTROY ioctlを
再度発行しないようにした。失敗した場合はIDを維持する。
この変更を試験に反映するにはSDKの再ビルドが必要。

## 8. CMA領域に対するTOR PMP

対象：[pmp.c](../keystone/sm/src/pmp.c)。

256 MiBにプログラム等を加えたCMA領域は2の冪サイズでなく、単一NAPOTでは表せない。
元の全hart設定の読み戻し検証はTORを明示的に拒否していたため、
CMA確保後のCREATEが100003（PMP_FAILURE）になった。

TORの上下pmpaddr、上側の権限・mode、下側のOFF状態を実CSRから確認するようにした。
UNSET後の確認、fence/sfence、各対象hartの世代ACKを維持する。
詳細は [TOR PMPの原因と修正](keystone-tor-pmp.md) を参照。

今回の後続ログではCREATE後のraw loaderまで進んでいる。
ただし、それだけで256 MiB版のアプリ・OCALL・DESTROYまでの成功とは扱わない。

## 9. 今回：CMA開始位置とEyrieの2 MiBページ制約

対象：[mm/mm.c](../keystone/runtime/mm/mm.c) の
`__map_with_reserved_page_table_64()` と `map_with_reserved_page_table()`。
同じソースがraw loader用とEyrie runtime用のライブラリへ組み込まれる。

### 観測と原因

```text
EPM allocated: 268656640 bytes at 104f00000 (DMA/CMA)
assertion failed at .../mm/mm.c:275
FAIL guest: SDK=0 exit=18446744073709551615 callbacks=0 failed=0
```

旧コードの275行目は、選択したleaf pageサイズへの物理開始位置の整列検査だった。
範囲が2 MiBを超えると2 MiB leafを選んだが、CMA開始位置0x104f00000は
2 MiB境界から1 MiBずれている。CMAは常に2 MiB整列を保証するものではない。

失敗した段階は、raw loaderがEPM全体のruntime用direct mapを作るところ。
まだEyrieの通常bootやU-modeアプリのOCALLには到達していない。
`SDK=0` は呼び出し経路が正常復帰したことを表すだけで、アプリ成功ではない。
exitの全bitが1なのはassertから `sbi_exit_enclave(-1)` した結果。
callbacks=0もその段階と一致する。

### 修正後のマッピング

- 物理・仮想両方が2 MiB境界に整列し、残りサイズも2 MiB以上なら2 MiB leafを使う。
- それ以外は4 KiB leafを使う。末尾の端数もEPMの範囲どおりにマップする。
- 最初のL3テーブルには予約済みページを使い、追加分をSPAから確保する。
- 静的テーブルとdirect map上の動的テーブルではPA変換が異なることを考慮する。
- 開始・サイズの4 KiB整列、加算overflow、1 GiB以内のL1 windowという制約を検査する。

今回の非整列EPMは約256 MiBなので、129個のL3テーブルが必要となる。
1個は予約済みで、追加128ページ＝512 KiBをfree memoryから使う。
loaderは消費したページをfree_base_finalへ反映するため、runtimeのfree allocatorへ
ページテーブル用メモリを再び渡さない。

アサーションを外して不正な2 MiB leafを作る変更ではない。
4 KiBで正しくマップできるようにしたもので、2 MiB整列の通常ケースも維持する。
1 GiBを超えるEPMや複数L1 windowへの拡張は今回の対象外。

## 実ゲストで確認した範囲

| 観測 | 判定 |
| --- | --- |
| 従来のhello実行と2 MiB設定edgecallの8往復 | ユーザーから成功報告あり。 |
| 4 MiB EPMのbuddy確保 | ユーザーの実ゲストログで成功。 |
| 256 MiB級EPMのCMA確保 | ユーザーの実ゲストログで成功。 |
| TOR修正後、256 MiB版がraw loaderのmm.cへ到達 | 今回のログで確認。 |
| 今回の4 KiB fallback修正後の256 MiB OCALL/DESTROY | 未確認。 |
| 省略なしの全統合試験、PMP直接参照拒否、全hart・実タイマーの網羅 | 未確認。 |

この表はユーザーの画面報告と現在のコードを対応させたもの。
コンパイル・リンク成功や以前の模擬試験を、実ゲストの全機能PASSへ読み替えない。
現在の試験構成は [QEMU統合試験](../verifications/README.md) を参照。

## 今回の開発環境での確認

- 修正版Eyrie/raw loaderをhello・hello-native向けにクロスビルドした。
- 2 MiB/256 MiB設定のedgecallパッケージを再生成し、整合性検査に成功した。
- runtime入口配置の検査2件と、raw loader入口の検査4件に成功した。
  これらはリンク配置・入口命令の検査で、OpenSBI/SMとの統合実行を代用しない。
- 修正後の256 MiBパッケージの実ゲスト実行は未確認。

## 今回の修正を反映する手順

ホストのリポジトリルートで、Eyrieとloaderを再ビルドして試験パッケージへ入れる。

```bash
git pull
bash scripts/build-install-keystone.sh --only sdk
make -C verifications guest
```

稼働中ゲストで共有リポジトリの新しいパッケージを実行する。

```bash
cd /mnt/host/verifications
bash run-all.sh
```

今回のmm.c修正だけなら、カーネル・Linuxドライバ・OpenSBI・rootfsの再ビルドは不要。
TOR修正前のファームウェアをまだ使っている場合は、それも更新してQEMUを起動し直す。
古いEyrie/loaderを内包した既存のhello.ke等は自動で置き換わらない。
必要に応じて新しいパッケージをゲストへコピーする。
SDK/Eyrieのビルド先を変えている場合は、試験のKEYSTONE_BUILD_DIRも合わせる。

## SBI 3001: 3方式の実装と検証環境

Zkr、Jupiter2 WTM CPU MMIO、他機種向けcounter hookを追加しました。
Eyrieのstatus/value分離、getrandomの端数・非整列・引数検証、AT_RANDOMと
page swapの乱数取得失敗伝播を変更しました。
方式別firmwareのビルド、起動オプション、実ゲスト専用の検証コードと
QEMUマトリクスを追加しています。実施済みのSM初期化確認と、未実施の
enclave統合実行の区別は [実装記録](keystone-sbi-3001-implementation.md) を参照してください。

## ルートMakefileと既存rootfsの更新

`make` / `make install` でQEMU、通常版と乱数3方式のfirmware、Linux、SDK/Eyrie、
ドライバ、verification、samplesをビルドし、既存rootfsにもドライバ・examples・
ゲストツールを反映する入口を追加しました。rootfsは一時コピーを更新して検査後に
置き換えます。元データ保持・置換・実行権限・失敗時保持・使用中ロックの検査は
実ext4イメージで確認済みです。Makefile経由のSDKとhelloworldビルドも確認済みです。
この作業環境では完全なカーネル成果物がないため、全体の `make install` と
更新後ゲストの起動までは未確認です。[操作方法](make-build-install.md) を参照してください。

## Makefileのbuild/install分離

初期の一括方式を変更し、`make` はbuildだけ、`make install` はビルド済み成果物の
配置だけにしました。一括処理は `make build-install` です。
QEMU、OpenSBI/SM、Eyrie、Keystone、verifications等の個別ターゲットと、
対応する `install-*` を追加しています。OpenSBI/SMとEyrie・sampleの実buildで
最終PREFIXが作られないことを確認し、一時PREFIXへのfirmware・SDK/runtime・sampleの
installも確認しました。全体のLinux/ゲスト統合実行とは区別しています。
仕様と依存関係は [make操作方法](make-build-install.md) を参照してください。

## 乱数matrixのFAIL診断表示

FAILだけを表示していた処理を変更し、失敗理由・ログパス・serialログ末尾を端末に
表示するようにしました。ゲストの終了コード、SMのbackend/status、AT_RANDOM完了を
結果に記録し、タイムアウトはプロンプト待ちとコマンド投入後を区別します。
CRCRLFとbash-/sh-形式のプロンプトにも対応しました。
ログ判定の5件の単体試験は確認済みです。報告された全FAILの停止原因は、
実際のゲストログを確認するまで未特定です。

## SDK string primitiveの自己再帰防止

乱数matrixの実ゲストログで、OCALL前のstore page faultと128 KiBスタック下端超過が
報告されました。実SDKの逆アセンブルでGCCによるmemset/strlenの自己呼び出し生成を確認し、
eappライブラリのfreestanding設定とGCC loop最適化設定を修正しました。
SDK build時のアーカイブ機械語チェックも追加しています。修正前の検出、修正後の非再帰化、
SDKと検証パッケージの再ビルドは確認済みです。報告者のQEMUでの再試験は未確認です。
[停止原因と再実行手順](keystone-sdk-string-recursion.md) を参照してください。

## SBI 3003の調査・実装計画

ユーザーから既存verificationsの全PASSを受領しました。
同梱OpenSBIにsealing APIとHKDF/HMAC/SHA3/署名ライブラリがないことを確認し、
Keystone SMの既存HKDF-SHA3-512とSHA3版Ed25519系署名を再利用する計画を作成しました。
OpenSBIのdispatcher・trap対応アクセス・domain判定等を優先して活用し、
鍵provider、EPM/PA検査、Eyrieの入出力処理、実QEMUでの専用verificationを段階実装します。
この変更では3003の有効化は行っていません。[詳細計画](keystone-sbi-3003-plan.md) を参照。

## SBI 3002/3003の同時実装計画

3002の既存Keystone report生成・SDK verifierとOpenSBIの再利用可能性を調査し、
共通の鍵provider、SM measurement、device→SM証明書、EPM/PAコピー、workspace、
runtime/verifier入力検査を先行する計画を作成しました。
3002のVA/PA不整合、検査前の入力コピー、report 1352 bytesに対する2048 bytesコピーも
修正対象に含めています。3003の個別計画も共通先行の順序へ更新しました。
[同時実装計画](keystone-sbi-3002-3003-joint-plan.md) を参照。この変更は文書のみです。

## Keystone関連不具合報告書の継続管理

memset/strlenの自己再帰、reportコピー、既知のSM/SDK/driver/移植問題を
[不具合台帳](keystone-bug-report.md) に整理しました。上流由来の確認範囲、修正済/未修正、
実ゲスト観測とsource確認を区別し、今後の発見・修正時に更新するルールをAGENTS.mdへ追加しました。
本変更は報告書と運用ルールであり、未修正の3002/3003コードの修正は含みません。

## SBI 3002/3003の共通基盤とQEMU開発用実装

共通provider、immutable text/rodata測定、device→SM証明書、EPM内PAコピー、
bounded workspace、KDF/署名、Eyrie/SDK/verifier検査を実装しました。
通常profileはunavailableのまま、別firmwareを `--crypto qemu-dev` で選択します。
KSB-002/003/004/006/023をコード修正、KSB-005はK3の新経路のみ修正し旧汎用helperが残ります。
新たにHKDF最大長での8 bit counter wrapをKSB-024として登録・修正しました。

実QEMU cold bootでSM measurementの独立値との一致、device→SM署名、2起動のidentity一致、
証明書改変拒否とunavailable初期化を確認しています。SDK/Eyrieと全guestパッケージはbuild済み。
Linux/enclaveからの新SBI試験と新コードの全suite回帰は未実行です。
[実装記録](keystone-sbi-3002-3003-implementation.md) と
[専用verification手順](../verifications/crypto/README.md) を参照してください。
