# Milk-V Jupiter2でOP-TEEを動かすための手順

調査日: 2026-11-10

## 対象と検証状況

対象はSpacemiT K3搭載の **Milk-V Jupiter2 SBC（Pico-ITX系）**。Jupiter（K1/M1）、Jupiter2 NX、Jupiter2 Dev Kitは区別する。

この文書は公開SDKのソースとハードウェア資料に基づく。実機でのビルド・書き込み・起動は未検証である。SDK取得、構成確認、ビルド、起動後の検証方法を示す。**Jupiter2固有のSPI NOR/UFSへの書き込み対応が未確認のため、この文書だけで実機更新を完了できる状態ではない。** 第6節の未確認事項を解消してから更新する。

## 1. 必要な変更

通常版ファームウェアから導入する場合、OP-TEE対応のブートファームウェアへの更新が必要。Linuxにパッケージを追加するだけでは動作しない。

公開SDKでは以下を組み合わせる。

| 要素 | 用途 |
| --- | --- |
| OP-TEE対応FSBL/SPL | `optee.itb`をロードする初期ブートローダー |
| 対応OpenSBI | M-mode側のTEE実行・隔離に必要なファームウェア |
| OP-TEE対応U-Boot・DTB | TEE用ドメイン、デバイスツリー処理、起動設定 |
| `optee.itb` | OP-TEE本体のFITイメージ |
| 対応Linux・DTB | Linux側のTEEドライバーと共有メモリー設定 |
| libteec、tee-supplicant、TA、テストアプリ | LinuxからのTA呼び出しと検証 |

SDKの`k3_sec.config`は`CONFIG_SPACEMIT_SECURE_BOARD=y`を有効にする。SPIのパーティション配置も通常版と異なる。既存の配置に`tee.bin`だけを書き込む方法は使わない。

## 2. ハードウェアと準備

**Jupiter2 SBCにはSD/microSDスロットがない。** SDKがSDイメージを生成できても、このボードでSD起動できる根拠にはならない。USBカードリーダーもBoot ROMのSDインターフェースを代替しない。

Milk-V公式仕様のストレージはオンボードUFSとM.2 NVMe。Milk-V公式Quick StartはSpacemiT K3 Pico-ITXユーザーガイドを参照している。同ガイドのOS導入方法はUSB Type-C経由のTitan/fastbootによる書き込みである。

用意するもの:

- Linux x86_64のビルドPC。Git、Googleの`repo`、make、利用可能なDocker環境。
- PC側の書き込みツールTitan、またはSDKに適合するfastboot環境。
- データ転送対応USB Type-Cケーブル、ボード用電源。
- シリアルログ取得用USB–TTLアダプター。電圧とTX/RX/GNDは対象基板の資料で確認。
- 現行ファームウェア、起動環境、必要なUFSデータのバックアップと復旧用イメージ。

通常のLinux USB/NVMe起動と、初期ブートファームウェアの更新は別の操作である。UEFIのBoot Managerで起動先を変えても、OP-TEE対応ファームウェアには置き換わらない。

## 3. SDKを取得する

新しい作業ディレクトリで実行する。

```bash
mkdir k3-optee
cd k3-optee

repo init \
  -u https://github.com/spacemit-com/manifests.git \
  -b main \
  -m k3-br-v1.0.y.xml
```

公開manifestのremoteはGitHub SSH形式。SSH認証を使わない場合は、取得したmanifestのremoteだけをHTTPSへ変更する。

```bash
sed -i 's|git@github.com:spacemit-com|https://github.com/spacemit-com|' \
  .repo/manifests/k3-br-v1.0.y.xml

repo sync -j8
repo manifest -r -o manifest-pinned.xml
```

`manifest-pinned.xml`は再現用に保存する。`k3-br-v1.0.y`は更新されるブランチであり、取得時点によって内容が変わる。

このmanifestには`bsp-src/linux-6.18`、`bsp-src/opensbi`、`bsp-src/uboot-2022.10`、`bsp-src/optee_os`などが含まれる。各コンポーネントは同じmanifestから取得し、別系列のバイナリーと混在させない。

## 4. 構成を確認してビルドする

SDKルートで確認する。

```bash
grep -E 'BR2_(TARGET_OPTEE_OS|PACKAGE_OPTEE|PACKAGE_SECURE_FIRMWARE|TARGET_UBOOT_NEEDS_OPTEE)' \
  buildroot-ext/configs/spacemit_k3_defconfig

cat bsp-src/uboot-2022.10/configs/k3_sec.config
```

調査時の公開defconfigには次の設定がある。

