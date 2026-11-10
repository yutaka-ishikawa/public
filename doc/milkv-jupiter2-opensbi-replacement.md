# Milk-V Jupiter2：EDK2を維持したOpenSBIの置換手順

作成日：2026-11-11（日本時間）

## 1. 対象と検証範囲

販売元提供の `itb.tar.gz` と `bin.tar.gz` を解析した構成を対象とする。実機での書き込み・起動テストは未実施。本書は実機の起動媒体・配置を確認してから実施する手順であり、出荷ロットすべてへの適用を保証するものではない。

既存の `doc/` 内のMarkdown文書を確認した。実機の適合性調査やビルド手順はあるが、OpenSBI単独の置換・復旧を通した手順書がないため本書を追加した。

FedoraのK3 Pico-ITX資料は、SpacemiT K3 Pico-ITXとMilk-V Jupiter 2を同一基板の販売名として扱っている。ただし実機のリビジョン、ストレージ、ファームウェアの世代は個別確認する。[S1]

目的は `opensbi` 領域の `fw_dynamic.itb` だけを交換すること。`uboot` 領域の `edk2.itb`、FSBL、bootinfo、ESOS、EC、OSは更新対象にしない。eFuse操作も不要。

## このリポジトリのKeystone版に関する先決条件

[既存の実機適合性調査](jupiter2-hardware-compatibility.md)では、照合時のKeystone入り `fw_dynamic.bin` は532,320バイトで、実機NOR領域393,216バイトを超えている。**その成果物は本書のNOR単独置換手順に使用できない。** FIT化するとさらに大きくなる。現在のビルドを再計測し、サイズ削減等が完了するまでは第7章・第8章の書き込みへ進まない。領域拡張や圧縮の有効化は、隣接配置・FSBLの対応を含む別作業である。

また、同調査にあるOpenSBI入力DT、K3 override、X100/A100のPMP、Sstc、IPI、RPMIの未確認事項は残る。本書の配布物解析でロード形式を補足しても、それらの実機互換性を確認したことにはならない。

リポジトリの通常ビルドはルートで `make opensbi JOBS=4`。現在のMakefileの生成先は `.build-k3/firmware/build-opensbi-k3-keystone/platform/generic/firmware/`。これはOpenSBI＋SMの生成であり、実機NORへの書き込みではない。`make install-opensbi` もホスト側への配置である。[ビルド・installの説明](make-build-install.md)を参照する。第5章の一般的なビルド例でこのSMリンク工程を代替しない。

FITテンプレートは既に [opensbi/platform/generic/spacemit/fw_dynamic.its](../opensbi/platform/generic/spacemit/fw_dynamic.its) にあり、今回の添付形式と主要設定が一致する。候補ディレクトリへコピーして使用できる。

## 2. 添付ファイルから確認した構成

想定実行順は BootROM → FSBL（U-Boot SPL）→ OpenSBI → EDK2（UEFI）→ EFIローダー → OS。FSBLがOpenSBIとEDK2をロードし、OpenSBIが後段へ実行を渡す構成と整合するが、実際のロード元・引き渡しはUARTログで確認する。

| 項目 | 添付で確認した値 |
| --- | --- |
| OpenSBI FIT | `fw_dynamic.itb`、271,311 bytes |
| 内部本体 | `fw_dynamic.bin`相当、270,552 bytes |
| ロード／エントリー | `0x100000000` |
| FITの本体ノード | `/images/opensbi` |
| 本体ハッシュ | CRC32、`ec98567e` |
| 本体SHA-256 | `33072ac8216a74981ed36785a7d515f3c3cadcab3cef8d775972c28e2b9e6a9c` |
| EDK2ロード先 | `0x102000000` |
| UART DT設定 | `uart0:115200` |

添付の配置は以下の2種類。実機がどちらを使うかはJSONの存在だけでは判定できない。

| 媒体／JSON | OpenSBI開始オフセット | 領域サイズ |
| --- | --- | --- |
| SPI NOR／`partition_4M.json` | 1728 KiB = `0x1b0000` | 384 KiB = 393216 bytes |
| GPTブロック媒体／`partition_universal.json` | 7 MiB = `0x700000` | 1 MiB = 1048576 bytes |

どちらも `uboot` パーティションに `edk2.itb` を指定する。`partition_flash.json` は `u-boot.itb` を指定する別の書き込み用構成であり、UEFI維持のために全体適用してはいけない。

