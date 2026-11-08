# SBI 2003 RUN：修正後のソース検証

修正対象：`cc885e6ad`。修正前の記録は[旧検証](source-review-before-fix.md)、実行結果は[results.md](results.md)。

## R1：PMP失敗時にcontextを壊さずエラーを返す

`context_switch_to_enclave()`はstatusを返す関数に変更し、PMPの設定をhostレジスタ・CSRの交換より前へ移動した。

```text
RUN本体がFRESHをRUNNINGへ変更しthread数を予約
 → PMP/HSM状態の検査
 → OS領域の権限切替
 → EPM/UTM等のlocal PMP設定
 → 成功した場合だけhost contextを保存・交換
 → trap vector、platform hook、CPU contextを切替
```

PMP設定エラーの場合は、設定途中の領域も含めて全enclave領域の権限をPMP_NO_PERMへ戻し、OS領域をPMP_ALL_PERMへ戻す。復旧できた場合は`PMP_FAILURE=100003`を返す。RUN本体はstateをFRESHへ戻してthread数の予約を取り消す。

そのため、失敗時もhostのGPR、mepc、mstatus、SATP、midelegは切替前のままであり、adapterの通常エラー復帰（PC+4）を使える。trap vector、platform switch hook、CPUのenclave context設定には到達しない。

## 権限を復旧できない場合

権限の取り消し、またはOS領域の復旧自体が失敗した場合は`sbi_hart_hang()`で停止し、S-modeへ戻さない。不明なPMP状態のままエラー復帰することも成功復帰することもしない。

これは通常のRUNが停止する変更ではなく、PMP操作とその復旧の両方が成立しない異常時の扱いである。ホスト試験では`longjmp`で停止呼び出しを捕捉し、通常復帰しないことを検査した。実hartのWFI等の動作を実行したものではない。

## R2：EIDの切り詰めを防止

`sbi_sm_run_enclave()`で、EIDを`unsigned int`へ変換する前に`eid >= ENCL_MAX`を拒否する。エラーは`INVALID_ID=100001`。

範囲内だが未確保、またはFRESH以外のenclaveに対するRUNは、従来どおり本体がNOT_FRESHを返す。

## 共通処理の回帰対応

RUNとRESUMEは同じcontext切替関数を使うため、RESUMEもstatusを受け取り、失敗時に元のSTOPPED等の状態とthread数を復元する。RESUME helperにも同じ全幅EID検査を加えた。

逆方向の`context_switch_to_host()`にもPMP操作の戻り値検査を加えた。ここはenclave側の処理を終了・中断した後の経路なので、権限を戻せなければ停止し、誤ったhost contextへ進まない。STOP/EXITによる正常な復帰とともに回帰確認した。

## 再検証した16グループ

- 正常RUNのa1–a7、S-mode mstatus、SATP=0、mideleg=0、入口PC、保存したhost context。
- 二度目のRUN、STOPPED、未確保・範囲外・64ビット超過EIDの拒否。
- enclave contextとSM未準備の拒否。
- EPMとUTMそれぞれの設定失敗でPMP、state、thread数を復旧。mockはエラー返却前に権限が変わった場合も模擬する。
- OS領域設定失敗の伝播と復旧。
- enclave権限取り消し失敗・OS権限復旧失敗時に停止し、通常復帰しない。
- 共通RESUMEの失敗時に元のSTOPPEDへ復元し、全幅EIDも検査する。
- 実C関数によるRUN→timer STOP→RESUME→EXITでstateと戻り値ABIを確認。
- hostへ戻る際のPMP取り消し・OS権限復旧エラーでも通常復帰しない。

初回の2件のOBSERVEDは、修正後の期待を確認するPASSへ変更した。

## 検証の限界

PMP CSR・IPI、実CPUのecall/mret、割り込み配送は模擬境界である。timer STOP試験は実SM関数を呼ぶが、実際のタイマー割り込みを発生させていない。実hart上の並行動作や実機でのcontext保存を証明したものではない。

## 対象ソース

- [RUN/RESUMEとcontext切替](../../keystone/sm/src/enclave.c)
- [SM helper](../../keystone/sm/src/sm-sbi.c)
- [thread交換処理](../../keystone/sm/src/thread.c)
- [adapter](../../keystone/sm/src/sm-sbi-opensbi.c)
- [テスト](test.c)
