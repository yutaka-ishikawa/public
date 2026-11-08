> 修正前（`c6598a7bc`）の記録です。現在の状態は[source-review.md](source-review.md)を参照してください。

# SBI 2002 DESTROY：ソースコード検証

対象：Keystone extension `0x08424b45`、function ID 2002。模擬境界は[共通README](../README.md)を参照。

## 呼び出し経路とABI

```text
SDK Enclave::destroy / driver release
 → __keystone_destroy_enclave
 → driver sbi_sm_destroy_enclave
 → OpenSBI dispatcher / Keystone adapter
 → SM sbi_sm_destroy_enclave
 → destroy_enclave
```

入力a0はSMのEID。通常の復帰はa0=status、a1=0、mepc=呼び出し位置+4。

## 実装の確認

1. adapterはSM準備状態と、enclave内からホスト機能を呼んでいないことを検査する。
2. SM helperが`unsigned long`のEIDを`unsigned int`へ変換する。
3. `ENCLAVE_EXISTS(eid) && state <= STOPPED`をlock内で評価し、受け付けた場合は`DESTROYING`へ変更する。
4. lock解放後、platform destroy hookを実行する。
5. INVALIDとUTM以外の領域を0クリアし、global PMP解除・PMP領域解放を行う。
6. UTMは共有メモリなのでここでは0クリアせず、PMP領域を解放する。
7. params、SATP、thread数、region typeを初期化してEIDを`INVALID`へ戻す。

## 確認できた正常・異常系

- FRESHとSTOPPEDからのDESTROY。
- EPM内容の実ゼロクリア、UTM内容の保持、metadata・模擬PMP領域の解放。
- RUNNING、未確保・範囲外EID、二重DESTROYの拒否。
- enclave context、SM未準備の拒否。
- CREATE→DESTROYの100回繰り返しで模擬資源が残らず再利用できる。

## 再現した要修正・仕様確認事項

### D1：DESTROYINGを再び受け付ける状態条件

enumは`DESTROYING=0, ALLOCATED=1, FRESH=2, STOPPED=3, RUNNING=4`。`state <= STOPPED`はFRESH/STOPPEDだけでなくDESTROYINGとALLOCATEDも受け付ける。

テストではCREATE後のstateをそれぞれDESTROYING、ALLOCATEDにして、実関数が成功を返すことを確認した。特にDESTROYINGについて、既存処理はlockを解放してから消去・解放するため、別hartが同じEIDを再度破棄処理へ進める余地がある。

**状態条件の不備は再現済みだが、実hartでの同時実行・二重解放そのものは未実行。** 今後は受け付ける状態を列挙し、破棄処理の単一実行を試験する必要がある。

### D2：PMP解除失敗を無視して成功する

実`pmp_unset_global()`は、無効領域、HSM遷移競合、IPI同期失敗等でエラーを返し得る。DESTROYはその戻り値を検査せず、領域解放とEID解放へ進み、成功を返す。

mockで解除APIが失敗を返すようにし、成功復帰・模擬領域の解放を再現した。実際のPMP状態がどう残るかは、このテストでは判断できない。

### D3：64ビットEIDの上位ビットが捨てられる

`(1UL << 32) | valid_eid`を入力すると、helper内の32ビット変換により`valid_eid`のDESTROYとして実行される。上位ビットを含む無効IDとしては拒否されない。

通常のdriverが発行するEIDは小さいため、通常利用で必ず問題が出るという意味ではない。SBI入力を32ビットEIDとして厳密に検査するか、上位ビットを無視する仕様とするかの明確化が必要。

11グループ中、上記3グループは`OBSERVED`として記録し、正常性のPASSには数えない。

## 参照ソース

- [SM helperの型変換](../../keystone/sm/src/sm-sbi.c)
- [state enum](../../keystone/sm/src/enclave.h)
- [DESTROY本体](../../keystone/sm/src/enclave.c)
- [PMP戻り値](../../keystone/sm/src/pmp.c)
- [OpenSBI adapter](../../keystone/sm/src/sm-sbi-opensbi.c)
- [driverの破棄経路](../../keystone/linux-keystone-driver/keystone-ioctl.c)
- [テストプログラム](test.c)
