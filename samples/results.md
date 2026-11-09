# samples統合の検証結果

2026-11-09（日本時間）。今回の作業ツリーをコミットする前に、K3の実system QEMUと
Linux/driver/OpenSBI/SM/Eyrieを使って確認した。実装commitは[8d8fd5b9c](https://github.com/yutaka-ishikawa/public/commit/8d8fd5b9c8e52f54169064548dad074e20ac5186)。
[実行manifest・入力hash](evidence/results.json)と同ディレクトリのassertion logを参照。

| 対象 | 確認内容 | 結果 |
| --- | --- | --- |
| ホストclean build | 4 packageとtestsの9アプリをGNU Makeでビルド | 成功 |
| rootのmake samples | SDK → 2 Eyrie profile → samples/Makefile、互換packageパス | 成功 |
| make -C samples check | 4 packageのsnapshot/checksum/展開検査 | 成功（guest試験とは別） |
| install-samples相当 | 共通runner/scriptと全packageを別prefixへ配置 | 成功 |
| qemu-dev / Sstc on | hello、native、attestor、9 testsの計12アプリ | 全PASS |
| qemu-dev / Sstc off | 同上 | 全PASS |
| unavailable / Sstc on/off、--basic | 各9アプリ、暗号3例を明示SKIP | 選択した対象は全PASS |
| unavailableで完全実行 | attestorが非0終了、suiteを成功扱いしない | 期待どおり拒否 |
| stackへ誤った期待値0を指定 | SDK=0、実終了12345に対してrunnerは非0 | 期待どおり拒否 |
| 公開device pinを32-byteの0へ変更 | 実qemu-devのreportをhostが拒否し非0終了 | 期待どおり拒否 |

attestorは署名・SM/enclave測定値・nonce一致を確認した。sealing例は鍵の取得を確認し、
鍵そのものはログへ出していない。fib-benchの戻り値はtimebase tick数で可変。
元のkeystone/examplesに変更がないこともgit diffで確認した。

## 再現方法

正常系は[全体README](README.md)の`python3 samples/run-qemu.py`と`--basic`。
無効providerの完全実行は`python3 samples/run-qemu.py --crypto unavailable --sstc on`で
非0になることを確認する。これは正常系PASSではなく、期待した拒否の試験。

戻り値不一致は、ゲストでtests packageを展開・driverロード後に
`./test-runner test-stack eyrie-rt loader.bin 0`を実行する。
誤pinは、公開metadataのコピーにあるdevice-public-key.binだけを32 bytesの0に置き換え、
そのディレクトリをKEYSTONE_PUBLIC_DIRへ指定してattestorを実行する。
秘密鍵やseedの変更・配布は必要ない。

## 検証の限界

新しいSDK/Eyrie workflowは専用の`.build-keystone/samples-integration`で検証した。
共有クラウドのcross compilerには専用sysrootを指定した。通常apt環境では標準toolchainを使う。
基本・暗号サンプルの動作確認であり、実機、認証付き暗号による永続保存・再起動後復元、
全SBI verificationの今回の再実行を意味しない。
`data-sealing_with_output.c`は参照用として保持するがビルド・実行対象外。
SDKのhost資源寿命（KSB-035）はこの統合では変更していない。

試験後、共通runnerの2行は8 spacesからtabへインデントを整え、assembly参照ファイルの行末空白も除去した。
処理と行番号は変更しておらず、manifestには試験時と最終版の両SHAを記録した。

## attestorループ復元後の再試験（2026-11-09）

初回統合で削除した1〜10000のループと値OCALLをnonceコピー前に復元した。
modified attestorを再ビルドし、snapshot/checksum検査後、実QEMUで全12アプリを再実行。
Sstc on/offの両方で全PASS、attestorの値OCALLは5000と10000の正確な2回で、
その後のnonce・署名・enclave/SM pin検証もPASSした。
[証跡](evidence/loop-restored.json)と[各ソースの評価](../doc/keystone-samples-change-review.md)を参照。
この結果はループの実行時間や機械語命令数を測定したものではない。
