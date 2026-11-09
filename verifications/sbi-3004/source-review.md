# SBI 3004ソースレベル確認

対象：本リポジトリのSBI 3004完了作業。実行結果とは分けた制御経路の確認。

## ABIと既存機能の利用

Keystone FID 3004、a0=request。request=0は割込み／yield、1はOCALL。
Linuxから呼び出すとadapterがPROHIBITED=100014で拒否する。
有効なSTOPはenclaveへ即時returnせず、保存したLinux RUN/RESUMEフレームへ戻る。
Linuxへはa0=100002または100011、a1=0。2005で再開した後にenclave側が継続する。

同梱OpenSBIのecall dispatcher、trap frame、timer/IPI処理を再利用する。
OpenSBI HSMのhart_stopはCPUを止める機能で、enclave STOPの代替ではない。
独自のtimer/IPI制御を新設せず、Keystone既存context switchへ接続する。

## 経路ごとの確認

| 層／ソース | 確認・変更内容 |
| --- | --- |
| `keystone/sm/src/sm-sbi-opensbi.c` | enclave呼出権限を検査。helperが実際に切り替えた場合だけskip_regs_updateにしてOpenSBIによる二重更新を防ぐ。不正要求は通常のecall復帰でenclaveへ戻す。 |
| `keystone/sm/src/enclave.c` | request=0/1以外を状態変更前に拒否。EID範囲・存在・RUNNING・n_thread>0をlock内で確認。最後の実行threadをSTOPPEDにしてhostへ切替。 |
| 同上context_switch_to_host | enclave PMPを閉じてOS PMPを復元。全レジスタ、CSR、mepc、mstatusを既存thread処理で交換し、cpuのenclave contextを解除。PMP失敗は既存の停止方針。 |
| `keystone/sm/src/sm-sbi.c` | INTERRUPTED/EDGE_CALL_HOSTだけを切替成功として扱う。復元host frameのa0/a1を設定しmepcを4進める。UNKNOWN_ERRORを成功扱いしない。 |
| `keystone/sm/src/thread.c` | request=0はreturn_on_resume=falseで中断レジスタを維持。request=1はtrueで再開時の戻り値0を設定する既存方式を維持。 |
| `keystone/sm/src/sbi_trap_hack.c` | IRQ_M_TIMER/IRQ_M_SOFTは中断enclaveのmepcを4戻してSTOP0へ渡す。RESUMEの+4と相殺し中断命令を飛ばさない。traceにcauseを記録。 |
| `keystone/runtime/call/sbi.c` | STOPのinline asmはa0とa1の両方を入出力宣言。request以外のABIを独自拡張しない。 |
| `keystone/sdk/src/host/Enclave.cpp` | INTERRUPTED/EDGE_CALL_HOSTを受けて既存のresumeループへ戻る。新しい任意observerは受け取った実statusを観測する。 |

## 不具合と互換性

KSB-027を修正。不正requestで先に状態変更・host復帰してUNKNOWN_ERRORとする旧処理を、
同じenclaveへILLEGAL_ARGUMENTとして返す方式に変更した。有効な0/1のstatusは維持する。
旧STOP_EXIT_ENCLAVE=2は定義だけ残すが、3006 EXITの代わりに使えるとは扱わない。
未使用host a1は0とし、停止したenclaveの保存レジスタをこの値で上書きしない。
上流最新版との照合による上流全体の不具合認定ではない。

## 検証の限界

本書はソース上の確認であり、競合、全hart、PMP失敗注入、実機の動作完了を意味しない。
専用試験は実SBIを呼ぶが、ここでのクロスビルド成功だけをゲストPASSとはしない。
[README](README.md) に試験コード・build・実行・期待結果を記載する。

## Linux Sstc／SM M_TIMERの共存追加

`opensbi/lib/sbi/sbi_timer.c`の共通event入口でenclaveのTIME要求だけを振り分ける。
SMはRUN/RESUMEごとに独立のMTIMER期限を設定し、guestのUINT64_MAX要求で取り消させない。
host timer状態はhartのscratch、guest絶対期限はSM thread metadataで保持する。
STOP/EXITでhostの比較値・STCE・MTIE・必要な保留STIPを復元する。
[共存設計・検証限界](../../doc/keystone-sstc-coexistence.md) を参照。
