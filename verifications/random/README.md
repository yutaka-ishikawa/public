# SBI 3001: 実QEMUゲストでの乱数取得検証

このテストはホストでSMのC関数を模擬実行しません。
QEMUのRISC-V CPUでLinux、ドライバ、OpenSBI/Keystone SM、Eyrie、eappを動かします。
QEMUの周辺デバイスモデルは仮想ハードウェアであり、Jupiter2実機の乱数品質を証明するものではありません。

## ビルド（ホスト）

最初に `scripts/build-install-keystone.sh` で通常のゲスト環境を構築します。
その後、リポジトリのルートで実行します。

```bash
bash scripts/build-verification-rng.sh --jobs 4
```

このスクリプトはQEMU、SDK/Eyrie、3方式のSM/OpenSBIと検証パッケージをビルドします。
既存のLinux、rootfs、ドライバは上書きしません。Linux Imageとrootfsに入っているドライバは、
同じカーネルビルドのものが必要です。

生成物:

- `install/keystone/firmware/k3/rng-{zkr,jupiter2,counter}/fw_dynamic.{bin,elf}`
- 各方式の `manifest.txt`（方式とファームウェアSHA256）
- `verifications/random/build/random.ke`

`--sdk-build-dir DIR` でSDKの出力先を変更できます。QEMUをビルド済みなら
`--skip-qemu`、方式を絞るなら `--backend counter` 等を使います。
単独の検証プログラムビルドは `make -C verifications/random guest` です。
SDK出力先を変更した場合は `KEYSTONE_BUILD_DIR=/absolute/path` を指定します。

## 手動実行

ホストで、方式ごとにQEMUを起動し直します。`--rng` はファームウェアと
Zkr拡張／WTMデバイスの有無を組み合わせて指定します。

```bash
bash scripts/run-k3.sh --rng zkr
# または --rng jupiter2 / --rng counter
```

ゲストのrootシェルで:

```bash
mkdir -p /proc /sys /mnt/host
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host
bash /mnt/host/verifications/random/run-guest.sh
```

`run-guest.sh` は必要なら `/proc`、`/sys` をマウントし、rootfs内の対応する
`keystone-driver.ko` をロードします。古いモジュールがロード済みの場合は
再起動するか、正しいモジュールに入れ替えてから実行してください。

## 自動マトリクス（ホスト）

```bash
python3 verifications/random/run-qemu-matrix.py
```

各ケースは別のQEMU起動です。既存raw rootfsをベースに一時qcow2オーバーレイを
作るため、ゲストによる変更はベースイメージに書き込みません。
同じベースイメージを他のQEMUで書き込み中に実行しないでください。
`init=/bin/bash` のrootプロンプトを検出し、9p共有をマウントしてテストを実行します。
独自のログイン処理を使うイメージにはそのまま対応しません。

`--prefix DIR`、`--rootfs FILE`、`--qemu-img FILE`、`--timeout SECONDS`、
`--case counter` 等で変更できます。ログと起動コマンド、QEMU/BIOS/kernelのSHA256、
結果は `results/` に保存します。終了コード0は全ケース成功です。
起動失敗、タイムアウト、方式表示の不一致、テスト失敗はFAILです。

| ケース | 起動時の追加指定 | ゲストの期待値 |
| --- | --- | --- |
| zkr | `--rng zkr` | 成功 |
| jupiter2 | `--rng jupiter2` | 成功 |
| counter | `--rng counter` | 成功、開発用の品質 |
| zkr-absent | `--rng zkr --zkr off` | `-38` ENOSYS |
| wtm-absent | `--rng jupiter2 --wtm-rng off` | `-38` ENOSYS |
| wtm-not-ready | `--rng jupiter2 --wtm-rng-fault not-ready` | `-11` EAGAIN |
| wtm-zero | `--rng jupiter2 --wtm-rng-fault zero` | 0値も有効な乱数値として成功 |

エラーケースを手動で動かす場合は、ゲストのスクリプトに期待errnoを渡します。
例: `bash run-guest.sh -11`。故障した方式からcounterへ自動退避して成功扱いにはしません。

## 検証範囲

1. native eappがEyrieの `getrandom` syscall経由で実際のSBI 3001を呼びます。
   0/1/7/8/9/15/16/17/32/4096バイト、非整列バッファ、前後の番兵、flags 0～3、
   不正flagsのEINVAL、不正ポインタのEFAULT、NULL/長さ0を確認します。
2. データをOCALLでLinuxホストへ渡し、XORした結果を受け取ります。
   RESUME後、元のprivateデータと返されたデータを比較します。
3. ENV_SETUPを使わないエラー専用eappで、ハードウェア不存在・ready timeout等を確認します。
   成功値の代わりにstatusを乱数として使ってしまう実装では失敗します。
4. 別のstatic libc eappで `AT_RANDOM` の存在、16バイトの受け渡しとRESUME後の保持を確認します。
   RNGエラー時はEyrieの環境初期化で停止し、eappが実行されないことを確認します。
5. 成功時・期待したエラー時ともにenclaveを明示DESTROYします。
6. `../sbi-errors` はLinuxホストからの3001を実ecallで呼び、PROHIBITEDとa1=0を確認します。
   この追加でioctl構造体が変わるため、`make -C verifications kernel-modules` で
   検証モジュールも再ビルドし、古いモジュールは取り外すかゲストを再起動してください。

暗号乱数の統計的品質、実機TRNG/PRNGのseed・reseed、安全な所有権、DMA隔離は検証対象外です。
詳細と実施済み／未実施の区別は [実装記録](../../doc/keystone-sbi-3001-implementation.md) を参照してください。
