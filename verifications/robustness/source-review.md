# C異常系・競合試験のソース対応

基準は本変更直前の `ba27803c8`。実行時のcommit、dirty有無、成果物hashは
manifestへ保存する。これは抽出した製品関数のPC実行ではない。

| 検査 | 製品の入口・状態・同期 | C側のoracle |
| --- | --- | --- |
| C CREATE 2001/2006 | driver CREATE/UTM_INIT/[BACKING_INIT]/FINALIZE、SM create内部、validate_and_hash_enclave | loader/runtime/eappをraw fileとして順にページ丸めして配置。2 MiB free＋16 pages余裕。SHA3-512に3個のRV64サイズと各ページ全体を投入 |
| 所有者・初期化・copy・mmap | driver keystone_ioctlのfile所有者検査、ctx->lock、utm_init_ioctl、backing_init、mmap | foreign-file EPERM、初期化後EBUSY、bad userspace pointer EFAULT、UTM超過mmap拒否 |
| RUN/RESUME | enclave.cのencl_lock、FRESH/RUNNING/STOPPED/n_thread | 新規enclaveへのRESUME拒否、EXIT後のRUN/RESUME拒否、正常データ保持、正確なSM status |
| 破棄 | DESTROYING予約→全hart PMP解除→descriptor/EID解放 | 一度だけ成功、再作成可能。driverの二重DESTROY拒否はSM直接拒否とは別 |
| 同一enclave競合 | 実SMのRUN/RESUME/DESTROY状態予約直後 | encl_lock解放後のgate=enteredを読んで別CPUから実SBI。contenderが戻った直後にもgate=enteredを確認。9組の正確な拒否statusとholderの結果を照合 |
| STOP/EXIT/edgecall | Eyrie ocall→3004→host→2005、native CRTのEXIT→3006 | worker/generation/sequence、257-byteデータ、overflowするreturn offsetの拒否、返信先の未変更 |
| RNG 3001 | Eyrie getrandom→SM rng_lock→選択provider | 無効flags/null拒否、32-byte結果または明示設定errno、前後canary。統計やentropyの保証はしない |
| ATTEST 3002 | Eyrie checked wrapper→SM security_lock/workspace→Ed25519 | 正規1352-byte型、data_len32、nonce、独立測定、外部device pinとSM hash、device→SM certificateとSM→enclave reportの署名 |
| SEAL 3003 | Eyrie→同じsecurity_lock/workspace→HKDF | 192-byte容量、同じmeasurement/識別子の反復鍵一致をenclave内で検査。鍵をhostへ送らない。commitmentのみ送る |
| QUERY 4000 | 既存MULTIMEM_VERIFY runtime hook→実plugin | 自分のbacking PA/size、未登録status、8種類の不正ID/出力先/予約引数のstatusとread-only canary |
| driver寿命 | Linux file/VMA参照、driver release→SM DESTROY | dup後に元fd closeして実行、OCALL STOPPED時SIGKILL、backing付きenclave再作成。in-flight中SIGKILL全位置の網羅ではない |
| 障害注入 | production provider/copy/KDF境界のone-shot分岐 | 対象SM EIDとstageが一致した1回だけ失敗。state=5で発火確認。最初の失敗/出力未変更、次回同じ実サービスの成功、canary、EXITを検査 |

## C verifierの契約

`common/report_check.c` は既存 `sdk/src/verifier/Report.cpp:checkSignaturesOnly`
と同じ署名対象を、同じCのEd25519 primitiveで検査する。
SM certificateはhash64＋public_key32の96 bytes、enclave reportは
hash64＋uint64 data_len＋実データ32 bytes。1024-byte予約領域全体を署名対象にしない。
外部device pinとSM hashを必須とし、report自身の鍵だけで自己承認しない。
C clientの測定は `sm/src/attest.c:validate_and_hash_epm` と同じraw-layout契約。
report成功により本物のSMが測定した値と照合する。
新clientのlinkにはlibkeystone-host.a、C++ Report、libstdc++を使わない。

## 検証用境界

通常buildでは `KEYSTONE_VERIFICATION` 未定義で、gate/faultはcompile out。
4900は検証build専用のprivate FIDで、標準Keystone ABIへの追加ではない。
SMはenclave文脈からの制御を拒否する。Linux moduleはCAP_SYS_RAWIOと0600を要求し、
raw RUN/RESUME/DESTROY対象は、そのopenが自身で確保・初期化したSM EIDだけ。
任意PA/任意CSR/他enclaveの秘密読出しコマンドは提供しない。
通常driverのSM EIDを推測してraw callする方法を使わない。

gateはencl_lockを持たずに有限時間で終了する。IPIも処理するため、別hartの
正規PMP更新をgate自身が永久に妨げない。gateへの到達とcontender復帰が
同じgate区間内に入らなければ成功にはしない。専用probeのraw suiteは
他のCREATEを併走させない独立guestで実行し、EID再利用を競合させない。
故障時に保護状態が不明のページはfreeせずquarantineする。

F-COPYは実copy処理の直前で失敗を注入する。中途byte数のcopyや実bus faultを
再現した試験ではない。F-CRYPTOはKDF呼出境界の失敗であり、Ed25519計算破損ではない。
F-RNGはinit成功後のread境界の失敗であり、Zkr品質やWTM実機所有権の検証ではない。

## 残る範囲

case台帳のPLANNED/BLOCKEDは未実装で、SKIP/PASSへ読み替えない。
複数processのlifetime overlapは共有security_lock/rng_lockの待ち発生の証拠ではない。
per-hart ring、全資源の厳密台帳、PMP/IPIの途中失敗注入、fail-stopのGDB証拠、
全てのallocation/close位置、shutdown中断、OOM回復、複合faultは残る。
PMP更新・rollbackにKSB-043/044が見つかったため、計画第9節に従い、これらを
解決する前に破壊的なPMP/IPI注入・stress規模の拡大を行わない。