`partition_4M.json` という名前からフラッシュ容量を判断しない。添付の末尾は5.0625 MiBに達する。

FSBLにはRSA-2048/SHA-256署名が2つあり、ファイル内公開鍵で検証成功。一方、後続FITには署名ノードがない。これだけではeFuseの状態、実機の検証強制、UEFI Secure Bootの有効状態は分からない。

## 3. 作業前の準備

LinuxホストPC、データ通信可能なUSBケーブル、基板に合った電源、UARTコンソールを用意する。UARTの信号電圧・配列は基板資料で確認する。端末は115200/8N1、フロー制御なし。電源投入直後からログを保存する。

ホストで必要なコマンド：`fastboot`、`mkimage`、`dumpimage`、`dtc`、Python 3、必要ならRISC-Vクロスコンパイラ。Debian/Ubuntuでは通常 `fastboot u-boot-tools device-tree-compiler gcc-riscv64-linux-gnu` が該当する。パッケージはホストの管理手順に従って導入する。

ホストに次の作業ディレクトリを作る。

```sh
set -e
mkdir -p jupiter2-opensbi/original jupiter2-opensbi/candidate
cd jupiter2-opensbi
```

販売元の同一リリースの `FSBL.bin`、`u-boot.itb`、`edk2.itb`、`fw_dynamic.itb`、`esos.itb`、bootinfo全種、JSON、可能なら完全な配布アーカイブを `original/` に保管する。RAM上の書き込み用U-Bootには、EDK2ではなく `u-boot.itb` が必要。

```sh
set -e
sha256sum original/* > original.sha256
dumpimage -l original/fw_dynamic.itb
```

## 4. 実機の起動媒体と配置を確定し、バックアップする

実機のLinux上で以下を取得する。コマンドの出力とUARTログをホストにも保存する。

```sh
set -e
cat /proc/mtd
lsblk -o NAME,PATH,TYPE,SIZE,PARTLABEL,MOUNTPOINTS
cat /proc/cmdline
sudo dmesg > dmesg-before.txt
```

`opensbi` というMTD名やGPT名があっても、それが現在使用中とは限らない。UARTのFSBLロード表示、MTDのオフセット、起動媒体設定と照合する。SPI NORとUFSの両方にコピーがある場合は、使用中のコピーを特定するまで書き込まない。OSのルートデバイスがUFSでも、ファームウェアはNORという構成がある。

### 4.1 SPI NORの場合

`/proc/mtd` の `opensbi` に対応する実際の番号を確認して設定する。以下の `N` は実際の番号へ置換する。

```sh
set -e
MTD_NAME=mtdN
cat /sys/class/mtd/"$MTD_NAME"/name
cat /sys/class/mtd/"$MTD_NAME"/type
cat /sys/class/mtd/"$MTD_NAME"/size
cat /sys/class/mtd/"$MTD_NAME"/erasesize
cat /sys/class/mtd/"$MTD_NAME"/offset
sudo dd if=/dev/"$MTD_NAME" of=opensbi-partition-before.bin bs=64K status=progress
sha256sum opensbi-partition-before.bin
```

`offset` が公開されないカーネルでは、DT・起動ログ・MTD配置から確認する。この手順では `type=nor`、開始 `0x1b0000`、サイズ393216を想定する。NANDにはこの書き込み手順を適用しない。バックアップをホストへコピーし、ファイルサイズが領域全体と一致することを確認する。

可能ならNOR全体も、マスターデバイスが公開されていればその読み出し、そうでなければ全MTDパーティションの個別読み出しで保存する。書き込み用Linuxツールを使う場合は `mtd-utils` が必要。

### 4.2 UFSなどGPTブロック媒体の場合

`lsblk` とパーティション表で `opensbi` を特定する。`/dev/sda` などの番号を固定しない。対象ディスクに対する `sudo sfdisk --dump /dev/実際のディスク` も保存する。

```sh
set -e
OPENSBI_DEV=/dev/disk/by-partlabel/opensbi
readlink -f "$OPENSBI_DEV"
sudo blockdev --getsize64 "$OPENSBI_DEV"
sudo dd if="$OPENSBI_DEV" of=opensbi-partition-before.bin bs=64K status=progress
sha256sum opensbi-partition-before.bin
```

同名パーティションが複数あればby-partlabelは使わず、ディスクの識別情報とPARTUUIDから一意に指定する。添付構成ではサイズ1048576、開始7 MiB。バックアップをホストへ保存する。

