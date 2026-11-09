# hello-native：元ソースとの差分・理由・評価

元は`keystone/examples/hello-native`。元ディレクトリは変更しない。

| 対象・変更 | 理由 | 判定 |
| --- | --- | --- |
| eapp/eapp_native.c：main(void)、braces、indent | 指定スタイルへ | 中立。Hello Worldの1回OCALLと終了値0は維持 |
| host/host_native.cpp→共通runner | init/run/終了値/destroyの検査を共有 | 失敗検出は改善。構成の単純さ・独立性と余分なverifier依存は後退 |
| print_stringの戻り値 | 元のprintfによる表示全体の文字数→payloadの末尾NULを除くbyte数 | 元のOCALL戻り値の意味を変えており、互換性は劣化。この例は値を利用しないが、一般化すれば影響する。未復元 |
| 出力のEnclave saidと引用符等を省略 | 共通のbyte列出力へ統一 | 表示・既存ログ解析との互換性低下。改善とは断定しない |
| free 1 MiB→2 MiB、UTM 1 MiB→256 KiB | 共通runnerへ統一 | free要求は増え、共有容量は減る。現helloはPASSだが、元より資源効率や汎用性がよいとは言えない |
| Makefile化とgp初期化entry検査 | 指定方式と現SDKの起動契約を維持 | 方式への適合。元hello-nativeのCMakeも本リポジトリでは既に専用entry指定済みであり、今回初めてその不具合を直したとはしない |

nativeアプリはSDK CRTを明示する。通常profileでの実行も確認済み。
[全体評価](../../doc/keystone-samples-change-review.md)と[検証記録](../results.md)を参照。
