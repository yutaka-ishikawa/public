# Milk-V Jupiter 2実機情報とKeystoneファームウェアの適合性

調査日：2026-11-08（日本時間）  
照合したソース：`4011d1c9d`（shutdown修正までを含む）

## 結論

提供された`jupiter2-info.tar.gz`により、実機のCPU・メモリ・S-mode割り込み構成を具体的に確認できた。16 hart、24 MHzのtimebase、UART、S-mode IMSIC/APLICの主要条件は、現在のQEMU Jupiter2モデルと一致する。

一方、**現在の`fw_dynamic.bin`を既存の実機用OpenSBI領域へそのまま書き込み、正常動作すると判断できる状態ではない**。主な理由は以下のとおり。

1. 照合したローカルバイナリは532,320バイトで、実機DTと起動引数にあるOpenSBI用NOR領域393,216バイト（384 KiB）に収まらない。
2. Linuxから取得したDTBのroot compatibleは`spacemit,k3-pico-itx`のみで、現在のOpenSBI K3 platform overrideの対応文字列と一致しない。OpenSBIに最初に渡されるDTBが同じかは未確認。
3. 実機はX100とA100の混成であり、現在のSMが固定している16 PMPエントリ等の前提を、両種類のCPUのM-mode実測値で確認できていない。
4. 工場出荷側のLinuxはSstcを利用している。今回のSMが必要とするM-modeタイマー経路への変更、M-mode IPIによる全hartのPMP更新、実機のRPMI電源断は未実行。

OpenSBIのベースがMilk-Vと同じK3ブランチであることはユーザーから確認済みである。K3固有の初期化を一から実装し直す話ではない。ただし、ブランチの一致はコミット・設定・起動用DT・Keystone追加部分・書き込み形式の一致を意味しない。

本調査は既存の実機ログとDTBの静的照合であり、改造ファームウェアを実機で起動した検証ではない。

## 入力資料と記録の保存方針

アーカイブには次の6ファイルが含まれていた。

| ファイル | 確認できる内容 |
| --- | --- |
| `running.dtb` | Linuxへ渡されたフラットなDTB。151,680バイト。 |
| `cpuinfo.txt` | 各hartのCPU種類、ISA、MMU、実装ID。 |
| `lscpu.txt` | 16 CPUのオンライン状態、CPU種類、周波数等。 |
| `uname.txt` | 実機Linuxの版。 |
| `dmesg.txt` | Linux起動、SBI機能検出、タイマー、割り込み、予約メモリ、NORパーティション。 |
| `iomem.txt` | Linuxが認識しているアドレス範囲。 |

アーカイブのSHA-256：

```text
d6185ab4ebbf5e7016e803c71f8dc48e3c0e74be11276a7901940256b7fd2dd3
```

DTBのSHA-256：

```text
19151092f33a1a0b340c21ba3aa88d6494e6726ebb9f30f802db758f38f006a8
```

将来のチャットでも参照できるよう、主要な実機値と各入力ファイルのハッシュを[機械可読のインベントリ](jupiter2-hardware-inventory.json)にも保存した。元のログ・DTBにはボードの個体情報やブート媒体の識別子等が含まれるため、それらの全文はこの文書へ転載していない。以下の抜粋とインベントリには動作条件の照合に必要な値を残した。

## 確認できた実機構成

### ボード、Linux、OpenSBI

| 項目 | 観測値 |
| --- | --- |
| Linuxが表示するモデル | `SpacemiT K3 Pico ITX` |
| DT root compatible | `spacemit,k3-pico-itx`（単独） |
| Linux | `6.18.3-generic`、ビルド`#1.0.5.4` |
| Linuxのビルド用コンパイラ | Bianbu GCC 15.2.0 |
| Linuxのビルド用リンカー | Bianbu GNU Binutils 2.46 |
| SBI仕様 | 2.0 |
| SBI implementation | ID `0x1`、Version `0x10006`（OpenSBI 1.6に対応） |
| 検出されたSBI機能 | TIME、IPI、RFENCE、SRST、DBCN、HSM |

