# Cによる異常系・競合検証（途中実装）

2026-11-09（日本時間）の中間保存版。**計画全体の完了版ではない。**
新規の機能検証コードはCで実装し、実QEMU Linuxからproduction driver、
OpenSBI、Keystone SM、Eyrie/enclaveを通す。製品C関数の抽出・模擬SBIは使わない。

## 現在の到達点

- C client：driver ioctl/mmap、raw loader/runtime/eapp配置、SHA3測定、OCALL、cleanup。
- C coordinator：fork、準備barrier、CPU affinity、deadline、結果照合。
- enclave：worker/generation/sequence付き257-byte往復、RNG、report、sealing、
  backing照会、不正edgecall返信の拒否。
- C report verifier：外部device key/SM hash、nonce、measurement、Ed25519署名chain。
- 専用kernel probe：自身で確保したEPMで実SBIを呼び、SMの状態予約後に
  RUN/RESUME/DESTROYを別CPUから競合させる。
- 検証専用SM：有限gateとone-shot fault。通常buildでは無効。
- Makefile、guest実行、disposable rootfs overlayでのQEMU実行スクリプト。

## 実際に確認した結果と未確認の区別

| 範囲 | 結果 |
| --- | --- |
| 同一enclaveのRUN/RESUME/DESTROY競合9組 | Sstc on/offでPASS。gate到達と競合要求のgate中復帰を確認 |
| RNG read、ATTEST入力/出力copy、SEAL入力/出力copy、KDF境界の計6故障 | Sstc on/offでPASS。one-shot発火、初回失敗、次回回復、canary、EXITを確認 |
| dup後の元fd close、OCALL停止中SIGKILL、backing付き再作成 | qemu-dev、Sstc on/offでPASS |
| EXIT後のRESUME | FAIL。実再進入してruntime page fault。KSB-042 |
| 別enclaveの並行CREATE/DESTROY | FAILを観測。PMP admission競合と回収失敗。KSB-043 |
| CREATE失敗時のPMP rollback | 戻り値無視等をソース確認。部分適用の注入再現は未実施。KSB-044 |
| 最後に追加したPMP unset前失敗・IPI送信後の観測失敗の試験 | **コード追加のみ。追加後のビルド・QEMU実行は未確認** |
| 最後に追加したfd数/backing_bytesの集計 | **追加後のQEMU実行は未確認** |

evidence/は実行済み時点のmanifestとログ抜粋。実行は変更途中のtreeで行ったため
source_dirty=true。**その結果をこの中間commit全体のPASSに読み替えない。**
最後のPMP/IPI関連変更は、それらの実行済み記録より後の追加。

## ビルド

既存SDKと一致するカーネルbuildを用意し、リポジトリrootで実行する。

```bash
make -C verifications/robustness guest
make -C verifications/robustness kernel-module
```

SDKは既定で `.build-keystone/sdk-install`、kernelは `.build-k3/linux`。
変更する場合は `SDK_DIR=/absolute/path`、`KERNEL_BUILD=/absolute/path` を指定する。
`make -C verifications guest kernel-modules`にもbuildを追加した。
このclientはlibkeystone-host.a、g++、libstdc++を使わない。

## 通常firmwareでの実行

開発ホストから：

```bash
python3 verifications/robustness/run-qemu.py --group lifecycle
python3 verifications/robustness/run-qemu.py --group lifetime
python3 verifications/robustness/run-qemu.py --group stress --workers 2 --cycles 4
```

既定はSstc on/offの両方、16-hart Jupiter2 machine、2 worker、RNG成功32-byte、
crypto unavailable。別のRNG profileには `--rng -38|-11|-5` を明示する。
`--workers 16`はコード上指定可能だが、今回16 worker実行は未確認。

QEMU guestに9pでcheckoutを `/mnt/host` へmountしている場合：

```bash
cd /mnt/host/verifications/robustness
bash run-guest.sh --lifecycle-only
bash run-guest.sh --lifetime-only
bash run-guest.sh --stress-only --workers 2 --cycles 4
```

モード指定なしは上記3群を実行する。既知不具合をFAILのまま扱うため、
現在は全体PASSを期待しない。`run-all.sh`の既存正常suiteへの自動実行追加は未了。

## 検証専用firmwareとraw競合・障害注入

通常のinstallを上書きしない専用出力へbuildする。

```bash
bash verifications/robustness/build-firmware.sh unavailable
python3 verifications/robustness/run-qemu.py --raw-only \
  --bios .build-keystone/robustness-unavailable/install/firmware/k3/fw_dynamic.bin
```

暗号の故障も検査する場合：

```bash
bash verifications/robustness/build-firmware.sh qemu-dev
python3 verifications/robustness/run-qemu.py --faults \
  --bios .build-keystone/robustness-qemu-dev/install/firmware/k3/fw_dynamic.bin \
  --crypto-pins .build-keystone/robustness-qemu-dev/k3/firmware/crypto-public
```

qemu-devは開発用identityであり、実機の鍵・endorsementではない。
`--crypto-pins`には選択BIOSと一致するpublic metadataを指定する。
通常経路でも同オプションを付けるとreportのpins/署名を検査する。
private seedはguestへ渡さず、ログにも出さない。

`--pmp-fault`、`--ipi-fault`は最後に追加した未検証の個別モード。
各モードは独立guestで実行する設計で、通常suiteには混ぜない。
これはreceiverのCSR故障・途中copy・全fail-stopを網羅した意味ではない。

## 残作業

1. 最終追加分をbuildし、module名の既存probeとの衝突を解消する。
2. PMP/IPIの個別モードを実QEMUで確認し、結果を記録する。
3. KSB-042/043/044の修正と再検証。現時点では製品側修正は行っていない。
4. production allocator fault、SM lock待ちのper-hart観測、全資源台帳、
   in-flight close/SIGKILL、shutdown/OOM、部分PMP失敗とfail-stopの観測を拡充する。
5. 全planのcase台帳を実装と照合し、標準suiteへの実行統合を仕上げる。

[ソース対応と検証限界](source-review.md)、[case台帳](cases.yaml)、
[計画](../../doc/keystone-abnormal-concurrency-verification-plan.md)、
[不具合台帳](../../doc/keystone-bug-report.md)も参照。