```text
BR2_TARGET_OPTEE_OS=y
BR2_TARGET_OPTEE_OS_PLATFORM="k3"
BR2_TARGET_OPTEE_OS_CORE_IMAGES="tee.bin"
BR2_PACKAGE_OPTEE_EXAMPLES=y
BR2_PACKAGE_OPTEE_TEST=y
BR2_PACKAGE_SECURE_FIRMWARE=y
BR2_TARGET_UBOOT_NEEDS_OPTEE_TEE=y
BR2_TARGET_UBOOT_NEEDS_OPTEE_TEE_BIN=y
```

ここでの`PLATFORM=k3`はSpacemiTのRISC-V移植。TI K3向けの`k3-am62x`などの手順は使わない。`local.mk`ではOP-TEE本体を`bsp-src/optee_os`から取得するよう上書きされるため、defconfigのバージョン文字列だけで上流版と判断しない。

```bash
make k3-build
```

公開トップレベルMakefileは、通常の新規環境でK3 defconfigを適用し、Dockerのビルド環境を準備してビルドする。失敗した場合は最初のエラーを確認する。依存パッケージ、レジストリ到達性、ディスク容量の問題と、ソースの不整合を区別する。

## 5. 生成物を確認する

```bash
ls -lh output/k3/images/sec/
ls -lh output/k3/images/*-sec.zip
```

| 生成物 | 意味 |
| --- | --- |
| `sec/tee.bin` | OP-TEE本体 |
| `sec/optee.itb` | 単独FIT形式のOP-TEE |
| `sec/u-boot.itb` | `k3_sec.config`を適用したU-Boot |
| `sec/FSBL.bin` | OP-TEE構成で生成した初期ブートローダー |
| `sec/env.bin` | OP-TEE構成の環境データ |
| `sec/uboot/*.dtb` | secure構成のU-Boot DTB |
| `*-sec.zip` | secure構成のファームウェア、共有OSファイル、パーティション定義 |
| `*-sec-sdcard.img` | SDKのSD対応ボード用。Jupiter2 SBCでは使わない |

`secure-firmware.mk`は通常版U-Bootの後にsecure構成をビルドし、生成物を`images/sec/`へ集める。`prepare_img.sh`はsecure構成のZIPを別途生成する。通常版ZIPと取り違えない。

対象ZIPを明示的に選び、内容を調べる。下記のファイル名は実際の生成名に置換する。

```bash
unzip -l output/k3/images/Buildroot-k3-TIMESTAMP-sec.zip
```

`optee.itb`、`u-boot.itb`、`env.bin`、`factory/FSBL.bin`、`fw_dynamic.itb`、`partition_*.json`が含まれることを確認する。**ZIPの生成成功はJupiter2への書き込み互換性や起動成功を保証しない。**

## 6. 実機へ書き込む前に確定すること

現時点では、Jupiter2で以下の対応が未確認。書き込みコマンドを確定するための残作業である。

| 確認項目 | 確認先・必要な結果 |
| --- | --- |
| 基板リビジョンとブート媒体 | 現行起動ログ、基板資料。FSBL/OpenSBI/U-BootがSPI NORとUFSのどちらにあるか |
| SPI NOR/UFS両方の更新要否 | secure構成のSPLと実機起動経路を照合する |
| 対象パーティション定義 | `flash_config/sec/partition_*.json`と対象媒体の容量・既存配置を照合する |
| Titanの設定 | 生成ZIPの`fastboot.yaml`と、対象ボード・媒体を選ぶ操作が整合するか |
| Pico-ITX用DTBの選択 | U-Bootのボード検出、生成DTB、Jupiter2の実機リビジョンを照合する |
| 復旧方法 | 通常版ファームウェアと書き戻し方法を確保し、FDLでPCから検出できることを確認する |

SPI向け`k3_sec.config`のオフセットと、ブロック媒体向け`partition_universal.json`のオフセットは異なる。後者の値をSPIフラッシュ書き込みに流用しない。任意のfastbootパーティション名や`sf write`アドレスを推測して実行しない。

Jupiter2固有の対応が確定した後、対応するsecureパッケージをTitan/fastbootで書き込み、正常終了を確認して電源を入れ直す。通常版の環境変数が残る場合の扱いも、その更新手順に含める。

### 書き込みモードへ入る操作

K3 Pico-ITXガイドで確認できる操作は次のとおり。

1. 電源オフでFDLボタンを押し続ける。
2. 電源を接続し、FDLを離す。
3. DRD Type-Cポート（ガイドの図で18番）をデータ対応ケーブルでPCに接続する。
4. Titanまたはfastbootでデバイスの認識を確認する。

給電済みなら、FDLを押したままRSTを短く押し、FDLを離す方法も記載されている。DRD Type-Cは給電入力ではないため、別の電源入力を使う。ガイドのコネクタ図で実物を識別する。

