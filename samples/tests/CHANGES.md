# tests：元ソースとの差分・理由・評価

元は`keystone/examples/tests`。元ディレクトリは保持する。

## 9例の変更

| 例・主なソース | 変更と理由 | 判定 |
| --- | --- | --- |
| stack/stack.S | eapp_entry→global main、SDK CRTから呼ぶ。stack storeと終了12345は同じ | 現link方式への適合。計算本体は中立。起動列・ELFは元と同一ではない |
| loop/loop.s | 同じ入口変更。10000000からの減算と終了54321は維持 | 適合。本体の負荷は削除していない。機械語や実時間の同一性は保証しない |
| malloc/malloc.c | eapp_entry→main、型・書式を統一。11411のstore/loadは維持 | 適合。malloc試験としての基本動作は同じ。malloc失敗処理の全面改善ではない |
| long-nop/long-nop.S | checked-in NOP列と終了12345を維持し入口変更。旧CMakeの追加生成add_long.Sはリンクしない | 適合とビルド簡略化。ただし総ELF容量・ページ負荷条件は元と同一でなく、その同等性は後退 |
| fibonacci/fibonacci.c | 関数本体をbraces付きに整形しmainへ。入力35と終了14930352は維持 | 適合・書式変更。本体機能は中立 |
| fib-bench/fib-bench.c | rdcycle→rdtime、read_cycles→read_ticks。計算結果をvolatileへ保存 | 現counter許可への適合と計算削除防止は改善。CPU cycles測定という元の機能は失い、単位互換性は劣化 |
| attestation/attestation.c | 正規1352-byte report、checked API、失敗時非0、成功時だけOCALL。nonce=nonceの5 bytesは維持 | 正当性改善。常に0終了する旧挙動からは変更 |
| untrusted/untrusted.c | count=0初期化、malloc/copy検査、コピー後free、size_tのindex | 未初期化値と失敗処理は改善。加えた4096-byte上限は制約であり、大きな有効文字列を扱う汎用性は低下 |
| data-sealing/data-sealing.c | main(void)と書式。identifierの鍵取得、statusに応じた0/-1は維持 | 適合・中立。新しい保存/復元や暗号化機能を追加したわけではない |

fib-benchのrdcycleは現guestでillegal instructionを観測した（KSB-038）。
SMのcounter権限を広げずrdtimeで実行可能にしたが、戻り値はtimebase ticksでありCPU cyclesではない。
hostへの中断・スケジューリングの影響も含み得る。元との性能比較にそのまま使えない。volatileへの結果保存も、特定の再帰回数・命令列や
元と同じ計算負荷を保証するものではない。

## heapとassembly生成

- 旧app.ldsはビルドに使わず、heap.cで__malloc_startと64 KiBの.bssを用意する。
  旧scriptのmalloc zoneは4 KiBだったため、配置・容量条件が異なる。
  link契約への適合だが、比較条件は変わる。この予約だけでallocatorの厳密な上限や大容量耐性を保証しない。
- generate_func.shはyes|headのpipelineからPython生成へ変更し、mainを出す。
  生成の安定性と現入口への適合が目的。既定buildはchecked-in long-nop.Sを使う。
- data-sealing_with_output.cは参照用。元のCMakeの9対象にも含まれず、現在も既定build/run対象外。
  今回それを実行対象から削除したわけではない。

## runner・実行script

| 変更 | 理由 | 判定 |
| --- | --- | --- |
| test-runner.cpp/edge_wrapper.cppの構成→共通runner | SDK、OCALL、report、終了、cleanupの検査を共有 | 失敗検出は改善。責務分離・独立性は後退 |
| init/runを未検査、retval不一致表示後も0→非0終了 | 試験失敗を呼出側へ伝える | 改善。誤った期待値0でstackを動かし、実値12345に対して非0になることを確認済み |
| retvalの探索→run-tests.shで明示 | 元CMakeはtest-stack/retval等を探し、実際のstack/retval等と不一致 | 改善。既定値が実際に検査される |
| --retval→位置引数EXPECTED | script単純化 | 検査は改善したがCLI互換性は低下 |
| --utm-size、--freemem-size、--time、--load-onlyを削除 | 共通runner簡略化 | 機能劣化。統合に不可欠ではない。今回は文書化のみで未復元 |
| free 48 MiB→2 MiB、UTM 2 MiB→256 KiB | 小規模例へ資源を合わせる | 小さく動く利点はあるが余裕・負荷条件を減らす。性能向上とは判定しない |
| 固定dev鍵→外部pins、checked report検証 | 現identity・ABIへ適合 | 正当性改善と運用変更。外部metadataの真正性が前提 |
| --basicで暗号2例を明示SKIP | 無効providerでも他の7例を実行する | 運用上の追加機能。完全試験PASSと同一ではない |
| set -eで最初の失敗時に停止 | 失敗を隠さず終了する | 失敗伝達は改善。一度に全失敗を収集する用途には制約 |

旧usageに表示される--utm-ptrはoption tableに実装されておらず、今回失った実装済み機能には数えない。
上記の--timeはhost runnerの計測option、fib-benchのrdcycleとは別の変更。
K&R書式、braces、prototypeの整理は、単独では実行性能や安全性の向上を意味しない。
[全体評価](../../doc/keystone-samples-change-review.md)、[結果](../results.md)を参照。
