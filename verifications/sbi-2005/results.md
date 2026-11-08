# SBI 2005：検証結果

日付：2026-11-08（日本時間）。製品コードの基準版：`705e9dbd0`。

- RESUME専用17グループ：すべてPASS。
- 未修正動作の再現（OBSERVED）：0。
- AddressSanitizer／UndefinedBehaviorSanitizer：検出なし。
- 既存SBI・edgecallを含む全体：77グループPASS。
- 実施範囲：ホスト上で実ソース関数を実行。実PMP/CSR/割り込み配送は模擬。
- ゲスト・実機での専用試験：未実行。

[ソース検証](source-review.md)に条件、復元レジスタ、PCとa0の扱い、復旧処理、限界を記載した。今回の追加では製品コードを変更していない。

## 実行

```bash
bash verifications/sbi-2005/run-tests.sh
# 全試験
bash verifications/run-all.sh
```

[Makefile](Makefile)から`make -C verifications/sbi-2005 test`でも実行できる。K&R、インデント4文字・タブ幅8、単文にも波括弧を付ける方針に従っている。

## 保存した証拠

- [実行ログ](baseline/run.log)
- [実行環境](baseline/environment.txt)
- [製品ソースハッシュと抽出関数](baseline/source-manifest.json)
- [テストコード等のハッシュ](baseline/test-manifest.json)
- [機械可読結果](results.json)
- [テストコード](test.c)

再実行すると現在のソースから関数を抽出して`build/`へ出力する。保存した`baseline/`は上書きしない。

## QEMUゲストで確認する場合

上記17グループはCロジックの単体試験であり、実ecall/mretの試験ではありません。[QEMUゲストでの実行手順](../../doc/keystone-verification-methods.md)に従い、edgecallのguest runnerで実際のSTOP/RESUME経路を確認してください。このguest試験は17グループ全部や実タイマー中断の網羅ではありません。
