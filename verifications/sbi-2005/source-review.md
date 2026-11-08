# SBI 2005 RESUME：ソースコード検証

基準版：`705e9dbd0`。Keystone extension `0x08424b45`、function ID 2005。2004は未定義である。

## 呼び出し経路

```text
SDK Enclave::runの再開ループ
 → KeystoneDevice::resume / __run(true)
 → driverのRESUME ioctl / keystone_resume_enclave
 → sbi_sm_resume_enclave
 → OpenSBI sbi_ecall_handler
 → Keystone adapter
 → SM sbi_sm_resume_enclave
 → resume_enclave / context_switch_to_enclave(load_parameters=0)
```

SDKはEdgeCallHostまたはEnclaveInterruptedを受けると、必要に応じてhost dispatchを実行してresumeする。driverはユーザー向けEIDを検索してSMのEIDを渡し、SBIのerror/valueをioctl構造体へ戻す。これらはソースで照合したが、今回のホスト試験にdriver/SDKのioctl経路は含まない。

## 状態と引数

- adapterはSM準備済み、かつenclave contextでないことを要求する。
- helperは`unsigned long eid >= ENCL_MAX`を型変換前に拒否する。範囲外はINVALID_ID=100001。
- 本体は存在するEID、stateがRUNNINGまたはSTOPPED、かつ`n_thread < MAX_ENCL_THREADS`をlock内で確認する。不成立はNOT_RESUMABLE=100010。
- 受理した場合、元stateを保存し、thread数を予約してRUNNINGへ変更する。

現構成はMAX_ENCL_THREADS=1なので、RUNNINGでthread数1の場合は再開不可。条件式はRUNNINGかつthread数0も受理する。専用試験ではこの組み合わせを人工的に作って条件式の挙動を確認するが、通常の単一threadライフサイクルで自然に到達する状態と主張するものではない。

## RUNと異なる復元処理

RESUMEは`load_parameters=0`を指定する。初回RUNのEPM起動アドレスやa1–a7、SATP=0を再設定せず、保存済みのGPR・mepc・mstatus・S-mode CSRを戻す。

- `swap_prev_state()`は先頭32レジスタ分を交換する。
- `swap_prev_mepc()`は保存したenclaveのPCを戻し、hostのPCを保存する。
- `swap_prev_mstatus()`は切替対象のマスクとhost側のマスク外ビットを組み合わせる。
- S-mode CSRはsstatus、sie、stvec、scounteren、sscratch、sepc、scause、stval、sip、satpの10個を交換する。
- adapterは成功時にskip_regs_updateを設定するので、OpenSBI dispatcherが復元済みのa0/a1/mepcを上書きしない。

## タイマー中断とOCALLの違い

`regs->zero`はこの処理で利用するソフトウェア上のmarkerであり、実CPUのx0が変更される意味ではない。

| 再開理由 | 保存marker | helperでのa0 | PC |
| --- | --- | --- | --- |
| OCALLから戻る | 0 | 成功値0にする。 | 保存したecall位置+4。 |
| タイマー中断から戻る | 1 | 中断前のa0を維持する。 | 事前に-4のbiasを付けて保存した位置へ+4し、元の命令位置へ。 |

タイマー理由の試験では、実STOP helperとRESUMEを使い、bias済みフレームを渡す。実タイマー割り込みの配送やM-mode trapへの進入を起こす試験ではない。

## PMP失敗と復旧

共通context切替は、host contextを交換する前にPMP権限を準備する。設定失敗時はenclave権限を取り消し、OS権限を戻す。復旧できればPMP_FAILURE=100003を返し、RESUME本体が元stateとthread数を戻す。保存済みcontextは消費されないため再試行できる。

権限取り消し・OS復旧に失敗した場合、およびPMP/HSM guardが成立しない場合は停止する。未知の権限でS-modeへ通常復帰しない。停止分岐はテスト用longjmpで捕捉して確認する。

## 専用17グループ

1. STOPPEDからGPR（a0以外のx1–x31）、a0成功値、10個のCSR、mstatus、PCを復元。
2. timer markerで保存a0を維持しPCを一度だけ補正。
3. OCALL markerでa0を0にし、ほかの復元値を保持。
4. INVALID/DESTROYING/ALLOCATED/FRESHを拒否。
5. STOPPED/RUNNINGでthread上限に達した場合を拒否。
6. 条件式上のRUNNING・thread数0を確認。
7. 境界以上・最大値・64ビット上位付きEIDを拒否。
8. enclave contextからの呼び出しを拒否。
9. SM未準備を拒否。
10. EPM/UTM設定失敗でSTOPPED/context/権限を戻し、再試行に成功。
11. 元stateがRUNNINGの場合も、そのstateへ復旧。
12. OS領域設定失敗でcontextとthread予約を復旧。
13. enclave権限rollback不能時は通常復帰しない。
14. OS権限復旧不能時は通常復帰しない。
15. PMP/HSM guard不成立で切替を開始しない。
16. 実timer理由STOP helperからのRESUMEでa0とPCを保持。
17. OCALL STOP/RESUMEを100回繰り返し、UTMの返却データを維持し、EXIT/DESTROYで模擬資源を解放。

## 判断と範囲

上記の範囲では実装と期待動作が一致し、新しい製品コードの修正は行っていない。実C関数をホスト上で実行し、PMP API・CSRアクセス・IPI・lockの実同時性・SM準備状態の算出は模擬している。実CPUのecall/mret、タイマー配送、FPU/Vector状態、実機での並行動作まで確認したものではない。

## 対象ソース

- [ABI定義](../../keystone/sdk/include/shared/sm_call.h)
- [SM helper](../../keystone/sm/src/sm-sbi.c)
- [状態とcontext切替](../../keystone/sm/src/enclave.c)
- [thread交換](../../keystone/sm/src/thread.c)
- [adapter](../../keystone/sm/src/sm-sbi-opensbi.c)
- [OpenSBI dispatcher](../../opensbi/lib/sbi/sbi_ecall.c)
- [driverのresume](../../keystone/linux-keystone-driver/keystone-ioctl.c)
- [SDKの再開ループ](../../keystone/sdk/src/host/Enclave.cpp)
- [SDKのioctl処理](../../keystone/sdk/src/host/KeystoneDevice.cpp)
