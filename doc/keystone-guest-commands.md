# Keystoneイメージのモジュール・ネットワークコマンド

## 原因と変更

以前のBusyBoxはinsmodとlsmodのみを組み込んでいた。
`busybox rmmod` が `applet not found` になるのはPATHではなく、
rmmod自体が未ビルドだったため。

ゲストツールのビルドとインストーラを変更し、以下を
`/usr/bin` と `/usr/sbin` の両方にコマンドとして配置する。
各コマンドは `/usr/libexec/keystone/busybox` を呼ぶ。

| 用途 | コマンド |
| --- | --- |
| モジュールのロード・解除・一覧 | insmod、rmmod、lsmod |
| 依存関係を含むロード・索引・情報 | modprobe、depmod、modinfo |
| アドレス・リンク・経路・近隣情報 | ip |
| 疎通確認 | ping、ping6 |
| その他のネットワーク確認 | ifconfig、route、netstat、hostname |

`ip` はaddress/link/route/rule/neigh/tunnelのBusyBox実装を組み込む。
iproute2版の全機能を提供するものではない。
モジュールの探索先は既存イメージと一致する `/usr/lib/modules` に設定する。

## 既存イメージへの追加

ホストのリポジトリルートで：

```bash
git pull
bash scripts/build-keystone-guest-tools.sh
```

RISC-V用のクロスコンパイラと既存のビルド依存コマンドが必要。
生成物は `.build-keystone/guest-tools/busybox`。

稼働中のQEMUゲストで：

```bash
mount-hostshare
bash /mnt/host/scripts/install-keystone-guest-tools.sh
hash -r

mkdir -p /proc
mountpoint -q /proc || mount -t proc proc /proc

command -v insmod rmmod lsmod modprobe depmod modinfo ip
lsmod
ip addr
ip route
```

このインストールはゲストrootfsへ書き込むため、通常の読み書き可能な
ディスクイメージでは次回起動後も残る。ファームウェアやカーネルの再ビルドは不要。

共有が未マウントで `mount-hostshare` もなければ、既存mountコマンドで：

```bash
mkdir -p /mnt/host
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host
bash /mnt/host/scripts/install-keystone-guest-tools.sh
```

## 新規イメージ

`bash scripts/build-install-keystone.sh` はゲストツールのビルド・配置を
呼ぶため、新規Keystoneイメージにはこれらのコマンドが入る。
既存イメージを保護する仕組みは従来どおりで、再生成する場合は別prefixを使う。
既存イメージへ追加するだけなら、上記インストーラを使う。

## ドライバ更新時の例

ビルド済みの新しいドライバが共有され、実行中のenclaveがない状態で：

```bash
lsmod
rmmod keystone_driver
insmod /mnt/host/.build-keystone/driver/keystone-driver.ko
```

未ロードならrmmodは不要。使用中のモジュールを強制解除しない。
次回起動用にドライバを置き換えた場合は索引を更新する。

```bash
cp /mnt/host/.build-keystone/driver/keystone-driver.ko \
  /usr/lib/modules/$(uname -r)/extra/keystone-driver.ko
depmod -a
modinfo keystone_driver
# 未ロードのときに使用
modprobe keystone_driver
```

insmodは指定したファイルを直接ロードする。modprobeはdepmodで作る
modules.dep等を使うため、新しいファイルを配置した後はdepmodを実行する。
カーネルとモジュールの一致が必要なのは従来どおり。
256 MiB EPMのCMA予約は別の変更であり、反映にはゲストの再起動が必要。

## 確認した範囲（2026-11-08）

- 静的RISC-V BusyBoxのクロスビルド成功。
- ビルド後の設定検査で対象13 appletの有効化を確認。
- QEMU user modeの `--list` で全13コマンドを確認。
- QEMU user modeの `ip addr` と `rmmod --help` が成功。
  `ip addr` で表示したのは試験ホストのネットワークであり、
  QEMUシステムゲストのネットワークではない。
- ステージング試験5件成功。全コマンドの配置、引数の境界を保持した転送、
  既存systemctlシンボリックリンクの安全な置換を確認。
- 実システムゲストでのモジュール解除・ロードはここでは未実行。
