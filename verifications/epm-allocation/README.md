# EPM連続メモリ確保の検証（4 MiB、256 MiB）

## 対象と変更

EPMはSMに渡すCPU物理アドレス上で連続している必要がある。
256 MiBを通常のbuddy allocatorだけで確保することはできない。
今回、次を修正した。

- Linux 6.8以降の `MAX_PAGE_ORDER` を有効な最大orderとして含める。
  566ページ要求はorder 10、4 MiBとしてbuddyへ渡せる。
- buddyで確保できない場合は `dma_alloc_pages()` を使う。
  現K3のDMA-direct実装はCMAから物理連続ページを確保できる。
  `dma_alloc_coherent()` の別仮想マッピングをEPMとして使う方式をやめる。
- 32 bit DMA maskと `__GFP_DMA32` 制約を廃止し64 bit maskを設定する。
- SMへ渡すCPU物理アドレスは `page_to_phys()` から取得する。
  DMAアドレスは別に保存し、対応する `dma_free_pages()` へ渡す。
- 確保失敗時のEPM構造体をゼロ初期化し、解放時も状態をクリアする。
- FINALIZE前の解放では、unsignedのEIDに格納した `-1` を明示比較し、
  未作成SM enclaveのDESTROYを呼ばずにメモリを解放する。
- KeystoneのQEMU起動時だけ `cma=512M@4G-16G` を既定で指定する。
  Bianbuと明示的な `--append` の内容は変更しない。

現RISC-Vカーネルは既定CMA予約の上限を4 GiBにするが、QEMU K3のRAMは
0x102000000以上にある。単なる `cma=512M` では予約に失敗する可能性がある。
明示的な4–16 GiBの探索範囲内でmemblockが空きRAMを選ぶようにした。
アドレスを固定してそのまま上書きする指定ではない。

512 MiBの予約は256 MiBのfree memoryにruntime等を加えたEPMを想定する。
同時に複数の大容量enclaveを保証する容量ではない。CMAは他の利用者とも共有し、
移動できないページや容量不足により失敗することがある。
QEMUの既定RAMは2 GiBで、実機の起動引数やDTは自動変更しない。
実機ではRAM配置・予約領域に合わせてCMA範囲を選ぶ必要がある。
UTMは今回変更せず、従来どおりbuddyから確保する。

## ホストでの回帰試験

```bash
bash verifications/epm-allocation/run-tests.sh
# SBI、edgecallも含む
bash verifications/run-all.sh
```

`extract.py` が実製品関数をそのまま抽出する。Linuxの確保APIとデバイスは
模擬であり、CMAが実機で予約・確保できた証明ではない。
Linux 6.7相当と6.8以降相当のマクロ条件で、それぞれ9グループを確認する。

1. 566ページ要求がorder 10のbuddyを呼ぶ。
2. ちょうど4 MiBの境界。
3. buddy失敗時のfallback、CPU PAとDMA handleを取り違えない。
4. 256 MiBでは無効なbuddy orderを呼ばず、大容量経路で確保・ゼロ化・解放。
5. 256 MiBにELF等54ページを加えた要求。
6. 両経路失敗後の安全な解放。
7. ゼロページ要求の拒否。
8. 64 bit DMA maskとモジュール初期化失敗時の後片付け。
9. FINALIZE前の解放と、SM DESTROY失敗時のメモリ保持。

大容量の模擬試験も実際に256 MiB以上のホストバッファを確保し、
全バイトのゼロ化を検査する。LinuxのCMA自体は実行していない。

## 対応するドライバの再ビルド：ホスト

使用中のImageと一致する完全なカーネルビルドと `Module.symvers` が必要。
以下は標準のビルド先の場合。

