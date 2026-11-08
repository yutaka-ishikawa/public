# Keystoneの検証範囲とQEMUゲストでのテスト方法

## 現在のverificationsが確認していること

`verifications/sbi-*/test.c`は、**verifications自身だけではなく、Keystone/OpenSBIの実装を検査するホスト単体試験**である。

ビルド時に現在の製品ソースから対象の関数本文をそのまま取り込み、テストが入力を用意し、実関数の出力や状態変化をassertで照合する。例えばRESUMEでは、実`resume_enclave()`、context交換、thread交換、SM helper、adapter、OpenSBI dispatcherを実行している。

一方、PMP設定API、CSRアクセス、IPI等の境界はテスト用の代替処理である。「PMP設定が成功した」「失敗した」という結果を与えて、その結果に対する実装の判断・後始末を確認する。本物のPMP CSRやCPUのecall/mretを実行する試験ではない。

| 検査するもの | ホスト単体試験での扱い |
| --- | --- |
| 状態の許可・拒否、状態遷移、thread数 | 実装のC関数を実行する。 |
| 戻り値、PC加算、レジスタ交換 | 実装のC関数で、メモリ上のフレームを操作する。 |
| PMP失敗時の判断と復旧 | 失敗を注入し、実装がエラー処理・rollbackを行うか確認する。 |
| PMP/CSRの実命令、アクセス権 | 模擬。実ハードウェアの効果は確認できない。 |
| 実割り込み、実IPI、hart間の競合 | 今回の単体試験では未検証。 |
| Cのフレーム定義と復帰アセンブリの整合 | C側の交換だけで全体の正しさを保証しない。 |
| edgecallのコピーとoffset計算 | 実SDK/runtimeコードを実行する。SBI/user-copy境界は模擬する。 |

したがって、PASSが示すのは**「用意した入力と代替処理の条件で、実装のCロジックが期待どおり動いた」**ことである。テスト側の期待値や模擬モデルにも誤り得る余地があり、移植全体の正しさの証明ではない。

**この単体試験をQEMUのLinux内でコンパイル・実行しても、PMP等を模擬する構造は変わらない。** QEMU上のCPU機能を使う統合試験にするには、以下のゲスト用プログラムを実行する。

## QEMUゲストでの統合試験とは

次の実際の経路を通す試験である。

```text
ゲストLinuxのrunner
 → Keystone driver
 → ecall
 → OpenSBI / SM
 → mretでEyrie/enclaveへ移る
 → OCALLでSTOP 3004
 → host callbackがUTM上でデータを処理
 → driverからRESUME 2005
 → enclaveの中断位置から続行してデータを確認
```

ここでのhost callbackは開発用PC側ではなく、**QEMU内のLinuxで動くrunner側**を指す。PMPはQEMUのCPUモデルが実装するものを使うため、Cの代替処理より広い範囲を確認できる。ただし、QEMUでの成功も実機のPMP・タイマー・電源管理等の保証とは別である。

## 1. データ往復とRESUMEをQEMUでテストする

### 開発用ホスト：準備とビルド

既にKeystone版の起動環境を作成している場合の手順。古いQEMUを終了してから、リポジトリルートで実行する。

```bash
git pull

# 現在のソースに合わせてSDK・Eyrieを準備
bash scripts/build-install-keystone.sh --only sdk

# SM修正も実際の起動用ファームウェアへ反映
bash scripts/build-install-k3.sh --only firmware --prefix "$PWD/install/keystone"

# ゲスト用edgecallアプリとrunnerをビルド
make -C verifications/edgecall guest

# Keystone版Linuxを起動
bash scripts/run-k3.sh
```

`--only sdk`や`make guest`だけでは起動用SMは更新されない。逆に、ファームウェアだけ更新してもゲスト用テストパッケージは生成されない。

この手順は既存のrootfsを再生成しない。QEMU・Linux Image・Keystone rootfs自体が未作成の場合は、まず通常の`build-install-keystone.sh`で起動環境を準備する。独自の`--prefix`を使っている場合は、ファームウェアのビルドとQEMU起動で同じ場所を指定する。

