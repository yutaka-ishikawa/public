# Makefileで開発するKeystone helloサンプル

`keystone/examples/hello`と同じCアプリとC++ runnerを、通常のGNU Makefileでビルドするサンプルです。`sample`内のビルド・パッケージ化ではCMakeを呼びません。作成済みのKeystone SDKと、`io_syscall linux_syscall env_setup`を有効にしたhello用Eyrieランタイムを再利用します。

## ファイル構成

| ファイル | 役割 |
| --- | --- |
| `eapp/hello.c` | enclave内で動くアプリ。`hello, world!`を表示します。 |
| `host/host.cpp` | ゲストLinux側で動くrunner。enclaveの初期化と実行、OCALLの受付を行います。 |
| `Makefile` | RISC-Vバイナリのコンパイル・リンクとmakeselfによるパッケージ化。 |
| `build/hello.ke` | ビルド後に生成される実行用パッケージ。 |

ソースコードは`keystone/examples/hello`からコピーしています。利用条件は同梱の[LICENSE](LICENSE)を参照してください。

ここでいう「host runner」はKeystoneのenclave外で動くプログラムの意味です。本構成ではQEMU内のRISC-V Linuxで動き、開発用PCで実行するプログラムではありません。

## 初回準備：開発用ホストで実行

リポジトリのルートで依存コマンドをインストールし、SDKとEyrieを準備します。

```bash
sudo bash scripts/install-build-deps.sh
bash scripts/build-install-keystone.sh --only sdk
```

すでに最新のSDKとhello用Eyrieをビルド済みなら、この準備は不要です。初回準備用の既存スクリプトはCMakeを使用します。アプリの編集・再ビルドでは実行する必要はありません。

前提となる生成物は次のとおりです。

```text
.build-keystone/sdk-install/include/
.build-keystone/sdk-install/lib/libkeystone-host.a
.build-keystone/sdk-install/lib/libkeystone-edge.a
.build-keystone/examples/hello/eyrie-rt
.build-keystone/examples/hello/loader.bin
.build-keystone/examples/hello/.options_log
```

Eyrieは、このリポジトリの入口配置修正を含む版を使用してください。詳細は[入口でGNU build-idを実行して停止した問題](../doc/keystone-eyrie-entry-build-id.md)を参照してください。

## ビルド：開発用ホストで実行

リポジトリのルートで実行します。

```bash
make -C sample -j4
make -C sample check
```

`sample`へ移動して`make`を実行しても構いません。デフォルトのクロスコンパイラは`riscv64-linux-gnu-gcc`／`riscv64-linux-gnu-g++`です。生成物はすべて`sample/build`に置かれます。

```text
sample/build/hello          # 静的リンクしたenclaveアプリ
sample/build/hello-runner   # SDKとリンクしたゲスト側runner
sample/build/pkg/           # パッケージに含めるファイル
sample/build/hello.ke       # 自己展開型パッケージ
```

`make check`はパッケージの整合性を検査します。enclaveの実行確認は、後述のQEMUゲスト内で行います。

別のSDKビルドディレクトリを使う場合：

```bash
make -C sample KEYSTONE_BUILD_DIR=/absolute/path/to/keystone-build
```

SDKとEyrieの場所を個別に指定する場合：

```bash
make -C sample \
  SDK_DIR=/absolute/path/to/sdk-install \
  RUNTIME_DIR=/absolute/path/to/examples/hello \
  CROSS_COMPILE=riscv64-linux-gnu-
```

`CC`、`CXX`、`CPPFLAGS`、`CFLAGS`、`CXXFLAGS`、`LDFLAGS`、`LDLIBS`、`MAKESELF`も指定できます。コンパイラやSDKの場所、ビルドフラグを変更する場合は、一度`make -C sample clean`してから再ビルドしてください。

## 実行：QEMU内のゲストLinuxで実行

動作確認済みのKeystone版QEMU環境を使います。開発用ホストのリポジトリルートで起動します。

```bash
bash run-k3.sh
```

起動後のゲストLinuxのシェルで、ホスト共有をマウントしてドライバをロードします。

```bash
mount-hostshare
if [ ! -e /dev/keystone_enclave ]; then
    /usr/libexec/keystone/busybox insmod /usr/lib/modules/$(uname -r)/extra/keystone-driver.ko
fi
```

ドライバの配置が異なる場合は、実際の`.ko`ファイルのパスに合わせてください。既存のhelloを実行してドライバをロード済みの場合は、再ロード不要です。

続いて、サンプルパッケージをゲストの`/tmp`へコピーして実行します。

```bash
cp /mnt/host/sample/build/hello.ke /tmp/sample-hello.ke
workdir=$(mktemp -d /tmp/sample-hello.XXXXXX)
bash /tmp/sample-hello.ke --target "$workdir/payload"
```

正常に実行されると、パッケージ検証・展開の表示に続いて次が表示されます。

```text
hello, world!
```

実行終了後、不要なら展開先とコピーを削除できます。停止中・実行中のプロセスがそのディレクトリを使用している間は削除しないでください。

```bash
rm -rf -- "$workdir"
rm -f /tmp/sample-hello.ke
```

毎回異なる展開先を使うため、以前の停止で残ったmakeselfの展開先と衝突しません。`--hostfs`で共有先を変更した場合は、コピー元を変更してください。QEMU内にSDKやコンパイラを入れる必要はありません。

## アプリの編集と再実行

1. 開発用ホストで`sample/eapp/hello.c`を編集します。enclaveのメモリ設定や起動処理を変える場合は`sample/host/host.cpp`を編集します。
2. `make -C sample -j4`で再ビルドします。
3. ゲストへパッケージを再コピーし、新しい展開先で実行します。

通常のアプリ編集では、ファームウェア・カーネル・rootfsの再ビルドやQEMUの再起動は不要です。Eyrieの機能・プラグインやSDK自体を変更した場合は、それらを先に再ビルドしてください。このMakefileはEyrie自体のビルドは行いません。

ビルド成果物の削除：

```bash
make -C sample clean
```

## 実行前の注意点

- x86などの開発用ホストで`hello-runner`や`hello.ke`を直接実行しても、enclaveの動作確認はできません。RISC-VのKeystoneゲストで実行してください。
- `host.cpp`は元のhelloと同じ簡易サンプルです。直接runnerを実行する場合は`./hello-runner hello eyrie-rt loader.bin`という引数が必要です。引数やSDKの戻り値を十分に検査する実装にはなっていません。
- runnerはクロスツールチェーンの標準設定で動的リンクします。既存helloと同様、ゲスト側に対応するRISC-V libc・C++ライブラリが必要です。本リポジトリのBianbuベースKeystoneイメージを前提にしています。
