# Keystone examplesのMakefile版

`keystone/examples/`で定義されたアプリを、リポジトリ直下の`samples/`へ統合した。
上流の`keystone/examples/`は比較用に残し、変更しない。アプリのビルドはGNU Makeを使う。
SDKとEyrie自体の準備には引き続きCMakeを使用する。

| 元の定義 | 統合先 | パッケージ |
| --- | --- | --- |
| `keystone/examples/hello` | [helloworld](helloworld/README.md)（既存名を維持） | `helloworld/build/hello.ke` |
| `keystone/examples/hello-native` | [hello-native](hello-native/README.md) | `hello-native/build/hello-native.ke` |
| `keystone/examples/attestation` | [attestation](attestation/README.md) | `attestation/build/attestor.ke` |
| `keystone/examples/tests` | [tests](tests/README.md)（9アプリ） | `tests/build/tests.ke` |

各ディレクトリにソース、LICENSE、Makefile、READMEを置く。
`common.mk`はnativeビルド、`common/runner.hpp`はゲストLinux側の共通runner。
上流の古い検証コードをそのまま複製する代わりに、現在のSDK／report ABIへ接続する。
具体的差分は[attestation互換性](../doc/keystone-examples-integration.md)を参照。

## 開発用ホストでのビルド

初回はSDKと2種類のEyrieを準備する。現在のスクリプトはその後、全サンプルをMakeで作る。

```sh
sudo bash scripts/install-build-deps.sh
make sdk JOBS=4
```

以後のアプリ編集・再ビルドでは、SDKを再ビルドせず以下でよい。

```sh
make -C samples -j4
make -C samples check
# 個別の例
make -C samples/attestation
make -C samples/tests
```

`check`はパッケージのchecksum検査であり、SMの実行確認ではない。
`KEYSTONE_BUILD_DIR`、`SDK_DIR`、個別Makefileの`RUNTIME_DIR`、`CROSS_COMPILE`で変更可能。
SDK／toolchain／コンパイルフラグを切り替える場合は先に`make -C samples clean`を実行する。
ソースとheaderは依存関係に従って再ビルドされる。

リポジトリ全体では`make samples`がSDK/Eyrie準備込みのビルド、
`make install-samples`がビルド済みソース・パッケージ・共通runner/scriptの配置を行う。
配置先は`install/keystone/share/keystone/samples/`。installではコンパイルしない。
既存hello用guest helperの互換パスも維持する。

## QEMUでの全サンプル実行

対応するfirmware、kernel、driver、rootfsを揃える。

```sh
make build-install JOBS=4
python3 samples/run-qemu.py
```

自動runnerはqemu-dev firmwareでSstc on/offを実行する。
一時qcow2 overlayを使い、元rootfsは変更しない。ログ・入力hashは`samples/results/`。
`--sstc on`等で片方だけ、`--rootfs PATH`で別イメージを指定できる。
qemu-devは開発用identityであり実機の鍵による証明ではない。

通常の鍵サービス無効firmwareでは、暗号を使わないサンプルだけを明示的に選ぶ。

```sh
python3 samples/run-qemu.py --basic
```

attestor、test-attestation、test-data-sealingはSKIPと表示する。
完全実行のPASSと区別する。`--basic`なしで無効providerを使うと失敗する。

## ゲストrootシェルで手動実行

ホストでビルド後、以下で起動する。

```sh
bash scripts/run-k3.sh --crypto qemu-dev
```

起動したQEMUのLinux rootシェルで：

```sh
mkdir -p /mnt/host
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host
bash /mnt/host/samples/run-guest.sh
```

`run-guest.sh`が/procと/sysを用意し、未ロードならdriverをロードする。
パッケージはローカルsnapshotからchecksumを検査して展開する。
変更した公開metadataを使用する場合は、起動firmwareに対応する信頼済みディレクトリを
`KEYSTONE_PUBLIC_DIR`で指定する。パッケージ内の自己申告鍵を信頼する方式にはしない。

これらは開発例。移植機能のverification一式は引き続き`verifications/`に置く。
