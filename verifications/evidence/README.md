# ビルド確認記録

これらは開発環境でのビルドログであり、ゲスト実行PASSの証拠ではない。

- `cross-build.log`：2 MiB/256 MiB設定の実enclaveパッケージと、実SBI試験runner。
- `kernel-objects.log`：PMP probeと実SBI検証モジュールのRISC-Vオブジェクト生成。
- `package-check.log`：自己展開パッケージの整合性検査。

対応する完全なModule.symversと稼働中K3システムゲストがないため、
新しい全試験の `.ko` リンク・ロード・実行結果は未確認。
過去の模擬試験ログは削除済み。

TOR読み戻し検証の修正後のファームウェアビルド記録は
`tor-firmware-build.log`、対応する入力・生成物ハッシュは`tor-firmware.json`。
これも実ゲストの256 MiB試験のPASSを示すものではない。
