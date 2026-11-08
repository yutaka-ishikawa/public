# QEMU上のOpenSBI・Keystone SMの検証方法

`verifications` はQEMUシステムゲストで実OpenSBI・Keystone SMへecallする
統合試験だけに変更した。以前の製品C関数の抽出試験、模擬PMP/CSR/IPI・SBI、
ホスト上の模擬試験プログラムと過去の結果ファイルは削除した。

[現在のビルド・実行手順と結果](../verifications/README.md)を参照。

```bash
# ホスト：SDKを現在のソースから作る
bash scripts/build-install-keystone.sh --only sdk
make -C verifications guest
# 起動するImageと一致する完全なカーネル出力を使う
make -C verifications kernel-modules KERNEL_OUT="$PWD/.build-k3/linux"
```

```bash
# QEMUゲストのrootシェル
mount-hostshare
cd /mnt/host/verifications
bash run-all.sh
```

`run-all.sh` を開発用ホストで実行するとエラーにする。
クロスビルドやQEMU user modeでプログラムを起動するだけでは統合試験を実行したとしない。
`make` の既定動作はゲスト用バイナリのビルドで、試験実行ではない。

## 実際の確認経路

正常系はLinux runner→Keystoneドライバ→ecall→OpenSBI→SM→Eyrie/enclave→
OCALL STOP→runner→RESUME→enclave→EXIT→DESTROYを通す。
2 MiBと256 MiBのfree memory設定で、データ8往復と不正return拒否を検査する。
DESTROYの戻り値も明示的に検査する。

異常系は検証専用カーネルモジュールから実ecallし、CREATEの不正layout、
DESTROY/RUN/RESUMEの無効ID、未定義2004への実応答を検査する。

PMP試験はOCALL中にゲストのLinux S-modeから物理EPMのRAM direct mapを読む。
UTMと通常RAMを読めることを正の対照とし、EPMだけが拒否されることを確認する。
`--data-only` はこの直接参照試験だけを省き、実SBI・SM実行は省略しない。

## 結果の扱い

ビルド成功、モジュールオブジェクト生成、実ゲストのPASSを分けて報告する。
新しい全試験のゲスト実行は未確認。従来の2 MiB版edgecallでユーザーが確認した
8往復PASSは、新しい全試験や256 MiB版までのPASSに拡張しない。

この試験でも全hart・タイマー割り込み・競合・全異常系は未網羅。
実機固有のPMP、IPI、メモリ配置の確認は実機で別途行う。