現在のソースのOpenSBI版も1.6である。ただし、SBIの版番号から正確なベンダーコミットや設定までは特定できない。また、上記GCC・リンカーの版はLinuxについての表示で、OpenSBIのビルドツールの版ではない。

### CPU：16 hartだが同一種類ではない

| hart ID | CPU | `marchid` | `mimpid` | MMU |
| --- | --- | --- | --- | --- |
| 0–7 | Spacemit X100 | `0x8000000058000002` | `0x33d8a600` | Sv39 |
| 8–15 | Spacemit A100 | `0x8000000041000002` | `0x10000000d5686200` | Sv39 |

全hartの`mvendorid`は`0x710`。`lscpu`のオンラインCPUは0–15で、Linuxログにも`Brought up 1 node, 16 CPUs`がある。

提供された`cpuinfo.txt`では16 hartの`isa`文字列は同一であり、Sstc、Smaia、Ssaia、Svpbmt等を含む。ただし、同じISA表示からPMPの個数・粒度・初期配置・PMAやキャッシュの動作まで同一と判断することはできない。

現在のQEMU起動は全hartに同じ`rva23s64` CPUモデルを指定しており、X100/A100の混成そのものを再現していない。

### メモリと予約領域

Linuxへ公開されるメモリ範囲は次のとおり。

```text
base          0x102000000
size          0x7fe000000
end inclusive 0x8ffffffff
```

`iomem.txt`も`102000000-8ffffffff : System RAM`を示す。DTにはそれより下にRCPU、RPMI、M-mode等の予約領域がある。

| DTノード・用途 | 先頭 | サイズ |
| --- | --- | --- |
| `mmode_resv0` | `0xd4282000` | `0x1000` |
| `mmode_resv1` | `0x100000000` | `0x100000`（1 MiB） |
| `mmode_resv5` | `0x100800000` | `0x400000`（4 MiB） |
| RPMI共有領域`rpmi0` | `0x100e00000` | `0x4000`（16 KiB） |

`mmode_resv1`という名前とアドレスはファームウェア配置の手掛かりだが、これだけで実際のOpenSBIロード先やその領域全体の所有者を断定しない。RCPU・表示・CMA等の予約領域もあるため、Linuxへ見えるDRAMの先頭だけを基準に実機ファームウェアを配置してはいけない。

工場出荷環境のログには`rcpu1_runtime`と`mmode_resv5`の予約範囲の重複警告もある。今回のKeystone修正による警告ではなく、提供された既存環境の記録である。この警告だけから新しい障害を断定していない。

### タイマーと割り込み

| 項目 | 実機DT・ログ | QEMUモデルとの照合 |
| --- | --- | --- |
| timebase | 24,000,000 Hz | 一致 |
| CLINT | `0xe081c000`、サイズ`0x4000`、`riscv,clint0` | 基本アドレス・サイズ一致 |
| S-mode IMSIC | `0xe0400000`、サイズ`0x400000` | 一致 |
| IMSIC ID | 1 hartあたり511 IDs、63 guest IDs | 一致 |
| IMSIC索引 | hart-index-bits=4、guest-index-bits=6 | 一致 |
| S-mode APLIC | `0xe0804000`、サイズ`0x4000`、512 sources | 一致 |
| UART | `0xd4017000`、サイズ`0x100`、IRQ 42 | 一致 |
| UARTアクセス | reg-shift=2、reg-io-width=4 | 一致 |

ログには以下がある。

```text
riscv-imsic: interrupt-controller@e0400000: providing IPIs using interrupt 1
sched_clock: 64 bits at 24MHz
riscv-timer: Timer interrupt in S-mode is available via sstc extension
```

これは既存LinuxのS-mode側の動作証拠である。SMが使うM-mode IPIの配線、M-modeタイマー割り込み、全hartのPMP更新応答を確認した証拠ではない。Linux用DTBにはM-mode IMSIC/APLICのノードが見当たらないが、OpenSBIのfixup後のDTでは削除される場合があり、実機に存在しないという結論にはならない。

## 現在のファームウェアとの差分・未解決条件

### 1. NORのOpenSBI領域に現在のraw binが収まらない

