# makeでビルドとインストール

リポジトリのルートで実行します。ホスト依存パッケージを未導入の場合だけ、先に
`sudo bash scripts/install-build-deps.sh` を実行してください。

```bash
make JOBS=4
```

`make` と `make install` は同じ処理です。ソースを修正・追加した後も同じコマンドを使います。
ルートのMakefileは毎回各ビルドシステムへ処理を渡すため、ルートのstampだけを見て
ソース変更を見落としません。C/C++ファイルの追加時は、各プロジェクトの
CMakeLists.txt、Meson、Kbuild、SMのソース一覧などにも必要な登録を行ってください。
各ビルドシステムが依存関係を判定して再ビルドします。SDK/Eyrieコピーの更新、
firmware再リンクや設定確認が毎回行われるため、無変更でも処理時間はかかります。

処理対象は以下です。

1. QEMUとqemu-img → `install/keystone/bin/`
2. 通常のauto版OpenSBI/SM → `install/keystone/firmware/k3/`
3. Linux Image、DTB、カーネルモジュール → `install/keystone/boot/k3/`、`lib/modules/`
4. SDK/Eyrie、hello、hello-native → `.build-keystone/`
5. Zkr/Jupiter2/counter版firmware → `install/keystone/firmware/k3/rng-*/`
6. verificationsのゲストプログラムと検証カーネルモジュール
7. `samples/helloworld` のビルド
8. 対応するKeystoneドライバ、カーネルモジュール、hello、ゲストツールをrootfsへ反映

既存rootfsがなければBianbuベースのKeystoneイメージを作成します。
既存rootfsがあれば、ゲスト内で追加したファイルを保ったまま、上記のインストール対象を更新します。
検証プログラムとsamplesは従来どおり9pの `/mnt/host` 経由で実行します。
このmakeはゲスト内の試験を自動実行しません。

## 既存イメージを更新する場合

**実行前にQEMUを正常にshutdownし、ホストでイメージをマウントしていればアンマウントしてください。**
POSIXロックでQEMU等によるディスク使用を検出し、loopデバイスへの接続も拒否します。
独自にロックを無効化して起動したQEMUなどの使用検出は保証しません。

更新は元イメージと同じディレクトリに作る一時コピーで行います。
更新ファイルをdebugfsで取り出してSHA256を比較し、e2fsckの読み取り検査に成功してから
置き換えます。途中で失敗した場合は元イメージを残します。
一時コピー分の空き容量が必要です（対応ファイルシステムではreflinkを利用）。
元イメージのファイルシステム検査でエラーが出た場合は更新を中止します。

rootfs内の旧リリースのモジュールは自動削除しません。
ビルドに失敗した場合のインストール全体のロールバックは行いません。
rootfs更新まで完了したことを確認してから再起動してください。

## 出力先・並列数

```bash
make JOBS=8
make PREFIX="$PWD/install/keystone-dev" \
     K3_BUILD_DIR="$PWD/.build-k3-dev" \
     KEYSTONE_BUILD_DIR="$PWD/.build-keystone-dev" JOBS=4
make ROOTFS_ARCHIVE=/absolute/path/bianbu-base-26.04-base-riscv64.tar.gz
```

イメージの新規作成時だけ `ROOTFS_ARCHIVE`、`ROOTFS_SHA256` を利用します。
ビルド段階は順番に実行し、各段階内の並列数を `JOBS` で指定します。
`make -j` ではなく `make JOBS=N` を使ってください。

## 個別処理

```bash
make firmware       # 通常のauto版firmwareだけ更新
make qemu           # QEMUだけ更新
make sdk            # SDK/Eyrie/examplesだけ更新
make rng            # SDKと乱数3方式firmware・乱数検証パッケージ
make verifications  # Linux/SDKと検証コード・検証.ko
make samples        # SDKとhelloworld
make guest          # Linux/SDKを更新し、ドライバ・ツール・rootfsへ反映
make run            # インストール済みの環境を起動
```

通常のソース更新後は `make` を使えば、関連成果物までまとめて更新できます。
QEMU起動方法と統合試験は [検証手順](../verifications/README.md)、
乱数の方式別試験は [SBI 3001検証](../verifications/random/README.md) を参照してください。
