# edgecall検証結果

検証日：2026-11-08（日本時間）。製品コードの基準版：`35aed28b2`。

| 対象 | 結果 |
| --- | --- |
| 実SDK＋実runtime関数によるデータ転送 | 10グループPASS |
| 実SMのedgecall STOP/RESUME | 2グループPASS（PMP APIは模擬） |
| 既存SBIを含むホスト試験全体 | 60グループPASS |
| AddressSanitizer／UndefinedBehaviorSanitizer | 検出なし |
| guest eapp/runner | RISC-Vクロスビルド成功。`-Wall -Wextra -Werror`。 |
| guestパッケージ | 生成と整合性チェック成功 |
| runner引数チェック | QEMU user modeでusage表示・exit 2を確認 |
| カーネルprobe | RISC-Vオブジェクトのコンパイル成功 |
| probeの`.ko`リンク・ロード | 未確認。対応する完全なModule.symversがない。 |
| ゲストでのOCALL往復・実PMP参照拒否 | **未実行**。起動用Imageと動作中ゲストがない。 |

専用ホスト試験は、共有領域を異なる二つのVAへマップして実行した。private入力からのコピーと変換した戻り値、NULを含むバイナリ、境界サイズ、長短return、不正offset、privateポインタの拒否を検査した。

参照拒否のゲスト試験は実装済みだが、ホスト試験だけではPMPを検証できない。defaultのguest runnerはprobeを使えなければ失敗し、明示的な`--data-only`の場合だけPMP試験をSKIPする。

- [実行ログ](evidence/host-run.log)
- [SDK/runtimeソースハッシュ](evidence/edge-source-manifest.json)
- [SMソースハッシュ](evidence/sm-source-manifest.json)
- [カーネルオブジェクトのビルドログ](evidence/kernel-object.log)
- [パッケージ検査](evidence/package-check.log)
- [試験コード・生成物のハッシュと機械可読結果](results.json)
- [実行手順と判定範囲](README.md)
