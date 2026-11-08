# edgecallのソース検証

## 受け渡し経路

```text
enclave app ocall()
 → Eyrie dispatch_edgecall_ocall()
 → private引数をUTMへcopy_from_user
 → SDKが引数offset/sizeを設定
 → SBI STOP 3004 (STOP_EDGE_CALL_HOST)
 → SMがEPM権限を取り消してhostへ復帰
 → SDK incoming_call_dispatch() / host wrapper
 → UTM上にreturn offset/size/statusを設定
 → SBI RESUME 2005
 → Eyrieがreturn範囲を検査してprivate出力へcopy_to_user
```

## 実コードを使用する箇所

- [SDK edge_call.c](../../keystone/sdk/src/edge/edge_call.c)：pointer/offset、args/ret、setup。
- [SDK edge_dispatch.c](../../keystone/sdk/src/edge/edge_dispatch.c)：call IDから登録済みwrapperを呼ぶ。
- [Eyrie syscall.c](../../keystone/runtime/call/syscall.c)：OCALLのコピー、STOP、host status、return範囲、コピー長の処理。
- [SM enclave.c](../../keystone/sm/src/enclave.c)：STOP/RESUMEとcontext/PMP切替。ホスト試験では実関数を共通ハーネスへ取り込む。

ホストのデータ試験ではSDKの二つのCファイルをそのままコンパイルする。Eyrieの対象関数は`extract_runtime.py`で原文どおり取り込み、関数のモデルを書き直さない。

同一共有領域を別VAへマップし、hostへ切り替わるとSDKの共有startをhost側VAへ、戻るとruntime側VAへ変更する。正しいoffset方式なら両側のVAが違ってもデータが一致する。

## privateポインタとPMPは別の判定

SDKは共有範囲外のprivateポインタをsetup_call/setup_retへ登録する操作や、不正offsetからpointerを作る操作を拒否する。しかし、これはhost側ライブラリの検査であり、hostが検査を無視して物理EPMへアクセスできないことを証明しない。

ゲスト用のアプリは、private領域にあるrequest構造体の仮想アドレスもデータとしてhostへ渡す。host wrapperはその値を共有範囲外と判定するが、実アクセス試験にはその仮想アドレスを直接使わない。SDKからEPMとUTMの物理アドレスを取得してprobeモジュールへ渡す。

[probeモジュール](kernel/edgecall_pmp_probe.c)は通常RAMのdirect mapを使い、Linux標準の例外回復付き読み取り`copy_from_kernel_nofault()`を実行する。通常メモリとUTMが読める正の対照を要求し、EPMの読み取りだけが-EFAULTとなることを条件にする。単なるuser VAの未マップや`/dev/mem`ポリシーによる拒否を合格にしない。

## 範囲と未確認事項

ホストで実行した試験はデータコピーとプロトコル、SMのPMP API呼び出しを確認する。実CSR、実IPI、実MPRV/user-copy、CPUによるSTOP/RESUMEは模擬境界である。

ゲスト用の実PMP参照試験は実装してクロスコンパイルしたが、今回の環境では未実行。kernelの`.o`生成は確認したが、完全な対応ビルドのModule.symversがなく、`.ko`のリンク・ロードは未確認である。

この専用試験は使用するOCALL/return経路を対象とする。全SDK API、wrapped-return API、全call IDの境界、全システムコール、すべての悪形式データを網羅した試験ではない。今回の追加では製品コード自体は変更していない。
