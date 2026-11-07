# SBI 2001 CREATE：ソースコード検証

対象：Keystone extension `0x08424b45`、function ID 2001。対象版・実行環境・模擬境界は[共通README](../README.md)を参照。

## 呼び出し経路とABI

```text
SDK Enclave::init
 → Linux driverのfinalize ioctl
 → keystone_finalize_enclave / sbi_sm_create_enclave
 → OpenSBI sbi_ecall_handler
 → Keystone adapter
 → sbi_sm_create_enclave
 → copy_enclave_create_args
 → create_enclave
```

ドライバの最初のCREATE ioctlによるメモリ確保と、SMのCREATE SBIは同じ段階ではない。SMへの2001はfinalize時に呼ばれる。

入力a0は`keystone_sbi_create_t`へのポインタ。成功時a0=0、a1=SMのEID。失敗時a0は正のKeystoneエラーコード、a1はdispatcherが初期化した値0。通常復帰のmepcは呼び出し位置+4であり、OpenSBIによる二重加算を`skip_regs_update`で防ぐ。

## 実装の確認

1. adapterが`sm_is_ready()`とホスト側呼び出しであることを検査する。SM helperとCREATE本体にも準備状態の検査がある。
2. 引数をSM側のローカル構造体へコピーし、コピー失敗を`REGION_OVERLAPS`として返す。
3. EPM/UTMの非ゼロ長・ページ整列・アドレス加算のoverflow・非重複、runtime/user/freeの範囲と順序を検査する。実OpenSBI+SM領域との重複も検査する。これらはEID/PMP確保より前。
4. EIDを`INVALID → ALLOCATED`にし、EPM・UTM用PMP領域を確保する。
5. `pmp_set_global(EPM, PMP_NO_PERM)`が成功してからUTMを0クリアする。EPMは測定対象なので、ここで内容を消さない。
6. metadata、初回context、platform hook、測定を準備し、成功後に`FRESH`としてEIDを返す。
7. 途中失敗時は、到達段階に応じてplatform、PMP領域、EIDを解放する。

## 実行テスト

[test.c](test.c)は実dispatcher・adapter・CREATE helperと本体、実layout validatorを実行する。12グループを用意した。

| 範囲 | 確認内容 |
| --- | --- |
| 成功 | EID、FRESH、thread数0、PMP領域2個、platform/hashの順序、UTM消去、EPM保持、PC+4。 |
| 呼び出し条件 | adapter/helperの未準備、enclave側からのホスト機能呼び出し。 |
| 引数コピー | コピー失敗時、確保へ進まない。 |
| レイアウト8種 | サイズ0、非整列、EPM/UTM重複、overflow、user順序逆転、free範囲外、SM領域重複、SM範囲取得失敗。 |
| 境界 | `free_paddr == epm_end`は有効。 |
| 資源不足 | EID枯渇、EPM確保失敗、UTM確保失敗。 |
| 後段失敗 | global PMP、platform create、hash失敗で成功を返さず、模擬資源を解放する。 |

## 判断と残る確認

通常のエラー伝播・引数検査・状態遷移・模擬資源の後始末はテストの期待と一致した。ただし、以下は合格済みとはしない。

- `pmp_unset_global()`の失敗をrollback側が検査しない。2002と同種の戻り値処理が必要かレビュー対象。
- global PMP設定が途中まで適用された後の失敗・遠隔hart停止・ACK欠落は、単純な失敗戻り値注入だけでは確認できない。今回のmockはPMP CSRを変更しない。
- platform createが途中まで作業して失敗した場合、そのhookの内部資源の復旧責任は今回のmockでは検証していない。
- 実MPRVコピー、hashアルゴリズム、実PMP確保・CSR/IPIは別の検証範囲。

## 参照ソース

- [共有ABI](../../keystone/sdk/include/shared/sm_call.h)
- [driver finalize](../../keystone/linux-keystone-driver/keystone-ioctl.c)
- [driver SBI](../../keystone/linux-keystone-driver/keystone-sbi.c)
- [OpenSBI dispatcher](../../opensbi/lib/sbi/sbi_ecall.c)
- [adapter](../../keystone/sm/src/sm-sbi-opensbi.c)
- [SM helper](../../keystone/sm/src/sm-sbi.c)
- [本体・rollback](../../keystone/sm/src/enclave.c)
- [K3 layout検査](../../keystone/sm/src/platform/spacemit_k3/k3_enclave_create_preflight.h)
- [引数コピーの下位実装](../../keystone/sm/src/mprv.h)
- [実PMP API](../../keystone/sm/src/pmp.c)
