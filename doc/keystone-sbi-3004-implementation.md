# SBI 3004 STOPの実装完了範囲

STOPは従来からOCALL経路へ接続済み。本変更で要求検査・返却ABIを整理し、
専用の実QEMUゲスト試験を追加した。実装コミットは [9b7285ae5](https://github.com/yutaka-ishikawa/public/commit/9b7285ae5)。コードの実装済みと、新試験の実行確認待ちを分ける。

## 変更点

- SMはrequest=0（interrupt/yield）、1（OCALL）だけ受理する。
  その他は状態・PMP・host frameを変更する前にILLEGAL_ARGUMENTでenclaveへ返す。
- EID存在・RUNNING・thread数をlock内で確認する。
- 有効STOPでhost frameへ切り替えた場合だけ、helperはadapterに切替成功を通知する。
  host a0は100002/100011、a1は0、host mepcは一度だけ進める。
- 既存のtimer割込み→STOP→RESUMEで中断PCを維持する方式を使う。
  OpenSBIのtimer/IPI機構とKeystoneのcontext switchを再利用し、HSM hart_stopで置換しない。
- SDKへ任意observer `registerStopDispatch` を追加。自動OCALL dispatch／RESUMEは従来どおり。
- verification専用runtimeだけにsyscall 1100を用意し、要求の直接発行とtimer設定を行う。
  通常runtimeに試験用APIを追加しない。

この変更で旧request=2等の扱いはUNKNOWN_ERRORでhostへ戻す方式から、
ILLEGAL_ARGUMENTでenclaveを継続する方式へ変わる。終了にはSBI 3006を使う。
不具合の根拠と由来は [KSB-027](keystone-bug-report.md#ksb-027-stop要求の事前検査と戻り値の明確化)。

## 検証環境と結果

[専用試験README](../verifications/sbi-3004/README.md) にbuild・起動・実行手順、
[source review](../verifications/sbi-3004/source-review.md) に各ソースの確認内容を記載した。
コードは `verifications/sbi-3004/eapp.c`、`host.cpp`。模擬SMは使わない。
rootの `make build` と `run-all.sh`、`crypto/run-qemu.py --all` に組み込み済み。

確認済み：SDK／通常runtime／専用runtime／全guest verificationパッケージ、
SM/OpenSBI trace版firmware、S-mode拒否試験のkernel objectのbuild。
通常runtimeに専用hookがないことと専用runtimeに存在することをnmで確認した。

**追加試験のLinuxゲスト統合実行と検証モジュール.koのリンクは未確認。**
クラウド環境に対応Image/rootfs・完全なModule.symversがないため。
以前の全PASSは追加したSTOP試験に対する報告ではない。

タイマー設定区間では非同期INTERRUPTED/RESUMEを計数する。
statusはM_TIMERとM_SOFTで共通なので、原因まで確認する場合はtrace版の
`STOP asynchronous cause a=7`（M_TIMER）を確認する。
この試験だけで全hart・全割込み原因・実機を網羅したとはしない。

## 最新ログに基づくtimer修正（KSB-028／030）

ユーザーの全3bootでは、不正request3件・直接yield3回・host側3004拒否はPASS。
timer区間は中断0でexit=18、suiteはそこで停止した。後続cryptoとedgecallを
今回のログでPASS扱いしない。乱数は6件PASS、wtm-not-readyは展開時MD5不一致で未到達。

原因は2点。Sstc抑止がK3実機overrideにだけ接続され、QEMU generic DTに適用されて
いなかった。さらにtrap_vector_enclaveが4-byte未整列でmtvec設定が反映されず、
通常OpenSBI handlerがM_TIMERを処理していた。

既存timer hooksをKeystone有効時のgeneric共通経路へ移し、vectorを4-byte整列、
最終ELF symbol整列検査を追加した。実機専用のearly/final initをQEMUへ強制しない。
実SM/Eyrieの最小実行でM_TIMER→STOP→RESUME→EXIT→DESTROYを確認した。
[観測記録](../verifications/sbi-3004/evidence/timer-preemption.md) を参照。
Linux suiteの全PASSは未確認なので、全firmwareをbuild/install後に再実行する。

MD5不一致は原因未確定（KSB-029）。guest runnerは共有上のパッケージをローカルへ
snapshotしてからchecksum検査・展開する。checksum不一致は引き続き失敗とする。