DTには次のパーティションがあり、Linux起動引数の`384K@1728K(opensbi)`とも一致する。

```dts
opensbi@1b0000 {
    reg = <0x1b0000 0x60000>;
};
uboot@210000 {
    reg = <0x210000 0x5f0000>;
};
```

| 対象 | サイズ |
| --- | --- |
| 既存OpenSBI領域 | `0x60000` = 393,216バイト |
| 照合したローカル`fw_dynamic.bin` | 532,320バイト |
| 超過分 | 139,104バイト |

照合したファイルはSMトレースを有効にしてビルドしたもので、SHA-256は以下。

```text
668bfa1a0c57351456117001f7f8b413825eaff7d8c2ce44ee2e0d6810caceed
```

このraw binを既存NORレイアウトの領域へ単純に書く方法は成立しない。領域外まで書くと、隣接するU-Boot領域に入る。FITヘッダー等を追加する場合は、その実際のコンテナサイズも評価する必要がある。

今後の候補は、サイズ削減、対応する圧縮・ロード経路、またはブートチェーンと整合した領域変更である。いずれもFSBL等の読み込み方法を確認してから選ぶ。トレースを無効にした最終版についても別途実サイズを測り、収まると推測しない。

### 2. OpenSBIへ最初に渡されるDTBとroot compatible

現在の`opensbi/platform/generic/spacemit/spacemit_k3.c`の対応文字列は次の二つ。

```text
spacemit,k3
riscv-spacemit
```

一方、提供された`running.dtb`のrootには次しかない。

```dts
model = "SpacemiT K3 Pico ITX";
compatible = "spacemit,k3-pico-itx";
```

`fw_platform_lookup_special()`はrootのcompatibleを照合してplatform overrideを選ぶ。**このDTBをそのまま現在のOpenSBIへ渡した場合、K3 overrideは選ばれない。** その場合はCCI、クラスタ起動等だけでなく、K3 overrideに登録されたSstc制御・DT fixupも呼ばれない。

ただし、今回のDTBはLinux側で採取したものであり、OpenSBIが先に受け取ったDTBと同一かは分からない。U-Boot等がLinux用DTBを別途渡す構成もある。実際のOpenSBI入力DTBを確認する前に、compatibleの追加だけで問題を解決したとは判断しない。

リポジトリ内の`k3-pico-itx.dts`は`spacemit,k3-pico-itx`に加えて`spacemit,k3`も指定しており、提供された実行中DTBとはこの点でも差がある。

### 3. PMP前提と混成CPU

現在のSMビルドは以下を指定する。

```text
-DKEYSTONE_K3_PMP_N_REG=16
-DK3_QEMU_PMP16=1
```

`platform.h`もPMP数16を固定し、SM側の証拠検査はQEMU向けに各hartのPMP数が16であることを要求する。全16 hartのオンライン状態や共通のPMP割り当てにも依存する。

今回の情報にはPMP CSRの個数・粒度・アドレス幅・配置・ロック状態・`mseccfg`・ベンダーPMACFGの実測値がない。X100とA100の双方について、M-modeで採取した値を確認する必要がある。Linuxの`/proc/cpuinfo`やオンラインCPU数からは代替できない。

### 4. Sstcと固定オンラインポリシー

工場出荷側のLinuxはSstcを利用している。現在のKeystone構成はSMのプリエンプションをM_TIMERで扱うため、K3 overrideでSstcへのアクセスを無効にし、Linuxへ渡すISA情報からも取り除く設計である。

この変更後に、実機LinuxのタイマーがSBI TIME経路へ切り替わり、enclave実行中にも必要なタイマー割り込みが届くかは未確認。また、Keystone構成ではhart STOP/SUSPENDを拒否するため、工場出荷側と同じCPU hotplug・省電力動作を保証するものではない。

### 5. shutdown／rebootは実機のデバイスで確認する

実機にはRPMI共有領域、SBI MPXY mailbox、RPMI regulator・RTC・power keyを使う記録があり、SBI SRSTも検出されている。これは既存ファームウェアでRPMI系サービスやSBIリセット機能が提供されている根拠になる。

