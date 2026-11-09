# hello-native

`keystone/examples/hello-native`由来の、libcを使わずOCALLで文字列を表示する例。
ソースは`eapp/eapp_native.c`、runner入口は`host/host_native.cpp`。

ホスト：`make -C samples/hello-native`。
前提SDK/Eyrie、QEMUの起動、共有マウントは[全体README](../README.md)を参照。
使用runtimeは`.build-keystone/examples/hello-native/`。
`-nostdlib`とSDKの`keystone_eapp_start`を明示してgpを初期化する。

ゲストで展開先に移動した後の実行コマンド：

```sh
./hello-native-runner hello-native eyrie-rt loader.bin 0
```

末尾の0は期待するenclave終了値。SDKエラーや不一致はrunnerも非0で終了する。
全体実行は`bash /mnt/host/samples/run-guest.sh --basic`。
