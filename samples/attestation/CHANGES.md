# attestation：元ソースとの差分・理由・評価

比較元は本リポジトリの`keystone/examples/attestation/`。最新上流全版に対する判定ではない。
元ディレクトリは変更していない。統合前の基準commitは`3054460c4`。
判定は「現K3構成での正当性」「元の機能・実行条件との互換性」「構造」を分ける。
単に動いたことを、全ての点で元より優れている根拠にはしない。

## enclave：eapp/attestor.c

元：[attestor.c](../../keystone/examples/attestation/eapp/attestor.c)。
現在：[attestor.c](eapp/attestor.c)。

| 変更 | 理由 | 判定・互換性 |
| --- | --- | --- |
| main(void)、EAPP_ENTRY、K&R配置 | 現SDKのnative入口と指定スタイルへ統一 | 型・書式は中立。起動は下記CRTへの適合。旧対応環境全体の不良とはしない |
| nonce領域2048→1024 bytes | 現SMのattestation data上限は1024 | 仕様適合による改善。1024超は元でも現SMへ有効に渡せない |
| 超過nonceの切詰め→拒否 | challengeを黙って変えない | エラーの明確化は改善。ただし切詰めを許容した旧挙動とは異なる |
| 空nonceを拒否 | このサンプルを非空challenge方式に限定 | 用途上の制約追加。SM API自体のゼロ長data対応を改善したわけではない |
| OCALL・共有コピー・attestationの戻り値を検査 | 失敗を成功として続行しない | 改善。失敗時の挙動は意図的に変更 |
| char buffer[2048]→struct keystone_report | 正規1352-byte ABIを使用 | 改善。report外696 bytesの転送を除去（KSB-040） |
| checked API成功時だけsizeof reportをOCALL | 容量不足・サービス無効時の誤転送を防ぐ | 改善。無効providerでは非0終了する |
| エラー時も常に0→1/2/3で終了 | host/scriptに失敗を伝える | 改善。既存の「終了値は常に0」という扱いとの互換性は失うが、失敗判定に必要 |
| 1〜10000のループと5000/10000の値OCALL | 初回統合で不要と判断し削除したが、負荷と往復経路が減っていた | 削除は劣化であり、今回復元した（KSB-041） |

### 復元した処理順序

```text
OCALL 4：challengeの共有領域情報を受領
  → i=1..10000のループ
      → i=5000、10000でOCALL 2
  → 共有領域からnonceをコピー
  → attestationを1回実行
  → OCALL 3：正規reportを返す
```

元と同じく、nonceのコピーを2回の値OCALLの後に置く。
従って追加のhost復帰/RESUMEだけでなく、その後も共有nonceを正しく取得できる経路を保つ。
OCALL失敗時の検査は維持し、値OCALL失敗時は3を返す。

このループに時計の取得・時間集計はない。2回のOCALLと一定のソース上の計算処理はあるが、
コンパイラ最適化後の命令数・一定時間の負荷を保証するものではない。
attestation自体を10000回呼ぶ負荷試験でもなく、report生成は最後の1回。
作者の意図はコードだけでは断定しない。初回統合で「表示用」とだけ説明したのは不十分だった。

## host：分割実装から共通runnerへ

元は`host/attestor-runner.cpp`、`host/host.cpp/.h`、`host/verifier.cpp/.h`。
現在の`host/attestor-runner.cpp`は入口のincludeだけで、実処理は
[common/runner.hpp](../common/runner.hpp)にある。

| 変更 | 理由 | 判定・互換性 |
| --- | --- | --- |
| random()の文字列nonce→getrandom()の32 bytesをhex化 | challengeの予測困難性を高める | 改善。nonceは64文字＋NULの65 bytesへ変化し、元と同じ入力列ではない |
| 固定_sanctum_dev_public_key→外部device-public-key.bin | 現qemu-dev identityに対応・鍵更新を可能にする | 互換性・運用性の改善。ファイルの真正性と保護が前提で、安全性が自動的に上がるわけではない |
| firmwareを0x1ff000へpaddingしてSM hash計算→sm-hash.bin | 現在のfirmware/providerの測定契約へ対応 | 現構成への適合。期待hashを直接再計算する旧方式とは異なり、外部metadataを信頼する必要がある |
| ローカルenclave測定 | 期待したloader/runtime/eappを検証する | 元と同じSDK Enclave::measure()を継続使用。新しい暗号機能ではない |
| fromBytes→fromBytesChecked、report・nonce・署名・hash検査の失敗を非0へ | 不正長・検証失敗を成功扱いしない | 改善。誤pinによる拒否をQEMUで確認済み |
| SDK init/run/終了値/destroyを検査 | 実行失敗を自動試験へ伝える | 改善 |
| --utm-size、--freemem-sizeを削除 | 共通runnerを簡略化 | 機能劣化。統合に必須の削除ではなく、復元または明示的設定化が望ましい。今回は未復元 |
| --sm-binを削除しKEYSTONE_PUBLIC_DIRを使う | 旧firmware padding方式から現方式へ変更 | 適合のためのインターフェイス変更。旧コマンドラインとは互換性なし |
| free 48 MiB→2 MiB、UTM 2 MiB→256 KiB | 小規模例の資源要求を下げる | 要求量は減るが汎用性・大きなpayloadの余地は減る。性能改善は未測定 |
| Host/SharedBuffer/Verifierの分割を共通headerへ集約 | サンプル間の重複を減らす | 重複削減は利点だが、責務分離・独立した理解・拡張性は後退。全面的な改善とは判定しない |
| 実行ファイル名attestorで検証動作を選択 | 共通runnerの実装簡略化 | 設計上の劣化。リネームで挙動が変わる。明示的なモード指定が望ましい。未変更 |
| 値の表示を共通形式へ統一 | runner共有 | 表示上の変更。旧ログ解析との互換性は低下。表示形式自体を優劣の根拠にしない |

外部pinはパッケージ内の自己申告鍵を信頼する方式ではない。ただし、qemu-devは開発用identityであり、
これらの変更やPASSは実機のroot of trust・hardware endorsementを強化した証拠ではない。

## ビルド

| 変更 | 理由 | 判定 |
| --- | --- | --- |
| アプリをCMake→GNU Makeへ | ユーザーの開発方法へ統一 | 要求への適合。CMakeより常に優れているという判定ではない |
| -u/-eでkeystone_eapp_startを選択 | SDK CRTでgpを初期化してmainへ入る | 現toolchainでのABI正当性改善。元CMakeはその選択を明示していなかった |
| prepared native Eyrieを再利用 | アプリの反復ビルドを単純化 | 利便性の利点。runtimeを変える場合は別途ビルドが必要 |
| fw_jump等をpackageへ添付しない | 現期待値は外部metadata | 旧package内容とは非互換。boot済みfirmwareとの対応確認が前提 |

検証結果は[samples/results.md](../results.md)と、今回の[ループ復元記録](../evidence/loop-restored.json)。
他のサンプルの比較は[全体評価](../../doc/keystone-samples-change-review.md)を参照。
