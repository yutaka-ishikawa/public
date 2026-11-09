# Makefile: buildとinstallの分離

## 基本操作

リポジトリ直下で実行します。依存パッケージがなければ、先に
`sudo bash scripts/install-build-deps.sh` を実行してください。

```bash
make JOBS=4           # 全体のbuildのみ。make buildと同じ
make install          # ビルド済み成果物のinstallのみ
```

一度のコマンドで両方行う場合:

```bash
make build-install JOBS=4
```

初期版Makefileはbuildとinstallを兼ねていましたが、現在は分離しています。
`make install` はbuildターゲットに依存せず、コンパイルを起動しません。
成果物がなければエラーになるので、先に必要なbuildを実行してください。
installは古い成果物の鮮度まで判定しません。ソース変更後はbuild→installの順に使います。

## 個別のbuildとinstall

| コンポーネント | buildコマンド | installコマンド | 内容 |
| --- | --- | --- | --- |
| QEMU | `make qemu` | `make install-qemu` | system emulator、qemu-img、QEMUデータ |
| OpenSBI＋SM | `make opensbi` | `make install-opensbi` | SMをコンパイルしてOpenSBIにリンクした通常auto版firmware |
| Keystone SMのみ | `make sm` | 単独installなし | OpenSBIにリンクする `sm-combined.o`。起動用firmwareは `make opensbi` で作る |
| Linux | `make linux` | `make install-linux` | Image、DTB、config、カーネルモジュール |
| Eyrie＋SDK | `make eyrie` | `make install-eyrie` | SDK、Eyrie、hello/hello-nativeのruntimeとパッケージ |
| Keystone | `make keystone` | `make install-keystone` | SM、SDK/Eyrie、対応Linuxを準備し、ドライバとゲストツールをビルド。installでrootfsに反映 |
| 乱数3方式 | `make rng` | `make install-rng` | Zkr/Jupiter2/counter版firmware。build時に乱数検証パッケージも作る |
| 3002/3003開発profile | `make crypto` | `make install-crypto` | QEMU開発鍵firmware、公開pin/measurement、専用verification |
| verifications | `make verifications` | `make install-verifications` | ゲスト検証プログラムと検証用カーネルモジュール |
| samples | `make samples` | `make install-samples` | `samples/Makefile`からhello／native／attestor／testsをビルド |

`make sdk` はSDK/Eyrie/examplesのbuild、`make firmware` は `make opensbi` の別名です。
綴りの別名として `make erie` / `make install-erie`、
`make openSBI` / `make install-openSBI` も使えます。
`make guest` は `make keystone` の別名で、buildだけを行います。

個別buildでは必要な依存コンポーネントもbuildします。
たとえばverificationsの.koには完全なLinuxビルドが必要なため、
`make verifications` はLinuxとSDK/Eyrieを先にビルドします。
既存のSDKスクリプトではEyrieとexamplesをまとめて処理するため、
`make eyrie` はSDK/examplesも処理します。

Keystone SMは独立した起動イメージではなく、OpenSBIへリンクされます。
SMの変更を起動へ反映する場合は次の順です。

```bash
make opensbi JOBS=4
make install-opensbi
```

Eyrie変更後にrootfs内のhelloも更新する場合:

```bash
make keystone JOBS=4
make install-eyrie install-linux install-keystone
```

`install-keystone` はrootfs更新です。firmwareの更新は `install-opensbi` で行います。
カーネルImageのSHA256と、ドライバbuild時に記録したImageのSHA256が一致しなければ
rootfsへの反映を拒否します。

## build成果物とinstall先

buildは最終インストール先や既存rootfsを更新しません。
cryptoの開発device rootだけは、再build間で保存し9p共有を避けるため、
ホストのprivate stateディレクトリに保存します。詳細は
[crypto verification](../verifications/crypto/README.md) を参照してください。
既存のbuild-installスクリプトの出力も、build用のstageへ向けています。
SDKの `cmake --install` は開発用SDKをbuildディレクトリ内へ配置する処理です。

| 用途 | デフォルトの場所 |
| --- | --- |
| QEMU/Linux/通常firmwareの作業領域 | `.build-k3/` |
| SDK/Eyrie/driver/乱数firmwareの作業領域 | `.build-keystone/` |
| インストール前の成果物 | `.build-keystone/stage/` |
| rootfsへ反映するファイル群 | `.build-keystone/guest-overlay/` |
| verification/sampleのbuild | 各ディレクトリの `build/` と検証.ko |
| 最終インストール先 | `install/keystone/` |

install先の主な構成:

```text
install/keystone/
├── bin/                         QEMU、qemu-img等
├── boot/k3/                     Linux Image、DTB、config
├── firmware/k3/                 通常firmwareとrng-*別firmware
├── lib/modules/                 ホスト側に配置するゲスト用kernel modules
├── rootfs/keystone-rootfs.img    ゲストイメージ
├── sdk/                         開発用SDK
├── runtime/{hello,hello-native}/ Eyrie、loader、runtime設定記録
└── share/keystone/
    ├── examples/                hello/hello-nativeパッケージ
    ├── verifications/           ソース・build成果物・実行スクリプト
    └── samples/                 ソース・build成果物
```

verificationsとsamplesは従来の9p `/mnt/host` からも実行できます。
installしたディレクトリもホスト側の成果物です。rootfs内へ検証一式をコピーする処理ではありません。
build/installだけではゲスト試験を実行しません。

## 既存rootfsの更新

**install前にQEMUを正常shutdownし、ホストでマウントしていればアンマウント・loop切断してください。**
rootfsがなければinstall時にBianbuから新規作成します。rootfs作成はファイルシステムの
パッケージ処理であり、コンパイラを起動しません。初回はアーカイブをダウンロードする場合があります。

既存rootfsは、同じディレクトリに作る一時コピーで更新します。
対象は対応kernel modules、Keystone driver、hello、ゲストツールです。
追加済みの他のゲストファイルは保持します。debugfsで取り出した更新ファイルのSHA256と
e2fsckの検査に成功したら置き換えます。更新失敗時は元イメージを保持します。

一時コピー分の空き容量が必要です（対応FSではreflinkを使用）。
POSIXロックとloop接続を検査しますが、独自にロックを無効化したQEMU等の検出は保証しません。
元のFSに検査エラーがあれば更新を中止します。旧リリースのモジュールは自動削除しません。
install全体のロールバックは行いません。すべての必要なinstallが成功してから起動してください。

## 変数と依存関係

```bash
make build JOBS=8
make build PREFIX="$PWD/install/dev" K3_BUILD_DIR="$PWD/.build-k3-dev" \
     KEYSTONE_BUILD_DIR="$PWD/.build-keystone-dev" JOBS=4
make install PREFIX="$PWD/install/dev" K3_BUILD_DIR="$PWD/.build-k3-dev" \
     KEYSTONE_BUILD_DIR="$PWD/.build-keystone-dev"
make install ROOTFS_ARCHIVE=/absolute/path/bianbu-base-26.04-base-riscv64.tar.gz
```

buildとinstallで同じディレクトリ設定を指定してください。
`ROOTFS_ARCHIVE` と `ROOTFS_SHA256` は新規rootfs作成時だけ利用します。
工程は順番に処理し、工程内の並列数は `JOBS` で指定します。
`make -j` ではなく `make JOBS=N` を使ってください。

ルートは毎回各buildシステムへ処理を渡します。依存するソース変更は各buildシステムが判定します。
新しいC/C++ファイルを追加した場合は、該当するCMakeLists.txt、Meson、Kbuild、SMソース一覧等にも
必要な登録を行ってください。設定確認・firmware再リンク等は無変更時も行うため、即時終了とは限りません。

## 確認範囲

Makefile経由のOpenSBI/SM、SDK/Eyrie、samplesのbuildと、最終PREFIXが作られないことを
確認しました。一時PREFIXへのOpenSBI、SDK/runtime、samplesのinstallも実施し、
元の成果物とSHA256を比較しました。実ext4を使ったrootfs更新・失敗時保持・使用中拒否も確認済みです。対応する完全なLinux成果物が作業環境にないため、
全コンポーネントを通したbuild/installと更新後のゲスト起動は未確認です。
統合試験は [verification手順](../verifications/README.md)、
乱数方式別試験は [SBI 3001検証](../verifications/random/README.md) を参照してください。

FID 4000のSDK・driver・firmware更新にも`make build-install`を使う。
`make verifications`は`sbi-4000`のguest packageとkernel probeも生成し、
`make install-verifications`はそれらも配置する。
既定のK3 firmwareではmultimemが有効。無効profileの作り方と個別試験は
[FID 4000 README](../verifications/sbi-4000/README.md)を参照。

## samplesへのexample統合

`make sdk`はSDKとhello/nativeのEyrie profileを準備し、`samples/Makefile`で全アプリを作る。
アプリだけの変更後は`make -C samples`、個別なら`make -C samples/attestation`等でよい。
`make install-samples`は共通runner・実行script・README・4パッケージもまとめて配置する。

Eyrie作業ディレクトリは`.build-keystone/sample-runtimes/{hello,hello-native}/{src,build}`。
従来の`.build-keystone/examples/{hello,hello-native}`には互換用runtime/packageを置く。
元の`keystone/examples/`のCMakeアプリビルドは通常workflowから呼ばない。
[samples手順](../samples/README.md)と[互換性差分](keystone-examples-integration.md)を参照。
