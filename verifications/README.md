# QEMU上のOpenSBI・Keystone SM統合試験

このディレクトリは、K3 QEMUのLinuxゲストから実際のOpenSBI・Keystone SMへ
`ecall`し、enclaveの実行と復帰を確認する試験だけを収録する。
C関数の抽出実行、模擬PMP/CSR/IPI、模擬SBIの試験とその過去ログは削除した。

## 試験経路と構成

```text
ゲストLinux runner → Keystone driver → ecall → OpenSBI → Keystone SM
 → enclave/Eyrie → OCALL STOP → Linux runner → RESUME → enclave → EXIT
 → DESTROY → Linuxへ成功復帰
```

| プログラム | 実際に検査するもの |
| --- | --- |
| `edgecall` | CREATE 2001、RUN 2003、STOP 3004、RESUME 2005、EXIT 3006、DESTROY 2002の正常経路。8種類のバイナリ往復と不正return offsetの拒否。 |
| `edgecall`の2種類のパッケージ | free memory 2 MiBと256 MiBで上記の正常経路。 |
| `sbi-errors` | S-modeの検証モジュールから実ecall。CREATEの不正レイアウト、DESTROY/RUN/RESUMEの無効ID、未定義2004の応答。 |
| `edgecall/kernel` | OCALL中、Linuxの物理RAM direct mapからEPMを読むと拒否され、UTMと通常RAMは読めること。 |

`host.cpp` のhostは開発用PCではなく、QEMUゲストLinux上のrunnerを意味する。
SBI 2004は定義済み機能ではなく、未定義番号を拒否する試験である。

## 1. 開発用ホスト：準備・ビルド

リポジトリルートで実行する。まだ起動環境がなければ最初に
`bash scripts/build-install-keystone.sh` で新規Keystoneイメージを作る。
既存イメージに新しいカーネル/ドライバ修正が必要なら先に反映する。

```bash
git pull
# 明示的DESTROY検査のため、修正版SDKも必ず再ビルドする
bash scripts/build-install-keystone.sh --only sdk
bash scripts/build-install-k3.sh --only firmware --prefix "$PWD/install/keystone"
make -C verifications guest
```

SDK/Eyrieを別ディレクトリへ出力している場合は、
`make -C verifications guest KEYSTONE_BUILD_DIR=/absolute/path` と指定する。

`make guest` はRISC-Vプログラムを作るだけで、統合試験を実行しない。
`make` の既定ターゲットもguestであり、ホスト単体試験は実行しない。

次にゲストのImageと一致する完全なカーネル出力を使って検証モジュールを作る。

```bash
cmp install/keystone/boot/k3/Image .build-k3/linux/arch/riscv/boot/Image
make -C verifications kernel-modules KERNEL_OUT="$PWD/.build-k3/linux"
```

比較失敗時は先へ進まず対応する出力に置き換える。
`Module.symvers` が必要。`.o`だけではロードできない。
2つの生成物は以下。

- `verifications/sbi-errors/kernel/keystone_sbi_verify.ko`
- `verifications/edgecall/kernel/edgecall_pmp_probe.ko`

これらは検証専用で、製品のKeystoneドライバとは別のモジュール。
起動中のファームウェアへ実際にecallし、PMPアクセスも実CPUモデルで行う。

## 2. QEMUゲスト内：全試験

ホストで `bash scripts/run-k3.sh` から起動し、ゲストのrootシェルで：

```bash
mount-hostshare
cd /mnt/host/verifications
bash run-all.sh
echo "exit=$?"
```

スクリプトがprocfs/sysfsをマウントし、必要なモジュールをロードする。
Keystoneプロファイルの既定起動引数には `cma=512M@4G-16G` を含む。
256 MiB試験の前提として `grep Cma /proc/meminfo` で512 MiB予約を確認する。
詳細は [大容量EPM](../doc/keystone-large-epm.md) を参照。

SBIエラー応答5件、2 MiB/256 MiBの各パッケージを2周しOCALL往復、
PMP参照拒否、明示的DESTROYの全成功と `exit=0` を確認する。
失敗した場合はそこで停止し、全体PASSを表示しない。

PMP参照probeを省く場合だけ明示的に：

```bash
bash run-all.sh --data-only
```

この場合も実SBI試験と実enclave実行を行う。省略するのはEPM直接参照試験のみ。
`sbi-errors` の検証モジュールはこの場合も必要。

追加モジュールをまだ用意できない場合、正常経路だけは：

```bash
cd /mnt/host/verifications/edgecall
bash run-guest.sh --data-only
```

これは全試験の完了とは区別する。

## 現在の結果と限界

ユーザーのQEMUゲストで、free memory 2 MiBのパッケージは4 MiBのEPM確保と
8回のOCALL往復・不正return拒否に成功したとの画面報告がある。
その報告は、今回追加した明示的DESTROY検査・SBIエラー試験・256 MiBパッケージ・
PMP実参照拒否までのPASSを示すものではない。

このクラウド環境ではRISC-Vビルドと検証モジュールのオブジェクト生成までを確認する。
完全な対応カーネルと稼働中K3システムゲストがないため、新しい全試験の実行結果は
**未確認**であり、コンパイル成功を実行PASSとして報告しない。

全hart、実タイマー中断、競合、各エラー経路、実機固有の動作を網羅するものではない。
各プログラムは実CPUモデルを使うが、QEMUの成功を実機での成功とは扱わない。
以前の模擬試験のグループ数・PASS表は現行の結果として引き継がない。

コンパイルとパッケージ検査の記録は [evidence](evidence/README.md) に保存する。
CIもビルド検査のみで、システムゲストの試験結果を代用しない。

256 MiB版のCREATEが100003で失敗した場合は
[TOR PMPの原因とファームウェア修正](../doc/keystone-tor-pmp.md)を参照。
