# SBI 2002/2003の不具合修正と再検証

日付：2026-11-08（日本時間）  
製品コードの修正コミット：`cc885e6ad`

## 対象

当時発見した次の5件を修正した。旧模擬試験は削除し、現在は
[実QEMU統合試験](../verifications/README.md)で連携動作を確認する。

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

この異常状態から自動的に再試行するAPIは追加していない。PMP状態を確認できない場合に、推測で元のFRESH/STOPPEDへ戻すこともしない。現在の資源再利用確認は、実enclaveのCREATE/DESTROYを含む統合試験で行う。

## RUN：PMP設定を先に完了してからcontextを交換する

修正前はhostレジスタやCSRを交換した後でPMPを設定し、戻り値を無視していた。修正後は次の順序となる。

```text
FRESH → RUNNING、thread数を予約
 → PMP/HSM状態確認
 → OS領域の権限変更
 → enclave領域の権限変更
 → 成功時だけレジスタ・CSR・trap vector・CPU contextを切替
```

途中のPMP設定が失敗した場合は、enclave領域の権限を取り消してOS領域を復旧する。復旧できればstateをFRESH、thread数を元へ戻し、変更していないhost contextへ`PMP_FAILURE`を返す。復旧後の再RUNは実QEMU試験では未確認。

取り消しやOS権限復旧まで失敗した場合は`sbi_hart_hang()`で停止する。不明なPMP状態でS-modeへ成功・エラー復帰しない。これは通常のRUNで停止する変更ではなく、PMPとその復旧の両方が成立しない異常時の処理である。

## 共通関数の影響

同じ関数を使うRESUME 2005も失敗を検出し、元のstateとthread数を復元する。EIDの全幅検査もRESUMEに適用した。

hostへ戻る共通経路にもPMP設定の戻り値検査を追加した。復旧できない権限状態でhost contextへ戻らず停止する。現在の統合試験ではOCALLによるSTOP/RESUME/EXITを実行する。
実タイマー中断やPMPエラー注入の網羅は未確認。

## 現在の検証方法

旧ホスト模擬試験のPASS表、baseline/after-fixログ、実行コードは削除した。
現在の試験と実行結果は [QEMU統合試験](../verifications/README.md) を参照。
正常系では実CREATE/RUN/STOP/RESUME/EXIT/DESTROYを通し、
異常系では実ecallで無効ID等の応答を検査する。
旧試験のグループ数や失敗注入結果を現行のゲスト試験の結果に流用しない。

## 既存QEMU環境への更新

ホストでファームウェアとSDKを更新して、ゲスト試験をビルドする。

```bash
git pull
bash scripts/build-install-k3.sh --only firmware --prefix "$PWD/install/keystone"
bash scripts/build-install-keystone.sh --only sdk
make -C verifications guest
```

ゲストを再起動して新しいファームウェアを使う。全試験には対応カーネル用の
検証モジュールも必要。[手順](../verifications/README.md)に従う。
現在のQEMU試験でも全hart・実タイマー・競合・全異常系は未網羅。