### 4.3 既存ファイルとの対応確認

バックアップの先頭271311バイトが販売元 `fw_dynamic.itb` と同じか、ホストで比較する。

```sh
set -e
cmp -n "$(stat -c %s original/fw_dynamic.itb)" \
    original/fw_dynamic.itb opensbi-partition-before.bin
```

不一致なら別リリースや別のロード元の可能性がある。既存領域からFITを取り出して解析し、同じ構成を確認するまで置換を進めない。

## 5. 交換用OpenSBI本体を用意する

最初の置換には、現在の販売元リリースと対応するK3 SDKのOpenSBIソース・設定・ツールチェーンを使う。ビルドの前にソースのコミットと変更差分を保存する。

```sh
set -e
git rev-parse HEAD > opensbi-source-commit.txt
git diff > opensbi-local.patch
```

対応SDKのビルド方法が利用できる場合はそれを優先する。一般的なK3向けビルドの形は、OpenSBIソースディレクトリで以下。[S2]

```sh
set -e
make -j"$(nproc)" O=build-k3 \
    CROSS_COMPILE=riscv64-linux-gnu- \
    PLATFORM=generic PLATFORM_RISCV_ISA=rv64gc \
    FW_TEXT_START=0x100000000
```

`CROSS_COMPILE` は対応SDKのツールチェインprefixへ置換できる。このコマンドだけで任意のOpenSBIソースがK3に対応するわけではない。K3対応、ESOS/RPMI連携、使用中のカーネルとの互換性が必要。生成物は `build-k3/platform/generic/firmware/fw_dynamic.bin`。

上流向け資料には `spacemit-com/opensbi-upstream` の利用例もあるが、メーカー独自機能やベンダーカーネルとの互換性は別途確認が必要。[S3] 上流版を使う実験と、同一SDKでの小変更を区別する。

UARTが消える問題にも注意する。K3向け上流化資料では、OpenSBIに渡すUART DTのcompatibleに `spacemit,k1-uart`、`intel,xscale-uart`、`ns16550` をこの順序で指定する対処が説明されている。必要なDTやFSBLの更新が生じる組合せは、本書のOpenSBI単独置換の範囲外。[S2]

## 6. 添付と同じ単独FIT形式へ包装する

`fw_dynamic.bin` をそのまま `opensbi` に書かない。添付FSBLが期待する `fw_dynamic.itb` を作る。対応SDKの `fw_dynamic.its` があれば、そのテンプレートを優先する。ない場合、今回解析したFIT構成に合わせて以下を `candidate/fw_dynamic.its` として保存する。

```dts
/dts-v1/;
/ {
    description = "Configuration to load OpenSBI before U-Boot";
    #address-cells = <2>;
    fit,fdt-list = "of-list";
    images {
        opensbi {
            description = "OpenSBI fw_dynamic Firmware";
            data = /incbin/("fw_dynamic.bin");
            type = "firmware";
            os = "opensbi";
            arch = "riscv";
            compression = "none";
            load = <0x1 0x0>;
            entry = <0x1 0x0>;
            hash-1 { algo = "crc32"; };
        };
    };
    configurations {
        default = "config_1";
        config_1 {
            description = "opensbi FIT config";
            firmware = "opensbi";
        };
    };
};
```

ホストで本体をコピーし、FITを作る。以下のソースパスは実際のビルド先に置換する。

```sh
set -e
cp /path/to/opensbi/build-k3/platform/generic/firmware/fw_dynamic.bin candidate/
(cd candidate && mkimage -f fw_dynamic.its fw_dynamic.itb)
dumpimage -l candidate/fw_dynamic.itb
dtc -I dtb -O dts candidate/fw_dynamic.itb -o candidate/fw_dynamic.generated.dts
sha256sum candidate/fw_dynamic.bin candidate/fw_dynamic.itb > candidate.sha256
```

ロード先・エントリー先が `0x100000000`、firmwareが `opensbi`、CRCが付いていることを確認する。NORではITB全体が393216バイト以内、UFSの添付構成では1048576バイト以内であることが必要。

まず変更していない販売元OpenSBI本体を同じITSで再包装し、抽出本体の一致を確認すると、包装工程を切り分けられる。ただしファイルの整合性確認は実機互換性の証明ではない。

## 7. SPI NORをfastbootで更新する主手順

この章は、実機が添付のNOR配置で起動することを確認済みの場合に限る。UFS起動には第8章を使う。

