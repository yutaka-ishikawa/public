# attestation / attestor

`keystone/examples/attestation`に相当する、challenge → report取得 → host検証の例。
統合版では現行SDK/report ABIに対応する。旧版との差分は
[ソース単位の説明](../../doc/keystone-examples-integration.md)を参照。

ホスト：`make -C samples/attestation`。生成物は`build/attestor.ke`。
使用runtimeはnative版。準備と共有マウントは[全体README](../README.md)。

```sh
# 開発用ホスト
make build-install JOBS=4
bash scripts/run-k3.sh --crypto qemu-dev
```

QEMUゲストで全体scriptを実行するか、展開先で以下を実行する。

```sh
KEYSTONE_PUBLIC_DIR=/mnt/host/install/keystone/share/keystone/crypto \
    ./attestor-runner attestor eyrie-rt loader.bin 0
```

runnerはLinux getrandomでchallengeを生成し、返却reportの署名、独立した公開鍵・SM hash、
ローカルの期待enclave測定値、challenge一致を検査する。
通常firmwareは3002無効なので成功しない。旧`--sm-bin fw_jump.bin`方式は使用しない。
reportは正規1352 bytesのみを送信し、API失敗時にはreportをhostへ送らない。
