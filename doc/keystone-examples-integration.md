# Keystone examplesのsamples統合と互換性差分

2026-11-09。`keystone/examples/`は変更せず、Makefile版を`samples/`に置く。
helloは既存helloworld名を維持し、hello-native、attestation、testsを追加した。
SDK/EyrieのビルドにはCMakeを使うが、アプリは全てMakefileからコンパイルする。

## 元のattestationをそのまま使えるか

現在のK3 OpenSBI+SMで正しい検証結果を得るには変更が必要。
元版が必ず同じ場所で停止するという実行結果ではなく、以下は具体的な契約の不一致。
通常profileでは3002/3003そのものがunavailableなので、成功経路にはqemu-devが必要。

| 元ソース | 元の処理 | 現在必要な処理／samples側 |
| --- | --- | --- |
| `keystone/examples/attestation/host/verifier.cpp`の`verify_report()` | `_sanctum_dev_public_key`を固定で使用 | 起動identityの外部公開metadataからdevice-public-key.binを読む |
| 同`compute_expected_sm_hash()` | fwファイルを0x1ff000 bytesにゼロ埋めしてhash | 当該ビルドで生成したsm-hash.binを独立した期待値として使用 |
| `attestation/CMakeLists.txt` | fw_jump.bin / fw_payload.binを添付し--sm-binで渡す | fw_dynamicを含む現在のboot構成に対応する公開metadataを外から指定 |
| 同`target_link_libraries()` | -nostdlib -staticだけでnativeをリンク | -u,keystone_eapp_startと-e,keystone_eapp_startでSDK CRTを選択 |
| `attestation/eapp/attestor.c` | nonce上限2048、attestのstatusを無視、2048-byte OCALL | nonce上限1024、checked APIの成功確認、1352-byte reportだけをOCALL |
| `host/verifier.cpp`等の失敗処理 | 署名/hash不一致を表示するだけの経路がある | SDK・report・nonce・終了値・DESTROYの失敗を非0で返す |

### 「旧エントリポイント」の正確な意味

元attestor.cにもmainはある。古い名前の関数だから動かないという意味ではない。
SDKの`keystone/sdk/src/app/crt.S`はkeystone_eapp_startでgpを初期化してmainを呼ぶが、
static archiveからこのobjectを取り出すには明示的な参照が必要。
元attestationのlink指定はそれを要求せず、ELF entryも指定しない。
したがって正しいC ABIで入ることが保証されず、gp相対命令があれば障害になる。
統合版のMakefileは専用entryの取り込みと指定を行い、実ELFを既存チェックで検査する。
libc版helloは通常のlibc CRTを使い、-nostdlibは指定しない。

### 鍵とSM hash

現在の`keystone/sm/src/security/security.c`は明示的qemu-dev providerを持ち、
`KEYSTONE-SM-IDENTITY-v1`等の現在の導出契約を使う。
元の`verifier/test_dev_key.h`の固定鍵と同じidentityであるという前提は置かない。
外部device/SM pinsと、ローカルのloader/runtime/eappから計算するenclave hashを検証する。
パッケージが自己申告する公開鍵をそのまま信頼するものではない。
開発用identityの成功はJupiter2実機の鍵供給・hardware endorsementの完成を意味しない。

### reportコピー

正規reportはshared/crypto_abi.hの1352 bytesであり、2048 bytesを送る契約ではない。
元サンプルの2048-byte buffer内でreport外の領域を送ることと、以前修正した
Eyrieの2048-byte書込み（KSB-002）は別の層の問題。同じサイズ差が両方にある。
統合版は共通型とchecked APIを使い、失敗時には送信しない。

## testsと共通runner

C/assembly例を現SDKのCRTへ揃え、SDK tiny-malloc用arenaを提供した。
retvalはstack=12345、loop=54321、malloc=11411、long-nop=12345、
fibonacci=14930352、untrusted=13、attestation/sealing=0を明示する。
fib-benchだけは計測値なので固定値と比較しない。

上流CMakeのretval探索先はtest-stack/retval等だが実際はstack/retval等。
上流runnerも失敗を終了statusへ反映しない経路があるため、そのまま転用しない。
統合版は共通runnerとset -eのscriptで失敗を伝える。
untrustedのcount未初期化もコピー側で修正した。

fib-benchのrdcycleは現在のLinuxから継承したcounter許可では不正命令を観測した。
SMの権限設定を広げず、samples側でrdtimeへ変更した。CPU cycle数とは表記しない。
KSB-036〜039に調査・修正の区別を記録する。

## ビルド・配置

`build-install-keystone.sh --only sdk`はSDK → build-sample-runtimes.sh → samples/Makefileの順。
Eyrieの2 profileは.build-keystone/sample-runtimes/{hello,hello-native}/で作り、
従来の.build-keystone/examples/{hello,hello-native}/へruntimeと互換packageを配置する。
keystone/examplesのCMakeによるアプリビルドは通常workflowから外した。
rootのmake samplesとinstall-samplesも統合先を使う。上流ファイルは保持する。

[使い方](../samples/README.md)、[検証結果](../samples/results.md)を参照。
