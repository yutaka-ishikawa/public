# K&R整形後の検証

日付：2026-11-08（日本時間）。今回の整形前の基準コミット：`ff8a76076`。

承認されたK&Rスタイルに合わせ、`common/harness.c`と各SBIの`test.c`を整形した。関数の開始波括弧を改行し、制御文の開始波括弧は同じ行に置き、1段4文字・タブ幅8（1段目は空白4文字、2段目はタブ1文字）、1文1行、ブロック冒頭の変数宣言とした。副作用のある初期化は宣言から分離し、元の実行位置を維持した。

Python抽出スクリプトに埋め込まれた補助C定義も整形した。Python・shell・Makefileはそれぞれの言語の構文に従う。製品コードから抽出する関数本文は、検証のため原文どおり保持する。

## 結果

| SBI | グループ数 | 結果 |
| --- | --- | --- |
| 2001 | 12 | PASS |
| 2002 | 15 | PASS |
| 2003 | 16 | PASS |
| 2004 | 5 | PASS |
| 合計 | 48 | PASS |

`bash verifications/run-all.sh`をAddressSanitizer／UndefinedBehaviorSanitizer有効で実行した。各SBIの`build/run.log`は、以前の`after-fix/run.log`とバイト単位で一致した。製品ソースの入力ハッシュも以前の記録と一致し、製品コードは変更していない。

clang-format 23.1.5で整形し、5個のCファイルについて`--dry-run --Werror`による再整形不要の確認も行った。機能検証の模擬境界は[共通README](../README.md)のとおりで、ゲスト・実機での追加実行ではない。

整形後の入力ハッシュ・ログハッシュ・結果は[results.json](results.json)に記録した。過去の`baseline/`と`after-fix/`は変更していないため、旧test-manifestのハッシュは整形前のコードを示す。

## 今後のスタイル

リポジトリの[AGENTS.md](../../AGENTS.md)と[.clang-format](../../.clang-format)に方針を保存した。C/C++の新規作成・変更にも同じスタイルを使う。

```bash
clang-format -i verifications/common/harness.c verifications/sbi-*/test.c
bash verifications/run-all.sh
```

前回の1段8文字の整形記録は[results-8column.json](results-8column.json)に保持し、現在の設定で再検証した結果を`results.json`に記録しています。
