# SBI 2003 RUN：ソースコード検証

対象：Keystone extension `0x08424b45`、function ID 2003。模擬境界は[共通README](../README.md)を参照。

## 呼び出し経路とABI

```text
SDK Enclave::run
 → driver keystone_run_enclave / sbi_sm_run_enclave
 → OpenSBI dispatcher / Keystone adapter
 → SM sbi_sm_run_enclave
 → run_enclave
 → context_switch_to_enclave
```

入力a0はEID。成功時はLinuxの次命令へ通常復帰するのではなく、トラップフレームがenclave初回入口へ切り替わる。a1–a7へEPM/UTM等の起動パラメータが入る。

## 実装の確認

1. adapterの準備状態・ホスト呼び出し検査後、helperがEIDを32ビットへ変換する。
2. `run_enclave()`は存在するFRESH enclaveだけを受け付け、lock内でRUNNINGへ変更し、thread数を増やす。
3. 実`context_switch_to_enclave()`がhostのGPR、mepc、mstatus、S-mode CSRを交換・保存する。
4. 初回入口の`mepc=dram_base-4`、S-mode復帰用mstatus、a1–a7を設定し、SATPを0にする。
5. trap vector、PMP、platform hook、CPU contextを切り替える。
6. SM helperがmepcへ4を加え、入口`dram_base`にする。adapterは`skip_regs_update`を立てるため、OpenSBI dispatcherは新しいa0/a1/mepcを上書きしない。
7. FRESH以外の入力は`NOT_FRESH=100016`。STOPPEDからの再開はRUNではなくRESUME 2005の役割。

## 実行で確認したこと

[test.c](test.c)では、context切替とthread交換のC関数も実ソースを実行し、CSRアクセスのみ配列へ置き換えた。

- 保存したhostのra、mepc、SATPと、enclaveへ渡すa1–a7の値。
- 最終的なmepcが`dram_base`ちょうどになり、+4の二重加算がないこと。
- 新しいmstatus、SATP=0、mideleg=0、RUNNING、thread数1、CPU context、hook/PMP呼び出し。
- 2度目のRUN、STOPPED、未確保・範囲外EIDの拒否。
- enclave contextと未準備状態の拒否が、状態変更より前に行われること。

実際の`mret`でenclaveへ入る動作、timer/OCALLによる復帰・再開は今回のホストテストには含まれない。

## 再現した要修正・仕様確認事項

### R1：local PMP設定の失敗がRUNの結果へ反映されない

`context_switch_to_enclave()`は`osm_pmp_set()`と各`pmp_set_keystone()`の戻り値を検査しない。実`pmp_set_keystone()`には無効領域・非所有PMP slotをエラーとして返す経路がある。

テストでは`pmp_set_keystone()`がエラーを返すようにし、それでもRUNが成功、stateがRUNNING、CPU contextがenclaveになることを再現した。実CSR状態やその後のCPUの動作を再現したものではない。戻り値を伝播する設計と、切替途中の失敗時にどのcontextで停止・復旧するかの検討が必要。

### R2：64ビットEIDの上位ビットが捨てられる

helperが`unsigned int`へ変換するため、`(1UL << 32) | valid_eid`でも対応するFRESH enclaveのRUNが成功する。2002と同じ入力仕様の明確化が必要。

8グループ中、上記2グループは`OBSERVED`であり、正常性のPASSではない。

## 参照ソース

- [RUN本体とcontext切替](../../keystone/sm/src/enclave.c)
- [実thread交換処理](../../keystone/sm/src/thread.c)
- [SM helper](../../keystone/sm/src/sm-sbi.c)
- [OpenSBI adapter](../../keystone/sm/src/sm-sbi-opensbi.c)
- [OpenSBI dispatcher](../../opensbi/lib/sbi/sbi_ecall.c)
- [PMP設定API](../../keystone/sm/src/pmp.c)
- [SDKのrun/resumeループ](../../keystone/sdk/src/host/Enclave.cpp)
- [テストプログラム](test.c)
