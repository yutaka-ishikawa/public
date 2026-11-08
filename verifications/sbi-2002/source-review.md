# SBI 2002 DESTROY：修正後のソース検証

修正対象：`cc885e6ad`。修正前の記録は[旧検証](source-review-before-fix.md)、実行結果は[results.md](results.md)。

## 修正した動作

### D1：破棄を受け付ける状態を限定

`state <= STOPPED`を廃止し、FRESHまたはSTOPPEDだけを受け付ける。状態確認とDESTROYINGへの変更は同じ`encl_lock`内で行う。

DESTROYING、ALLOCATED、RUNNING、INVALIDは拒否する。先行する破棄処理がlockを離してplatform callbackや消去を行っている間も、後続の破棄要求はDESTROYINGを見て拒否される。

単一スレッドのテストではplatform callbackから同じEIDのDESTROYを再入させ、内側の呼び出しが拒否され、callbackと資源解放が1回だけになることを確認した。実hart同士の同時実行テストではない。

### D2：全private領域のPMP解除成功後に解放

破棄は次の段階に分けた。

```text
FRESH/STOPPEDをlock内でDESTROYINGに変更
 → platform cleanup
 → 各private領域を消去してglobal PMP解除を確認
 → 全private領域のPMP descriptorを解放
 → UTM descriptorを解放
 → metadataを初期化してEIDをINVALIDへ
```

`pmp_unset_global()`が失敗した場合は`PMP_FAILURE=100003`を返す。PMP descriptor、UTM、EIDを解放せず、状態はDESTROYINGのまま保持する。後の領域の解除が失敗した場合も、先に解除できた領域のdescriptorをまだ解放していない。

descriptor解放自体の戻り値も確認し、失敗なら成功を返さずEIDを保持する。解放段階で途中失敗した場合、先に解放済みのdescriptorは戻さないが、全private領域の消去・global解除は既に成功している。metadataはDESTROYINGのままで、再入・RUN/RESUMEへ使われない。

Linux driverの`__keystone_destroy_enclave()`はSBIエラー時にページ解放へ進まないため、エラーの返却はdriver側のメモリ再利用抑止にもつながる。このdriver経路はソースで確認したもので、今回のホストテストには組み込んでいない。

保持した資源を自動的に再試行するAPIは今回追加していない。通常の破棄で領域が残る変更ではなく、PMP状態を確認できなかった異常時の扱いである。解消にはファームウェアの再初期化を伴う再起動等が必要となる。実PMP同期側では失敗によりSM全体が未準備となる場合もある。

### D3：型変換前にEID全体を検査

`sbi_sm_destroy_enclave(unsigned long eid)`で`eid >= ENCL_MAX`なら`INVALID_ID=100001`を返し、その後で32ビット型へ変換する。

`(1UL << 32) | valid_eid`、`UINT_MAX`、`ULONG_MAX`は拒否され、有効なenclaveを別名で破棄できない。範囲内だが未確保のEIDは、本体の状態検査でNOT_DESTROYABLEとなる。

## 再検証した15グループ

| 範囲 | 確認内容 |
| --- | --- |
| 正常破棄 | FRESH、STOPPED、EPM消去、UTM保持、metadata初期化。 |
| 状態 | RUNNING、DESTROYING、ALLOCATED、二重破棄の拒否。 |
| EID | 境界以上、32ビット超過、最大値を拒否。 |
| 呼び出し条件 | enclave contextとSM未準備を拒否。 |
| 再利用 | CREATE/DESTROYを100回繰り返し、模擬領域を再利用。 |
| 解除失敗 | 最初または後続private領域で失敗しても全descriptorとEIDを保持。 |
| 再入 | callback中の同一EID破棄を拒否し、二重解放しない。 |
| 解放失敗 | private/UTM descriptorの解放エラーを成功扱いしない。 |

5件の初回指摘のうち2002に対応する3件は、修正後の期待動作を確認するPASSへ変更した。

## 境界と残る範囲

状態管理・消去・エラー処理の実Cコードを実行しているが、PMP API、IPI、lockの同時性、platform実処理は模擬している。実機・QEMUでの全hart解除や、物理メモリの再利用までは今回確認していない。

CREATE失敗時のrollback等、2001の[残る確認事項](../sbi-2001/source-review.md)は別対象であり、本修正で解消したとはしていない。

## 対象ソース

- [DESTROY本体](../../keystone/sm/src/enclave.c)
- [SM helperのEID検査](../../keystone/sm/src/sm-sbi.c)
- [driverのエラー時処理](../../keystone/linux-keystone-driver/keystone-ioctl.c)
- [テスト](test.c)