SDK等を別のビルド先へ出力した場合は、`make guest KEYSTONE_BUILD_DIR=/absolute/path`等で合わせる。

### QEMUゲスト内：実行

起動後のrootシェルで実行する。

```bash
mount-hostshare
cd /mnt/host/verifications/edgecall
bash run-guest.sh --data-only
echo "exit=$?"
```

このスクリプトは実際のゲスト用RISC-V runner/eappを起動する。SBI単体試験の`test.c`を実行するのではない。

正常時の主要な表示：

```text
SKIP GUEST PMP isolation: explicit --data-only mode
PASS GUEST: 8 binary OCALL round trips and invalid return rejection
exit=0
```

### この試験で確認すること

- enclaveからhostへサイズ0、1、7、8、31、255、256、4096のデータを渡す。
- NULを含むバイナリをhostが検査・変換して返す。
- RESUME後のenclaveが返却データ、元のprivate入力、返却範囲外のcanaryを確認する。
- 不正なreturn offsetをhostから返すと、runtimeが拒否する。
- 実driver・ecall・SM・Eyrie・SDKを通るOCALLのSTOP/RESUME経路を確認する。

`--data-only`は**直接のEPM物理メモリ読み取り試験を省く**指定であり、SBIやPMPのCモックを使う指定ではない。通常のenclave実行とPMP切替はQEMU内で行われる。

一方、この試験だけではSBI 2005の全17異常系、実タイマーによるプリエンプション、全hartでの動作を網羅しない。特にOCALL経由のRESUME成功を、実タイマー中断からのRESUME成功と読み替えない。

## 2. host側からEPMを参照できないことも確認する

追加のカーネルprobeが必要。開発用ホストで、**ゲストのImageと一致する完全なカーネルビルド出力**を指定する。

```bash
# デフォルト構成の場合。比較に失敗したら、そのビルド出力を使わない。
cmp install/keystone/boot/k3/Image .build-k3/linux/arch/riscv/boot/Image && \
make -C verifications/edgecall kernel-module KERNEL_OUT="$PWD/.build-k3/linux"
```

`Module.symvers`がない場合、`.ko`を生成するターゲットは失敗する。`kernel-object`で`.o`だけ生成できても、モジュールの準備・ロード・動作確認の完了とはしない。

ゲスト側で、今度はオプションなしで実行する。

```bash
cd /mnt/host/verifications/edgecall
bash run-guest.sh
echo "exit=$?"
```

正常時の主要な表示：

```text
PASS GUEST: kernel direct-map EPM read denied; UTM/control readable
PASS GUEST: 8 binary OCALL round trips and invalid return rejection
exit=0
```

OCALL中のhost callbackからprobeを呼び、通常kernelメモリとUTMは読み取れることを確認したうえで、EPM内の物理ページの読み取りだけが`-EFAULT`になることを要求する。EPMが読めた場合、対象RAMが無効な場合、probeが使えない場合は失敗になる。

これはenclaveのVAがhost processに未マップなだけで失敗する試験や、SDKがprivateポインタを拒否するだけの試験とは異なる。ただし対象はEPM先頭の代表1バイトであり、全ページ、全hart、書き込み、実行、DMAまで網羅しない。

詳細は[edgecall README](../verifications/edgecall/README.md)を参照。

## 結果の記録と現在の到達点

現在保存している77グループのPASSはホスト単体試験の結果である。ゲスト用コードはクロスビルドとprobeのオブジェクトコンパイルまで確認しているが、この文書を追加した時点で、こちらの環境では上記のQEMUゲスト統合試験を実行していない。

実行後は、使用したコミット、ファームウェア・Image・パッケージのハッシュ、起動ログ、上記テスト出力と終了コード、PMP probeを実施したかを記録する。単体試験のPASS記録とは区別して保存する。
