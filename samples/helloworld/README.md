# helloworld（libc版hello）

`keystone/examples/hello`に対応する既存ディレクトリ名を維持した。
`eapp/hello.c`はlibcのprintfで`hello, world!`を表示する。
`host/host.cpp`は[共通runner](../common/runner.hpp)を使い、SDKのLinux syscall
OCALL配送、終了値とDESTROYの検査を行う。

## ビルド

ホストでSDKとhello用Eyrieを準備してからビルドする。

```sh
make sdk JOBS=4
make -C samples/helloworld
make -C samples/helloworld check
```

個別MakefileからCMakeは呼ばない。SDK/Eyrieの初回準備にはCMakeを使う。
libcアプリなので`-nostdlib`は使わず、通常のlibc CRTで開始する。
必要なruntimeは`.build-keystone/examples/hello/`のeyrie-rt、loader.bin、.options_log。
IO_SYSCALL、LINUX_SYSCALL、ENV_SETUPが有効なprofileを使う。
生成物は`build/hello`、`build/hello-runner`、`build/hello.ke`。
`check`はsnapshotからのchecksum・展開検査であり、enclaveの実行ではない。

## 実行

[全体README](../README.md)の起動・共有マウント後、ゲストのrootシェルで：

```sh
bash /mnt/host/samples/run-guest.sh --basic
```

個別実行は、共通extractorで一時ディレクトリへ展開した後に行う。

```sh
work=$(mktemp -d /tmp/hello-sample.XXXXXX)
bash /mnt/host/samples/common/extract-package.sh \
    /mnt/host/samples/helloworld/build/hello.ke "$work"
(cd "$work/payload" && ./hello-runner hello eyrie-rt loader.bin)
rm -rf "$work"
```

host runnerはQEMU内のRISC-V Linuxで動くプログラムであり、開発PCでの実行ではない。
通常の鍵サービス無効profileでもこのサンプルは実行できる。
動的リンクしたrunner用のRISC-V libc・C++ libraryはBianbu rootfs側のものを使う。
利用条件は[LICENSE](LICENSE)を参照。
