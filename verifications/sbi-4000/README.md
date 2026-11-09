# FID 4000 / multimem の実QEMU verification

Linux → driver/SDK → OpenSBI → Keystone SM → Eyrie → enclaveの統合試験。
模擬SMやホストでSM関数を呼ぶ試験ではない。pagingは有効にしない。

## ビルドと実行

ホストのリポジトリ直下で、対応するkernel/driver/rootfsも更新する：

```sh
make build-install JOBS=4
python3 verifications/sbi-4000/run-qemu.py
```

専用runnerは通常firmwareでSstc on/offをそれぞれ起動する。元rootfsは変更せず、
一時qcow2 overlayを使う。ログとhash付きmanifestは`results/`に保存する。
別イメージは`--rootfs PATH`、片方の試験は`--sstc on`または`--sstc off`。

verificationだけの再ビルド：

```sh
make -C verifications/sbi-4000 guest
make -C verifications/sbi-4000 kernel-module
```

`guest`はSDKのinstall済みheader/libraryを使うため、SDK変更後は先に`make sdk`を実行する。
kernel-moduleには起動Imageと一致するkernelのビルド結果が必要。
QEMUゲストのrootシェルから手動で実行する場合：

```sh
mkdir -p /mnt/host
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host
bash /mnt/host/verifications/sbi-4000/run-guest.sh
```

`verifications/run-all.sh`および`crypto/run-qemu.py --all`にも組み込み済み。
旧rootfs内のdriverでは新IOCTLが利用できないため、更新後にゲストを再起動する。
モジュールは未ロード時に実行スクリプトがロードする。

## plugin無効profile

既定のK3ビルドはmultimem有効。無効profileは明示的に別出力へ作る：

```sh
KEYSTONE_MULTIMEM=0 bash scripts/build-install-k3.sh --only firmware \
    --build-dir .build-k3/multimem-disabled \
    --prefix .build-k3/multimem-disabled/install
python3 verifications/sbi-4000/run-qemu.py --disabled \
    --bios .build-k3/multimem-disabled/install/firmware/k3/fw_dynamic.bin
```

無効profileでは4000の未実装status、拡張CREATEの拒否、旧CREATEでの実行継続を確認する。
有効profileを明示する場合は`KEYSTONE_MULTIMEM=1`。

## ファイルと検証範囲

- `test.c` + `kernel/probe.c`：固定された不正要求を実ecallで送信。
  hostからの4000、2006のversion/size/予約引数、backingの0・不整列・overflow・EPM/UTM重複。
- `eapp.c`：実U-mode enclaveから検証専用syscall 1098とOCALLを反復する。
- `keystone/runtime/call/multimem_verify.c`：専用runtimeだけに組み込むS-mode検証hook。
  正常照会、未知plugin/call、NULL・不整列・overflow・UTM・書込み不可のapp rodata・予約引数を検査。
  SMを置き換えず、全要求を実FID 4000へ送信する。
- `host.cpp`：SDKがdriverから受け取ったPA/確保サイズと照会値を完全比較。
  file所有権、不正IOCTL、二重割当、FINALIZE失敗後close、2 enclaveの3反復生成・破棄、
  256 MiB + 4 KiBのCMA/TOR backing、未登録、プロセス終了時の解放を検査。
  OCALL時に利用可能CPUを切り替え、timerによる中断が発生したことも検査する。
  全hartや同時実行の網羅試験ではない。
- `/sys/module/keystone_driver/parameters/backing_bytes`を前後比較し、driverの保持量が
  初期値へ戻ることを確認する。一般的なLinux全体のメモリリーク検出ではない。
- `Makefile`、`run-guest.sh`、`run-qemu.py`：ビルド、guest実行、QEMU自動起動。
- [ソース検証](source-review.md)、[結果](results.md)。

backingはhost-visibleな非信頼メモリでありEPMではない。試験中も平文秘密の退避には
使わない。実際のbacking読み書き・evict/swap-in・hash・暗号化・replay対策は、
paging（P3）とPAGE_HASH/PAGE_CRYPTO（P4）の別試験で実装する。
この試験のPASSはそれらやJupiter2実機、DMA隔離の完成を意味しない。

他enclaveのbacking PAを検証用kernel moduleから拡張CREATEに渡し、
SMが重複登録を拒否することも確認する。このprobeはCAP_SYS_RAWIO限定で、
通常driverの任意PA登録APIではない。