ただし、Linux DTBには`riscv,rpmi-system-reset`、`syscon-poweroff`、`syscon-reboot`のノードは見当たらない。OpenSBI入力DTBが別なら、そこに定義されている可能性がある。既存環境で実際に電源断・再起動が成功するかも、この採取ログだけでは確認できない。

今回のshutdown修正は、QEMUのsysconアドレスをハードコードせず、登録されたリセットデバイスを呼ぶ。そのため実機でもRPMI等を選べる構造だが、実機のリセット先ファームウェアが全hartを止めない終了経路を受け付けるかは実動作で確認する必要がある。

### 6. 実機のロード形式と配置

リポジトリの`opensbi/platform/generic/spacemit/fw_dynamic.its`はOpenSBIのload/entryに`0x100000000`を指定する。一方、QEMUモデルのDRAM先頭は`0x102000000`であり、両者を混同しない。

照合したファームウェアELFは位置独立の`DYN`、リンク上の`_fw_start=0`、`_fw_end=0x88000`だった。この範囲は静的なファームウェア範囲の手掛かりであり、scratch、スタック、ヒープ、DTや後続のロード物も含めた実機配置の正当性を証明するものではない。実機FSBL等のロード先・確保サイズ・`fw_dynamic`引き渡し条件・FIT形式を照合する必要がある。

## 次に必要な情報と検証順序

今回の6ファイルで分かった事項を再収集する必要はない。追加で重要なのは次である。

1. **実際に動いているOpenSBIのコミット・defconfig・ビルドコマンド、およびOpenSBI入力DTB。** Linux用DTと初期化用DTの差、K3 override、RPMI・M-mode割り込みノードを確定する。
2. **電源投入直後からLinuxまでのシリアルログ。** 今回の`dmesg.txt`はLinux以降で、OpenSBIバナー、PMP情報、選択されたreset/IPI/timerデバイスは含まれない。
3. **FSBL・OpenSBI・U-Bootのパッケージ構造とロード条件。** 384 KiBの制約、圧縮対応、実ロード先、実際に読み込むサイズとコンテナ形式を確定する。
4. **X100/A100各hartのPMP・PMACFG等のM-mode診断。** ブートhartだけでなく両CPU種類と全hartの割り当てを確認する。既存コードには読み取り診断用の`K3_PMP_DIAG_READONLY`や`K3_A1_DIAG_PMP_SNAPSHOT`があるが、採取方法はベンダーの正常ブート経路と照合して用意する。
5. **実機での段階的実行確認。** まずベンダー基盤と配置を合わせたLinux起動、全hart起動、M-modeタイマー/IPI、helloの実行・終了・繰り返し、各CPU種類への固定実行、最後にshutdown/rebootを確認する。

アプリが動くことと、PMP・DMAを含むenclaveの隔離保証は別の検証である。本報告は移植・起動の適合性を評価したもので、後者の保証を追加するものではない。

## 関連ソースと既存の調査記録

- [実機インベントリ](jupiter2-hardware-inventory.json)
- [QEMU Jupiter2モデル](../qemu/hw/riscv/jupiter2.c)
- [QEMU定数](../qemu/include/hw/riscv/jupiter2.h)
- [OpenSBI platformの選択](../opensbi/platform/generic/platform.c)
- [OpenSBI K3 override](../opensbi/platform/generic/spacemit/spacemit_k3.c)
- [実機向けFITテンプレート](../opensbi/platform/generic/spacemit/fw_dynamic.its)
- [Keystoneのビルド定数](../keystone/sm/tools/k3_rv64_compile_audit.py)
- [Keystoneプラットフォーム定数](../keystone/sm/src/platform/spacemit_k3/platform.h)
- [PMP情報の検査](../opensbi/include/sbi/k3_pmp_partition_evidence.h)
- [Eyrie入口の修正](keystone-eyrie-entry-build-id.md)
- [システム終了の修正](keystone-system-reset.md)

## Zkr乱数拡張の追加調査

公式資料・K3 CPU定義・実機情報からZkrの利用可能性は確認できていない。
[調査結果と実機での確定方法](jupiter2-zkr-investigation.md)を参照。
