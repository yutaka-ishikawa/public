# SBI 2002/2003の不具合修正と再検証

日付：2026-11-08（日本時間）  
製品コードの修正コミット：`cc885e6ad`

## 対象

[初回検証](../verifications/README.md)で再現した次の5件を修正した。

| SBI | 初回の問題 | 修正後 |
| --- | --- | --- |
| 2002 DESTROY | DESTROYING/ALLOCATEDも破棄対象になる。 | FRESH/STOPPEDだけをlock内で受け付ける。 |
| 2002 DESTROY | global PMP解除が失敗しても解放・成功復帰する。 | エラーを返し、EIDと未解放の資源をDESTROYING状態で保持する。 |
| 2002 DESTROY | 64ビットEIDを32ビットへ切り詰める。 | 型変換前にEID全体の範囲を検査する。 |
| 2003 RUN | local PMP設定が失敗してもcontext切替を完了する。 | PMP設定をcontext交換前へ移動し、失敗時は権限・state・thread予約を復旧してエラーを返す。 |
| 2003 RUN | 64ビットEIDを32ビットへ切り詰める。 | 型変換前にEID全体の範囲を検査する。 |

新しいSBI番号やreport形式は追加していない。成功時のCREATE/RUN等のABIは維持する。範囲外EIDへのエラーは、helperで`INVALID_ID=100001`として返すようになった。

## DESTROY：単一の破棄処理が資源を所有する

状態条件を列挙式にし、状態の判定とDESTROYINGへの変更を同じlock内で行う。先に破棄中のenclaveをもう一度破棄する要求は拒否される。

資源の解放は、全private領域の消去・global PMP解除が成功した後に行う。途中の領域で解除に失敗した場合も、それまでに解除できた領域のdescriptorを解放しない。エラーは`PMP_FAILURE=100003`で、driverへ成功を返して物理ページを再利用させることを避ける。

PMP descriptor解放自体のエラーも確認する。既に解放できたdescriptorを巻き戻す処理はないが、全private領域の消去・global解除が成功した後の段階である。EIDと残る資源はDESTROYINGのまま保持し、再入・RUN/RESUMEを拒否する。

この異常状態から自動的に再試行するAPIは追加していない。PMP状態を確認できない場合に、推測で元のFRESH/STOPPEDへ戻すこともしない。通常時の資源再利用は100回のCREATE/DESTROY試験で確認した。

## RUN：PMP設定を先に完了してからcontextを交換する

修正前はhostレジスタやCSRを交換した後でPMPを設定し、戻り値を無視していた。修正後は次の順序となる。

```text
FRESH → RUNNING、thread数を予約
 → PMP/HSM状態確認
 → OS領域の権限変更
 → enclave領域の権限変更
 → 成功時だけレジスタ・CSR・trap vector・CPU contextを切替
```

途中のPMP設定が失敗した場合は、enclave領域の権限を取り消してOS領域を復旧する。復旧できればstateをFRESH、thread数を元へ戻し、変更していないhost contextへ`PMP_FAILURE`を返す。復旧後の再RUNもテストした。

取り消しやOS権限復旧まで失敗した場合は`sbi_hart_hang()`で停止する。不明なPMP状態でS-modeへ成功・エラー復帰しない。これは通常のRUNで停止する変更ではなく、PMPとその復旧の両方が成立しない異常時の処理である。

## 共通関数の影響

同じ関数を使うRESUME 2005も失敗を検出し、元のstateとthread数を復元する。EIDの全幅検査もRESUMEに適用した。

hostへ戻る共通経路にもPMP設定の戻り値検査を追加した。復旧できない権限状態でhost contextへ戻らず停止する。正常なRUN→timer STOP→RESUME→EXITと、host復帰時のPMPエラーを回帰試験に含めた。

## 再検証結果

| 対象 | グループ数 | 結果 |
| --- | --- | --- |
| 2001 CREATE | 12 | PASS |
| 2002 DESTROY | 15 | PASS |
| 2003 RUNと共有経路 | 16 | PASS |
| 2004 未定義ID | 5 | PASS |
| 合計 | 48 | すべてPASS、OBSERVED 0 |

旧5件のOBSERVEDは、修正後の期待動作を確認するテストに変更した。さらに、callback内再入、複数private領域の後段解除失敗、descriptor解放失敗、rollback不能、復旧後のRUN再試行、共通RESUME/STOP/EXITを追加した。

AddressSanitizerとUndefinedBehaviorSanitizerを有効にし、assert失敗・sanitizer検出がないことを確認した。既存のSM trap/戻り値、システムreset、タイマーポリシーのテスト計5件も成功した。

また、修正したSMをOpenSBIへ組み込み、ファームウェアのコンパイル・リンクに成功した。今回のトレース有効ビルドの`fw_dynamic.bin`のSHA-256は以下。

```text
9b5081d5a2025dd949487d0277671bc158e78007adb3c30a998c8d60280368e4
```

トレースにはビルド日時が入るため、別の時刻にビルドしたファイルのハッシュ一致を要求するものではない。

## 証拠と実行方法

各SBIの`baseline/`に初回記録を残し、`after-fix/`へ修正後のログ・環境・製品ソースとテストのSHA-256を保存した。

- [2002：修正後のソース検証](../verifications/sbi-2002/source-review.md)
- [2002：結果](../verifications/sbi-2002/results.md)
- [2003：修正後のソース検証](../verifications/sbi-2003/source-review.md)
- [2003：結果](../verifications/sbi-2003/results.md)

```bash
bash verifications/run-all.sh
```

## 既存QEMU環境への更新

古いQEMUプロセスを終了し、ホスト側でファームウェアだけを再ビルドする。

```bash
git pull
bash scripts/build-install-k3.sh --only firmware --prefix "$PWD/install/keystone"
bash scripts/run-k3.sh
```

SDK・example・カーネル・rootfsの再ビルドは不要。独自のインストール先を使う場合はビルドと起動で同じ`--prefix`を指定する。

## 検証の限界

テストは実ソース関数を取り込んだホスト上の試験である。PMP CSR、IPI、CSRアクセス、実タイマー配送、lockの同時性は模擬している。停止分岐もテスト用の捕捉処理で確認し、物理hartを停止していない。

QEMUゲストでのhello再実行、実機での全hart同期、driverと物理ページ管理までの統合確認は今回未実行である。ファームウェアのビルド成功を、実機対応やenclave隔離の証明とはしていない。2001のrollback等、今回対象外の残る確認事項もそのまま区別している。