この操作は書き込みモードへの移行方法であり、OP-TEEファームウェアの書き込み先を指定するものではない。

## 7. 起動後の動作確認

シリアルを115200 baudに設定し、電源投入直後からログを保存する。OpenSBI、OP-TEE、U-Boot、Linuxそれぞれのログを確認する。表示順や文言はSDKの実装に依存する。

Linux上で実行する。

```bash
uname -a
cat /proc/device-tree/model
dmesg | grep -Ei 'optee|tee|sbi'
ls -l /dev/tee*
pgrep -a tee-supplicant
command -v optee_example_hello_world
command -v xtest
```

`tee-supplicant`が起動していない場合は、まずOSのサービス起動方法を確認する。手動検証では必要な権限で次を実行する。既存プロセスと二重起動しない。

```bash
tee-supplicant &
```

続いてTA呼び出しとテストを実行する。

```bash
optee_example_hello_world
xtest
```

合格条件は、OP-TEEの初期化が確認できること、LinuxのTEEデバイスが利用できること、Hello WorldのTA呼び出しが成功すること。`xtest`の失敗・スキップは個別に記録し、全件成功と混同しない。デバイスファイルが存在するだけでは動作確認完了としない。

| 症状 | 主な確認点 |
| --- | --- |
| OP-TEE起動ログがない | 通常版のFSBL/SPL、別媒体のブートファームウェア、`optee.itb`のロード経路 |
| Linuxは起動するが`/dev/tee*`がない | ドライバー、対応DTB、OpenSBI、secure U-Bootの組み合わせ |
| TAを開けない | tee-supplicant、TA配置、libteec、TA開発キットとの整合 |
| 共有メモリーやドメイン初期化に失敗 | 同一SDK由来か、予約メモリーとPMP/IOPMP設定が一致するか |

## 8. 評価用設定と製品用設定

調査時のK3 OP-TEE設定には`CFG_K3_HUK_ALLOW_UNFUSED=y`があり、eFuse未設定時は公開の開発用HUKを使用できる。実機で動作しても、製品固有の秘密鍵を保護できているという意味にはならない。

OP-TEEの動作確認とSecure Bootの有効化は別に検証する。この手順ではeFuseの書き込みを行わない。製品化時はHUK、TA署名鍵、暗号ドライバー設定、ストレージ保護、ブート検証を別途設計する。

## 参考資料

- [Milk-V Jupiter2公式仕様](https://milkv.io/jupiter2)
- [Milk-V Jupiter2 Quick Start](https://milkv.io/docs/jupiter2/getting-started/quick-start)
- [K3 Pico-ITXユーザーガイド（公式PDF）](https://cdn-resource.spacemit.com/file/product/K3/k3_pico_ug_en.pdf) — 2.3.2 Flashing Interface、3.1 OS Installation、3.2 Serial Debugging
- [同ガイドのHTML版](https://docs.bit-brick.com/docs/k3_pico/pico_user_guide)
- [K3 CoM260のOP-TEE対応表記](https://www.spacemit.com/community/development-kit/k3-com260) — Jupiter2と同一の基板という意味ではない
- [SDK manifest](https://github.com/spacemit-com/manifests/blob/main/k3-br-v1.0.y.xml)
- [ビルド用Makefile](https://github.com/spacemit-com/scripts/blob/k3-br-v1.0.y/Makefile)
- [K3 Buildroot defconfig](https://github.com/spacemit-com/buildroot-ext/blob/k3-br-v1.0.y/configs/spacemit_k3_defconfig)
- [ローカルソースの指定](https://github.com/spacemit-com/buildroot-ext/blob/k3-br-v1.0.y/local.mk)
- [secure-firmware生成処理](https://github.com/spacemit-com/buildroot-ext/blob/k3-br-v1.0.y/package/secure-firmware/secure-firmware.mk)
- [secureイメージのパッケージ処理](https://github.com/spacemit-com/buildroot-ext/blob/k3-br-v1.0.y/board/spacemit/k3/prepare_img.sh)
- [U-Boot secure構成](https://github.com/spacemit-com/uboot-2022.10/blob/k3-br-v1.0.y/configs/k3_sec.config)
- [secure構成のブロック媒体パーティション定義](https://github.com/spacemit-com/buildroot-ext/blob/k3-br-v1.0.y/board/spacemit/k3/flash_config/sec/partition_universal.json)
- [OP-TEE K3設定](https://github.com/spacemit-com/optee_os/blob/k3-br-v1.0.y/core/arch/riscv/plat-k3/conf.mk)

リンク先は更新される。実際に使用したコミットは第3節の固定manifestとともに保存する。
