# SBI 2001：検証結果

検証日：2026-11-08（日本時間）。製品コードの基準版：`c6598a7bc`。

## 結果

- 実行したグループ：12
- 正常・異常系の期待を確認した`PASS`：12
- 要修正・仕様確認動作を再現した`OBSERVED`：0
- assert失敗・sanitizer検出：なし
- 実行スクリプト：exit 0（記録された観測との一致を示すもので、未修正事項の解消ではありません）

CREATEの正常系と失敗注入は期待と一致しました。PMP/コピー/hashの下位実装は模擬しているため、実hartの保護状態や実測定の正しさまで合格としたものではありません。

[ソース検証の詳細](source-review.md)に各条件と残る課題を記載しています。

## 保存した証拠

- [実行ログ](baseline/run.log)
- [実行環境とコマンド](baseline/environment.txt)
- [入力ソースのSHA-256と実関数一覧](baseline/source-manifest.json)
- [テストコード](test.c)
- [Makefile](Makefile)
- [実行スクリプト](run-tests.sh)

## 再実行

```bash
bash verifications/sbi-2001/run-tests.sh
```

現在のソースから関数を再抽出し、`build/run.log`と`build/source-manifest.json`を作ります。`baseline/`は掲載時点の記録なので自動で上書きしません。

## 未実行の範囲

QEMUゲスト・実機でのS-mode ecall/mret、Linux driverとの統合、実PMP CSR、IPI、MPRVコピー、同時実行は今回のテストに含まれていません。共通の模擬境界は[README](../README.md)を参照してください。
