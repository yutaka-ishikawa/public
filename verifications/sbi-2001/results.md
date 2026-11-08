# SBI 2001：修正後の検証結果

検証日：2026-11-08（日本時間）。製品コードの修正版：`cc885e6ad`。

## 再検証結果

- 実行：12グループ、すべてPASS。
- 未修正動作の再現を表すOBSERVED：0。
- assert失敗、AddressSanitizer／UndefinedBehaviorSanitizer検出：なし。
- 実行スクリプト：exit 0。

CREATEの既存12グループは引き続きPASSです。本体の共有ソース変更に対する回帰確認であり、2001の未検証項目まで解消したものではありません。

詳細は[ソース検証](source-review.md)と[修正全体の説明](../../doc/keystone-sbi-2002-2003-fixes.md)を参照してください。

## 修正後の証拠

- [実行ログ](after-fix/run.log)
- [実行環境・コマンド](after-fix/environment.txt)
- [製品ソースのSHA-256と実関数一覧](after-fix/source-manifest.json)
- [使用したテストコード等のSHA-256](after-fix/test-manifest.json)
- [テストコード](test.c)、[Makefile](Makefile)、[実行スクリプト](run-tests.sh)

## 初回の記録

初回（製品コード`c6598a7bc`）の[ログ](baseline/run.log)、[環境](baseline/environment.txt)、[ソースハッシュ](baseline/source-manifest.json)は`baseline/`に保持しています。初回のOBSERVEDは過去の不具合再現であり、現在の結果ではありません。

## 再実行

```bash
bash verifications/sbi-2001/run-tests.sh
```

現在のソースから再生成して`build/run.log`等に出力します。保存した`baseline/`と`after-fix/`は自動では上書きしません。

## 範囲

実C関数を64ビットLinuxホスト上で実行しました。PMP・CSR・IPI等は模擬境界です。QEMUゲストや実機でのCPU命令としてのecall/mret、Linux driverとの統合、全hartの実動作は未検証です。[共通README](../README.md)に境界を記載しています。
