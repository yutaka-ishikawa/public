# 256 MiB EPMの準備と実SM統合試験

## ドライバと起動引数

256 MiB級のEPMには、通常のbuddy allocatorに加えてCMAの物理連続領域を使う。
ドライバは最大orderを含む境界条件を修正し、64 bit DMA maskを設定して
`dma_alloc_pages()` を使用する。SMへ渡すCPU物理アドレスと解放用DMA handleは
別々に管理する。K3のRAMは4 GiB以上にあるため32 bit DMA制約を使わない。

Keystone用 `scripts/run-k3.sh` の既定引数：

```text
cma=512M@4G-16G
```

RISC-Vの既定CMA予約範囲は4 GiB未満でK3 RAMと合わないため、明示範囲を指定する。
実機では実際のRAMと予約領域に合わせて選ぶ。`--append` を指定した場合は
この引数も自分で含める。CMA予約の変更はゲスト再起動後に反映される。

```bash
# ゲスト。必要なら先にprocfsをマウントする
cat /proc/cmdline
grep Cma /proc/meminfo
```

`CmaTotal: 524288 kB` を確認する。容量や断片化により確保に失敗し得る。
256 MiBのfree memoryにはELF/runtime等のEPM overheadも加わるため、
予約は余裕を持って512 MiBとする。同時に複数の大容量enclaveを保証するものではない。
UTMの大容量化はこの修正の対象ではない。

## ドライバの更新

ホストで、使用中のImageと一致する完全なカーネル出力を用意して：

```bash
cmp install/keystone/boot/k3/Image .build-k3/linux/arch/riscv/boot/Image
test -s .build-k3/linux/Module.symvers
mkdir -p .build-keystone/driver
cp -a keystone/linux-keystone-driver/. .build-keystone/driver/
make -C linux-k3 O="$PWD/.build-k3/linux" ARCH=riscv \
  CROSS_COMPILE=riscv64-linux-gnu- M="$PWD/.build-keystone/driver" \
  KEYSTONE_SDK_DIR="$PWD/.build-keystone/sdk-install" modules
```

ゲストで、実行中のenclaveがない状態で：

```bash
mount-hostshare
# ロード済みの場合だけ。使用中なら強制しない
rmmod keystone_driver
insmod /mnt/host/.build-keystone/driver/keystone-driver.ko
cp /mnt/host/.build-keystone/driver/keystone-driver.ko \
  /usr/lib/modules/$(uname -r)/extra/keystone-driver.ko
depmod -a
```

既存イメージへのツール導入は [ゲストコマンド](keystone-guest-commands.md) を参照。
新規イメージはbuild-install-keystone.shが修正版ドライバを配置する。

## 実際のSM実行まで確認する

ホストでSDKを再ビルドし：

```bash
bash scripts/build-install-keystone.sh --only sdk
make -C verifications guest
```

ゲストで：

```bash
cd /mnt/host/verifications/edgecall
EDGE_VERIFY_PACKAGE=build/guest-256/edgecall.ke bash run-guest.sh --data-only
```

256 MiB設定のenclaveが実SMで作成・実行され、OCALL/RESUMEとEXIT/DESTROYまで
成功することを確認する。全容量への読み書き診断とは異なる。
[省略なしの全試験](../verifications/README.md)は実SBIエラー応答とPMP probeも実行する。

以前のepm-allocationの模擬試験と、SMへFINALIZEしない確保専用試験は削除した。
現在は実enclaveの正常ライフサイクルを通る上記試験を使う。
実ゲストで4 MiB buddy確保とOCALL往復の成功報告はあるが、
新しい256 MiB統合試験の実行成功は未確認。

CMA確保後に `PMP global set unavailable or incomplete` / 100003となる場合、
旧SMのTOR読み戻し検証が未対応。[原因と修正](keystone-tor-pmp.md)に従い
ファームウェアを更新してQEMUを起動し直す。

TOR修正後に `mm/mm.c` の整列assertで終了する場合は、
[Eyrie側のCMA対応と累積修正](keystone-cumulative-changes.md)を参照し、
Eyrie/loaderを再ビルドして試験パッケージを更新する。