### 7.1 BootROMから、書き込み用U-BootをRAM上に起動する

1. FDLボタンを押したままリセットするか、給電する。
2. FDLを離し、ガイドで指定されたデータ用Type-CとホストPCを接続する。必要な電源は別途確保する。
3. UARTで `ROM: usb download handler` を確認する。
4. ホストで以下を実行する。[S1][S3]

```sh
set -e
fastboot devices
fastboot stage original/FSBL.bin
fastboot continue
```

UARTでSPLのDDR初期化が終わり、USBが再認識されるのを待って次を実行する。複数台接続時は各fastbootコマンドに `-s シリアル番号` を指定する。

```sh
set -e
fastboot stage original/u-boot.itb
fastboot continue
```

RAM上のU-Bootがfastboot要求を受け付けることを確認する。通常プロンプトで停止した場合、対応するベンダーU-Bootでは `fastboot 0` で開始する例がある。[S2] コマンド未対応なら書き込みを進めない。

`stage` はRAM転送であり、この時点でEDK2領域をU-Bootに置き換える操作はしていない。

### 7.2 NOR配置の選択とOpenSBIだけの書き込み

ベンダーK3の `flash mtd` は通常のfastbootと異なる専用操作。配置選択／設定のためにJSONを送る。実機の既存NOR配置と添付JSONが一致していることが前提で、再分割の目的では使わない。[S2]

```sh
set -e
fastboot flash mtd original/partition_4M.json
fastboot getvar partition-size:opensbi
```

サイズ照会が対応していれば `0x60000`（393216）であることを確認する。未対応の応答は容量確認成功ではない。事前に取得した実機のMTD配置と、RAM上U-Bootが選択した媒体・配置を確認できない場合は停止し、第8章のLinux経由を検討する。

```sh
set -e
fastboot flash opensbi candidate/fw_dynamic.itb
```

`OKAY`を確認する。`uboot`、`fsbl`、`bootinfo`、`env`、`esos`、`mtd`以外の全体イメージ、ECは追加書き込みしない。`mtd`へのJSON転送は上記の配置選択に限る。

fastbootの標準機能にreadbackを前提としない。対応するLinux／U-Bootの読み出し機能があれば再起動前に照合する。なければ次の起動後に第9章の読み戻し確認を行う。

```sh
set -e
fastboot reboot
```

電源投入直後からUARTログを保存する。

## 8. 実機Linuxから対象領域だけ更新する代替手順

第4章で起動元と対象デバイスを特定し、領域全体のバックアップをホストへ退避済みであること。以下はJupiter2のLinux上で実行する。新しいFITを先に `candidate/fw_dynamic.itb` に転送し、ホストとSHA-256が同じことを確認する。各ブロックはエラーが出た時点で停止する。

### 8.1 SPI NOR：flashcpを使用

`MTD_NAME=mtdN` のNを確認した実番号にする。実機のMTDがNORであり、この領域がOpenSBIであることが条件。

```sh
set -e
MTD_NAME=mtdN
test "$(cat /sys/class/mtd/"$MTD_NAME"/name)" = opensbi
test "$(cat /sys/class/mtd/"$MTD_NAME"/type)" = nor
test "$(stat -c %s candidate/fw_dynamic.itb)" -le "$(cat /sys/class/mtd/"$MTD_NAME"/size)"
sudo flashcp -v candidate/fw_dynamic.itb /dev/"$MTD_NAME"
sync
```

MTDには通常のファイルシステムのような上書きができないため、`dd`による直接書き込みではなく、消去・検証を行う `flashcp` を使う。読み取り専用なら原因を確認し、強制解除はしない。

### 8.2 GPTブロック媒体：opensbiパーティションだけ書く

以下の対象が実機で使用中のOpenSBIパーティションであることを確認する。ディスク全体やESPは指定しない。

```sh
set -e
OPENSBI_DEV=/dev/disk/by-partlabel/opensbi
test -b "$OPENSBI_DEV"
test "$(stat -c %s candidate/fw_dynamic.itb)" -le "$(sudo blockdev --getsize64 "$OPENSBI_DEV")"
sudo dd if=candidate/fw_dynamic.itb of="$OPENSBI_DEV" bs=64K conv=fsync,notrunc status=progress
sync
```

上記ブロックはBashで実行する。`set -e` により検査失敗時は停止する。エラーが出たら原因を解消するまで後続の書き込みを実行しない。

