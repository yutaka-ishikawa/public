# SBI 2004：未定義function IDのソースコード検証

対象：Keystone extension `0x08424b45`、function ID 2004。

## 定義の確認

現在の[共有ヘッダー](../../keystone/sdk/include/shared/sm_call.h)では、ホスト側の機能は2001 CREATE、2002 DESTROY、2003 RUN、2005 RESUMEである。**2004の機能定義はない。** 比較した上流Keystone `b8c2385355b2afb5b94a4e5dc503936f1502b972`でも同じである。

したがって、2004をRESUMEへ読み替えず、未割り当てfunction IDとして扱う。2004が将来利用されないことを保証する意味での永久予約とは断定しない。

## dispatcherの動作

1. extension `0x08424b45`はKeystone adapterへ到達する。
2. SM未準備なら先に`NOT_IMPLEMENTED=100100`を返す。
3. 2004はホスト側の番号範囲に含まれるため、enclave contextなら先に`SBI_PROHIBITED=100014`を返す。
4. SM準備済み・ホストcontextではswitchのdefaultへ入り、`NOT_IMPLEMENTED=100100`を返す。
5. adapterがa0=status、a1=0、mepc+=4を設定してskipを立て、OpenSBIのエラー正規化や二重PC更新を避ける。

これは、extensionそのものが存在しない場合の標準SBI `ENOTSUPP=-2`とは異なる。

## 実行テスト

[test.c](test.c)は実adapterとOpenSBI dispatcherを通して次の5グループを確認する。

- 準備済みのホストcontextで100100、a1=0、PC+4。
- 既存FRESH enclaveのEIDを渡してもmetadataやPMP領域を変更しない。
- enclave contextでは100014になり、defaultよりも呼び出し範囲検査が優先される。
- SM未準備の拒否。
- a0に`ULONG_MAX`を渡してもそれを引数ポインタやEIDとして処理しない。

RESUMEが2005であることはコンパイル時assertでも確認する。

## 判断

未定義IDへの応答はソースの定義・dispatcherの動作と一致した。2004の実機能のテストに合格したという意味ではない。RESUMEの検証が必要な場合は、2005を独立した対象として追加する。

## 参照ソース

- [ABI定義](../../keystone/sdk/include/shared/sm_call.h)
- [adapterの呼び出し範囲とdefault](../../keystone/sm/src/sm-sbi-opensbi.c)
- [OpenSBI dispatcher](../../opensbi/lib/sbi/sbi_ecall.c)
