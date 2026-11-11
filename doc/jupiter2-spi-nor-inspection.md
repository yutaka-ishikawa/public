# Jupiter2 SPI NORの消去済み領域調査手順

- 記録日：2026年11月11日（日本時間）。
- 対象script：[scripts/spi-nor-erased-ranges.py](../scripts/spi-nor-erased-ranges.py)。
- 目的：保存済みMTD dumpから、全バイトが0xffの消去blockを列挙する。
- **NORへの書込み・消去・partition変更は行わない。消去状態と利用可能な空き領域は別。**
- AK設計：[jupiter2-ak-management.md](jupiter2-ak-management.md)。

## 1. 実機から読取り専用で取得

Python 3とLinuxのdd、sha256sumを使う。repositoryのrootで実行する。
MTD番号はpartition変更で変わるため、毎回nameとsizeを確認する。

```bash
mkdir -p work/nor-inspection
cat /proc/mtd | tee work/nor-inspection/proc-mtd.txt
cat /sys/class/mtd/mtd6/name
cat /sys/class/mtd/mtd6/size
cat /sys/class/mtd/mtd6/erasesize
```

今回のmtd6はnameがuboot、sizeが6225920（0x5f0000）、erasesizeが4096。
一致しない場合は以下のmtd6指定をそのまま使わず、実際のpartitionに合わせる。
更新tool等によるNOR書換えを停止した状態で取得する。

```bash
sudo dd if=/dev/mtd6 of=work/nor-inspection/uboot-partition.bin bs=4096 status=progress
sha256sum work/nor-inspection/uboot-partition.bin
stat -c '%s bytes' work/nor-inspection/uboot-partition.bin
```

ddの終了statusとsizeを確認する。読取りerrorを0xff等で埋めない。
出力先に既存ファイルがあると上書きするため、過去の採取結果は別directoryへ退避する。
dumpはfirmware／環境情報を含み得るのでローカルで管理し、GitHubへは登録しない。
不整合が疑われる場合は静止状態で再取得し、hashを比較する。

## 2. 解析

```bash
python3 scripts/spi-nor-erased-ranges.py \
    work/nor-inspection/uboot-partition.bin \
    --base-offset 0x210000 --erase-size 0x1000 --expected-size 0x5f0000
```

上記offset、erase size、expected sizeは今回のJupiter2向け既定値でもある。
scriptはregular fileを読取り専用で開き、サイズ不一致、非整列条件、不正引数を拒否する。
MTD deviceを直接渡さず、先にdumpする。他partitionへ使う場合は全parameterを変更する。

JSONで採取証跡を保存する場合：

```bash
python3 scripts/spi-nor-erased-ranges.py \
    work/nor-inspection/uboot-partition.bin --json \
    > work/nor-inspection/uboot-erased-ranges.json
```

JSONにはsize、SHA-256、解析parameter、消去済み連続範囲と末尾範囲を記録する。
終了codeは正常0、I/O error 1、不正引数・サイズ等2。
解析中に変更されないローカルdumpを入力する。

## 3. 出力の読み方

- 範囲は開始を含み、終了を含まない。
- 「区画内」はdump先頭からのoffset。
- 「NOR全体」は区画内offsetにbase offsetを加えた値。
- erase block全体が0xffの場合のみ消去済みと数える。
- 途中に0xff領域があってもboot container内padding等の可能性がある。
- 末尾の0xffも予約・更新用途を調査するまでpartition再利用を確定しない。
- 最後の非0xff byteはimageの正規サイズ／署名範囲とは限らない。

## 4. 今回ユーザーから提供された実測結果

採取時刻、dumpのSHA-256、chip型番は今回の提供情報には含まれていない。
本記録はユーザー提供のconsole出力に基づき、こちらで実機を操作・再採取した結果ではない。
今回登録するscriptは、会話で使用したblock走査処理を引数・hash・入力検査付きに整理したもの。
実機の元dumpそのものをこのscriptで再解析したとは主張しない。

```text
dev:    size   erasesize  name
mtd0: 00800000 00001000 "d420c000.spi"
mtd1: 00020000 00001000 "bootinfo"
mtd2: 00080000 00001000 "fsbl"
mtd3: 00010000 00001000 "env"
mtd4: 00100000 00001000 "esos"
mtd5: 00060000 00001000 "opensbi"
mtd6: 005f0000 00001000 "uboot"

全バイト0xffの4 KiBブロック連続範囲:
区画内 [0x24e000, 0x5f0000)  NOR全体 [0x45e000, 0x800000)  3720 KiB

末尾の消去済み領域:
区画内 [0x24e000, 0x5f0000)  NOR全体 [0x45e000, 0x800000)  3720 KiB
```

mtd0はNOR全体のmasterで、mtd1〜mtd6は同じ媒体の子partition。
全体容量にmasterと子partitionを二重加算しない。
現区画表は8 MiB全域を覆っており、未割当領域はない。

| 項目 | 結果 |
| --- | --- |
| MTD公開NOR容量 | 0x800000 = 8 MiB |
| erase単位 | 0x1000 = 4 KiB |
| uboot区画 | NOR [0x210000, 0x800000)、0x5f0000 bytes |
| 連続消去済み末尾 | NOR [0x45e000, 0x800000)、0x3a2000 = 3720 KiB |
| 末尾64 KiB候補 | NOR [0x7f0000, 0x800000)。今回の消去済み範囲内 |

読出しと解析はできている。write-enable／lock状態、erase／program成功、
耐久性、更新処理との非競合は未確認である。

## 5. AK領域を確定するまでの確認

1. 配布U-Boot image／FITの正規サイズ、signature、backup image、固定offset参照を調べる。
2. U-Boot環境変数のprimary／redundant配置を調べる。
3. 更新・recovery toolのerase／write範囲を調べる。
   uboot partition全消去とchip全消去の両方を対象にする。
4. bootloader、Linux cmdline／DT、flash layout manifestを更新し、
   NOR [0x7f0000, 0x800000)を独立したAK予約領域にする。
5. uboot imageが新上限0x5e0000 bytesを超える場合にbuild／updateで拒否する。
6. 新旧layout混在や旧recovery imageの利用を防ぐmigration手順を用意する。
7. partition確定後だけ、専用区画で書込み・readback・電源断試験を実施する。

offsetは候補であり、この手順ではflash_erase、flashcp、ddによるNOR書込みを実行しない。

## 6. scriptの確認範囲

保存済み人工データで、今回の境界、全0xff、消去済み領域なし、
途中の消去済み範囲、サイズ不一致・不正引数を確認する。
これはオフライン解析scriptの確認であり、SBI／PMP／HUK／実NORの動作試験ではない。