## 9. 読み戻しと動作確認

Linuxから対象領域を読み戻し、FITの長さ分を比較する。以下の変数は、実際に書いたデバイスとする。

```sh
set -e
# NOR例。GPTの場合はDEVICEを実際のopensbiブロックデバイスにする。
DEVICE=/dev/mtdN
sudo dd if="$DEVICE" of=opensbi-after.bin bs=64K status=progress
cmp -n "$(stat -c %s candidate/fw_dynamic.itb)" candidate/fw_dynamic.itb opensbi-after.bin
```

Linux上で更新した場合は一致を確認してから再起動する。fastboot更新の場合はLinuxが再び起動した後に実施する。

UARTで新OpenSBIのバージョン／固有ビルド表示、EDK2への移行、OS起動を確認する。さらにCPU起動数、再起動、電源断、ストレージ、コンソール、使用する仮想化・省電力機能を確認する。読み戻し一致だけでは、別媒体の古いOpenSBIが起動していないことは証明できない。起動ログの識別と組み合わせる。

## 10. 起動しない場合の復旧

### NOR fastboot経由

再びFDLでBootROMモードへ入り、第7.1節と同じ販売元FSBLとU-BootをRAMへロードする。対応するNOR配置を選び、元OpenSBIを戻す。

```sh
set -e
fastboot flash mtd original/partition_4M.json
fastboot flash opensbi original/fw_dynamic.itb
fastboot reboot
```

元ファイルと実機のバックアップが一致していなかった場合は、最初に保存した実機の領域バックアップを使う。領域全体を復元する方法は、その書き込みツールのサイズ制限と媒体を確認する。

### Linuxが起動する場合

NORなら `flashcp -v opensbi-partition-before.bin /dev/実際のmtd番号`、GPTなら `dd if=opensbi-partition-before.bin of=実際のopensbiパーティション bs=64K conv=fsync,notrunc` で元領域を戻し、読み戻し確認後に再起動する。

### GPT媒体でLinuxも起動しない場合

同一媒体を認識できる復旧LinuxをUSB等から起動し、PARTUUIDと開始位置を照合して元パーティションを復元する。事前にこの復旧LinuxからUFS等を読み書きできることを確認しておく。NOR用JSONをUFS復旧の代わりに使用しない。

FDLでUSB認識しない場合は、電源・ケーブル・ポート・ボタン操作を確認する。FSBLやeFuseを変更して復旧を試みない。

## 11. 根拠と実施上の境界

- [S1] [Fedora：SpacemiT K3 Pico-ITX / Milk-V Jupiter 2](https://fedoraproject.org/wiki/Architectures/RISC-V/SpacemiT/K3-Pico-ITX) — 同一基板の説明、RAMロード、fastboot更新例。資料全体はU-Boot運用を想定しているため、全項目をUEFI機へ適用しない。
- [S2] [SpacemiT開発者のOpenSBI K3 v4投稿](https://lists.infradead.org/pipermail/opensbi/2026-September/010600.html) — 独立FIT、ロード先、UART条件、OpenSBIのみのfastboot更新例。実機テスト対象はCOM260＋上流Linuxであり、今回のJupiter2＋EDK2組合せの動作保証ではない。本書では必要事項を要約している。
- [S3] [U-Boot：SpacemiT K3 Pico-ITX](https://docs.u-boot.org/en/latest/board/spacemit/spacemit-pico-itx.html) — 上流ビルドとUSB RAM起動。この資料のOpenSBIを含む一体型 `u-boot.itb` は、今回の販売元独立FIT構成とは異なる。生成物をそのまま混用しない。
- [S4] [CanonicalのK3ファームウェア書き込みスクリプト](https://github.com/canonical/spacemit-k3-flash-firmware/blob/main/flash-k3-firmware.sh) — NORの `opensbi=fw_dynamic.itb` と `uboot=edk2.itb` の対応。
- [S5] [K3 Pico-ITXユーザーガイド](https://docs.bit-brick.com/docs/k3_pico/pico_user_guide) — ボタン・USB接続・UART位置。
- ローカル根拠：ユーザー提供 `itb.tar.gz` / `bin.tar.gz`。FIT構成、配置、サイズ、CRC、FSBL署名はこの会話の解析結果による。

書き込み前に未確定なのは、手元実機の起動媒体・既存ファームウェア版・新OpenSBIの互換性である。これを確認できない段階では、FITの生成・静的検査・バックアップまでに留める。