```bash
git pull
cmp install/keystone/boot/k3/Image .build-k3/linux/arch/riscv/boot/Image
test -s .build-k3/linux/Module.symvers
mkdir -p .build-keystone/driver
cp -a keystone/linux-keystone-driver/. .build-keystone/driver/
make -C linux-k3 O="$PWD/.build-k3/linux" ARCH=riscv \
  CROSS_COMPILE=riscv64-linux-gnu- M="$PWD/.build-keystone/driver" \
  KEYSTONE_SDK_DIR="$PWD/.build-keystone/sdk-install" modules
make -C verifications/epm-allocation guest
```

別のSDKやカーネル出力を使用している場合はそのパスに置き換える。
現設定は `CONFIG_CMA=y`、`CONFIG_DMA_CMA=y`。異なるカーネルなら確認する。
対応するカーネルが揃っていれば、この変更にカーネル・OpenSBIの再ビルドは不要。

## QEMUの再起動とドライバ更新：ゲスト

CMA予約にはQEMUゲストの再起動が必要。
ホストで修正版の `bash scripts/run-k3.sh` を使用して起動する。
既存rootfsを使用できる。

ゲストで共有ディレクトリをマウントし、稼働中のenclaveがない状態で：

```bash
mount-hostshare
BB=/usr/libexec/keystone/busybox
# ロード済みの場合だけ実行。使用中なら強制せず、実行中の試験を終了する。
$BB rmmod keystone_driver
$BB insmod /mnt/host/.build-keystone/driver/keystone-driver.ko
# 次回起動時にも修正版を使う
cp /mnt/host/.build-keystone/driver/keystone-driver.ko \
  /usr/lib/modules/$(uname -r)/extra/keystone-driver.ko
cd /mnt/host/verifications/epm-allocation
bash run-guest.sh
```

未ロード時のrmmodエラーを除き、ロードのエラーは解決してから試験する。
`run-guest.sh` はproc/sysをマウントし、起動引数とCMA情報を表示する。
`CmaTotal: 524288 kB` と予約成功の起動ログを確認する。

`guest.c` は実ドライバのCREATE ioctlで4/256/257 MiBを要求し、
先頭・中間・末尾ページのゼロ値と読み書きを検査し、DESTROY ioctlで解放する。
この順を3回繰り返し、解放後の再確保も確認する。

期待する最後の行：

```text
PASS GUEST EPM: 3 allocation/release cycles for 4/256/257 MiB
```

**この試験はドライバの確保・解放試験であり、SMへのFINALIZE/RUNは行わない。**

## 256 MiB free memoryでのedgecall統合試験

ドライバの確保試験に加え、SMで実行してOCALL/RESUMEする場合はホストで：

```bash
make -C verifications/edgecall clean
make -C verifications/edgecall guest FREE_MEM_MIB=256
```

ゲストで：

```bash
cd /mnt/host/verifications/edgecall
bash run-guest.sh --data-only
echo "exit=$?"
```

8往復PASSとexit=0を確認する。これは256 MiBのfree memoryを設定した
EPMでの統合試験で、全256 MiBをenclave内部から読み書きする試験ではない。
PMPの実参照拒否は従来どおり別途probe付きで確認する。

edgecallの既定free memoryは2 MiBへ戻した。通常の4 MiB割り当ても再度通る必要がある。
`FREE_MEM_MIB` を切り替える際はcleanして作り直す。

## この環境での確認結果（2026-11-08）

- 既存77グループに加え、EPM 9グループ×2条件がASan/UBSanで成功。
- 起動スクリプト5試験：CMAの既定値、Bianbuと明示appendへの非適用を確認。
- 変更したドライバ3翻訳単位：RISC-Vカーネルオブジェクトのビルド成功。
  ioctlの既存プロトタイプ不足の警告は残る。
- ゲスト確保試験：静的RISC-V実行ファイルのビルド成功。
- edgecall：2 MiB/256 MiB設定でビルド・パッケージ検査成功。
- `.ko`全体のリンク・ロードと実QEMU試験：未確認。
  この環境に完全な `Module.symvers` と起動用Imageがないため。

ログと抽出対象ハッシュは [evidence](evidence/) に保存する。
