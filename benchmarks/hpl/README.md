# Jupiter2 / K3：HPL 2.3

Jupiter2 32 GiBモデルの実機上でビルド・実行するための一式です。
公式HPL 2.3のソースパッケージを`vendor/`に同梱しています。
初回の`make`でMPICH 4.3.2とOpenBLAS 0.3.34を取得・検証・ビルドし、HPLをリンクします。
MPI通信部分とOpenBLASは静的リンクです。OSのlibc/libmは動的リンクします。
MPIのヘッダー・開発パッケージをシステムへ別途インストールする必要はありません。
グローバルなライブラリ設定やシステムのOpenBLASは変更しません。

## ビルド済みファイル

MPIを別途インストールせず実行する場合は、[配布archive](dist/hpl-jupiter2-riscv64.tar.gz)を使用してください。
[HPL本体だけのELF](dist/xhpl-riscv64)も用意しています。
手順・依存関係は[配布説明](dist/README.md)を参照。
MPIとOpenBLASは静的リンク、OSの共有ライブラリ依存はlibc.so.6のみ（GLIBC 2.38以降）。

## 1. 必要環境と使い方

Bianbu / Ubuntu系、RISC-V GCC、GNU Make、同梱ビルドするMPICH、Python 3.12以降を推奨。
Python 3.11でも`tarfile.data_filter`がセキュリティ更新で導入済みなら使用できます。
Cコンパイラだけを使用し、FortranコンパイラやLAPACKは必要ありません。

This product includes software developed at the University of Tennessee, Knoxville, Innovative Computing Laboratory.

```sh
sudo apt-get update
sudo apt-get install -y build-essential python3
cd benchmarks/hpl
make doctor
make JOBS=4       # HPL + local OpenBLASのビルド。実行はしない
make plan         # 実行時メモリから最大Nを計算して表示
make config       # .build/run/HPL.dat と plan.jsonを生成
make smoke        # N=1536、同じ8-rank構成で残差チェック
make run          # 空きメモリを再確認し、最大Nで本測定
```

すべて`make -C benchmarks <target>`でも実行できます。
MPIは通常ユーザーで実行します。root実行を自動許可する処理は入れていません。
初回のMPICH・OpenBLAS取得にはHTTPS接続が必要です。取得元とハッシュは後述します。
実行は1ノード専用です。SSH設定や複数ホスト起動は行いません。

## 2. 既定パラメーターと根拠

| 項目 | 値 | 根拠・意味 |
|---|---|---|
| HPL | 2.3、FP64 | Netlib公式パッケージ |
| BLAS | OpenBLAS 0.3.34 | RISC-V向け既知不具合修正を含む固定版 |
| OpenBLAS target | RISCV64_ZVL256B | X100のRVV 1.0 / VLEN=256用 |
| MPI実装 | MPICH 4.3.2、ch3:sock | static library、1ノードTCP通信 |
| MPI ranks | 8 | X100のみを使用 |
| CPU_LIST | 0-7 | 指定リポジトリのJupiter2実機記録に対応 |
| P × Q | 2 × 4 | K3公開HPL例のX100×8構成 |
| NB | 192 | K3公開HPL例に合わせる |
| BLAS threads | 1、USE_THREAD=0 | MPI×BLASの過剰並列を防止 |
| N | auto | 32 GiBを上限に、実メモリから最大値を選ぶ |
| PMAP | 0、row-major | 比較用の標準構成 |
| PFACT / RFACT | Right / Crout | RISC-V P550の公開HPLログと同じ |
| NBMIN / NDIV | 4 / 2 | 同上 |
| BCAST / DEPTH | 1ringM / 1 | 同上 |
| SWAP / threshold | mixed / 64 | 同上 |
| L1 / U | transposed / transposed | 同上 |
| EQUIL / ALIGN | yes / 8 doubles | 同上 |
| 残差判定 | 16.0、必須 | 計算が正しいことを確認 |

K3の公開例はBPI-SM10、N=12000、NB=192、2×4、X100×8です。
本設定はそのNB・grid・rank数を採用し、Nだけを32 GiB向けに拡大します。
他ボードの52 GFLOP/s等をJupiter2の保証値とは扱いません。

旧Milk-V Jupiter/K1・Orange Pi RV2やP550の公開結果との比較用には：

```sh
make run N=12000                 # K3公開例と同じ問題サイズ
make run NB=256                  # 他RISC-V例で使われるNB
make run P=1 Q=8 NB=256          # gridも変更して測定
```

N、NB、grid、BLAS、コンパイラ、周波数が違う結果を、CPUだけの速度差と解釈しないでください。

## 3. 32 GiBでの「最大サイズ」

`N=65536`の行列だけで32 GiBを消費するため、その値を既定にすることはできません。
OS、MPI、BLAS、右辺・解ベクトル、panel/lookahead領域が別に必要です。

このパッケージでは、**定義した予約量と保守的なメモリ見積もりの範囲で、
ブロック分散が均等になる最大のN**を選びます。実測による絶対最大値の保証ではありません。

予算は次のように決めます。

```text
total = min(32 GiB, MemTotal, 認識できたcgroup v2上限)
budget = min(total - 2 GiB, MemAvailable - 1 GiB)
step = NB × lcm(P, Q)                 # 既定では768
N = 見積使用量がbudget以下となる最大のstepの倍数
```

見積もりには、HPL 2.3 `HPL_pdtest.c`の各rankの実際の配列確保式
`8 × (ALIGN + (ld+1) × (nq+1))`と、`HPL_pdpanel_init.c`のpanel領域を含めます。
DEPTH+1個分のpanelを保守的に見積もり、MPI/BLAS等に1 rankあたり32 MiBを加えます。
`ld`のalignmentと2のべき乗を避ける処理も反映しています。

理想的にMemTotal/MemAvailableとも32 GiBある場合の例：

| 値 | 既定構成での計算例 |
|---|---|
| budget | 30 GiB |
| 最大N | **61440** |
| 行列本体 A と右辺の理論量 | 約28.125 GiB |
| 各rank確保・panel・余裕込み | 約29.437 GiB |
| 次の均等サイズ N=62208 | 約30.158 GiBとなり予算を超える |

実機ではファームウェア予約等でMemTotalが32 GiB未満になります。
ブラウザー等の使用量も考慮するため、実際の自動Nはこれより小さくなる場合があります。
**Jupiter2自身で実行した`make plan`の値が、その実行時点の設定値**です。
ホストPCで生成したHPL.datをそのまま実機へ持ち込む必要はありません。
`make run`は実行直前に再計算し、専用の結果ディレクトリへHPL.datを生成します。

`make config`の生成物や同梱の`HPL-32GiB.dat`は確認用です。runはそれらの手修正を読みません。
手動指定は`make run N=... NB=... P=... Q=...`で行います。
手動Nもメモリ予算を超える場合は拒否します。
実行中に他プロセスがメモリを消費すれば、開始時の見積もり内でもOOMは起こり得ます。

予約量を変える場合（既定値を推奨）：

```sh
make plan RESERVE_GIB=3 AVAILABLE_RESERVE_GIB=2
```

Nを約6万にすると計算量はO(N^3)となり、N=12000の例より大幅に時間がかかります。
本測定は`make smoke`の残差PASSを確認してから行ってください。

## 4. X100/A100の混在を避ける

MPIの各rankは、**xhplをexecする前**に指定CPUへ固定します。
その後でOpenBLASがロード・初期化されます。
Python wrapperの`HPL_BIND local_rank=... cpu=...`をログへ残します。
同梱MPICHを使い、各rankにCPU_LISTの1 CPUを割り当てます。
別のOpen MPIランチャーをこのMPICHバイナリと組み合わせないでください。
指定CPUがcpusetやOSの制限で使えなければ、そのまま失敗させます。

K3のA100はVLEN=1024ですが、stock OpenBLASのZVL256B kernelには
大きいVLENでの移植性問題が報告されています。単にCPU_LISTを0-15に増やす構成は採用しません。
本runnerも、stock ZVL256BビルドではCPU 0-7以外への実行を拒否します。
A100専用kernelや異種backendの混成最適化は本パッケージの対象外です。

`CPU_LIST`はLinuxの論理CPU番号です。実機記録と違うCPU配置なら、
`/proc/cpuinfo`や`lscpu`を確認してから設定を見直してください。

CPU governorは自動変更しません。比較実験ではCPU0/CPU8のpolicy、周波数、冷却を揃えてください。

## 5. 結果とPASS条件

`results/<UTC timestamp>/`に以下を保存します。Gitには入れません。

- `HPL.dat`：実際に使った問題サイズとパラメーター
- `plan.json`：メモリ観測値、予算、N、見積もり、CPUリスト
- `manifest.json`：コマンド、CPU情報、governor・周波数、MPI・コンパイラ情報、バイナリSHA-256、終了状態
- `HPL.log`：全rankのbindingとHPL出力
- `result.json`：成功時の秒数・GFLOP/s・scaled residual

MPIの終了コード0だけでは成功としません。指定したN/NB/P/Qの結果行、
有限な正の時間・GFLOP/s、16.0未満の残差PASSED、1件成功・0件失敗・0件skip、
全rankのCPU bindingを確認します。NaNや残差失敗、skipを性能結果として採用しません。

## 6. ソース・依存物・クリーン

- HPL 2.3は`vendor/hpl-2.3.tar.gz`に無改変で同梱。
- HPLの著作権・ライセンスは`vendor/HPL-COPYRIGHT`とアーカイブ内に保持。
- HPL SHA-256：`32c5c17d22330e6f2337b681aded51637fb6008d3f0eb7c277b163fadd612830`
- OpenBLAS 0.3.34：公式release assetを取得。release APIが公開するdigestと照合済み。
- OpenBLAS SHA-256：`cd7e129868320cc2d033afa920e31202dfe0b8066a5b66661900ccc0f197dfed`
- OpenBLASはsingle-thread、LP64、DYNAMIC_ARCH=0、NOFORTRAN/NO_LAPACK/NO_LAPACKE。
- MPICH 4.3.2は公式tarballを取得し、SHA-256 `47d774587a7156a53752218c811c852e70ac44db9c502dc3f399b4cb817e3818`で固定。
- MPICHは`--disable-shared --enable-static --with-device=ch3:sock`。MPIランチャーとproxyも同梱ビルド。
- HPLはCBLAS、`-O3`。fast-mathや残差チェック無効化は追加しない。
- `.build/`だけに展開・インストール。`make clean`はvendorとresultsを保持。
- targetやコンパイラ等を変更した場合は、混在を避けるため`make clean`が必要。

OpenBLASのアーカイブをあらかじめ用意する場合：

```sh
make OPENBLAS_ARCHIVE=/path/to/OpenBLAS-0.3.34.tar.gz
```

これは公式release assetと同じSHA-256を持つファイルを指定します。
GitHubの自動生成source archiveは内容・hashが違うため使えません。

## 7. 開発環境での検証

```sh
make test
# 実Jupiter2ではないホストのビルド/配線確認だけを行う場合：
make
make smoke ALLOW_HOST=1 CPU_LIST=0-1 P=1 Q=2
```

CPU_LISTはそのホストで許可されたCPUへ変えてください。
非riscv64ホストではOpenBLAS GENERICでビルドし、実行はALLOW_HOST=1が必要です。
ホスト上の小規模残差PASSを、Jupiter2の性能・実機PASSとは扱いません。
Jupiter2の32 GiB最大N実行は、ここでの開発検証だけでは確認できません。

## 8. 参照

調査日：2026-11-09。

1. [Netlib HPL 2.3](https://www.netlib.org/benchmark/hpl/)、[FAQ](https://www.netlib.org/benchmark/hpl/faqs.html)。
2. [K3公開HPL例の解説：NB=192、2×4、X100×8、N=12000](https://www.open-riscv.com/blog/openblas-zvl1024b-spacemit-k3-a100-hpl-heterogeneous.html)。
3. [SpacemiTフォーラムの元投稿](https://forum.spacemit.com/t/topic/1684)。
4. [OpenSolversのRISC-V HPL結果](https://www.opensolvers.com/apps/hpl.html)。
5. [P550の公開HPLログ：NB=256等](https://github.com/geerlingguy/top500-benchmark/issues/56)。
6. [top500-benchmarkのHPL.datテンプレート](https://github.com/geerlingguy/top500-benchmark/blob/master/templates/HPL.dat.j2)。
7. [OpenBLAS 0.3.34 release](https://github.com/OpenMathLib/OpenBLAS/releases/tag/v0.3.34)。
8. [EESSI：RISC-V HPLのNaNとOpenBLAS修正](https://www.eessi.io/docs/blog/2026/07/12/risc-v-x60-openblas-hpl/)。
9. [指定リポジトリのJupiter2実機記録](../../doc/jupiter2-hardware-compatibility.md)。

ハードウェア・ライブラリの既知問題への対応と、ユーザーのKeystone/IMA実装は別の作業です。

## ビルド済みファイルの検証・再作成

[RISC-V/QEMUでの確認範囲とログ](tests/evidence/README.md)を参照。
Jupiter2実機の8-rank・最大Nは未実行です。

Jupiter2上のnative buildから同じ形式のarchiveを作る場合：

```sh
make JOBS=4
make package
```

クロスビルドにはRISC-V GCCとsysrootを別途用意し、例えば以下のように指定します。

```sh
make HPL_PROFILE=riscv64 BUILD_CC=riscv64-linux-gnu-gcc \
  CROSS_HOST=riscv64-linux-gnu AR=riscv64-linux-gnu-ar \
  RANLIB=riscv64-linux-gnu-ranlib OPENBLAS_TARGET=RISCV64_ZVL256B
make package HPL_PROFILE=riscv64 BUILD_CC=riscv64-linux-gnu-gcc \
  CROSS_HOST=riscv64-linux-gnu AR=riscv64-linux-gnu-ar \
  RANLIB=riscv64-linux-gnu-ranlib STRIP=riscv64-linux-gnu-strip \
  OPENBLAS_TARGET=RISCV64_ZVL256B
```

`HPL_PROFILE`は`.build/`内のビルド分離用です。配布物内build.jsonにコンパイラ版・target・ソースhashを保存します。
MPICHはライブラリをstatic-onlyでビルドし、HPLのリンク時に`-static-mpi`で明示します。
OSのlibcまでの完全static化は行いません。
通信backendはMPICH ch3:sockなので、Open MPIや共有メモリ最適化したMPIの公開値との差にはMPI実装の差も含まれます。

MPICH公式ソース：https://www.mpich.org/static/downloads/4.3.2/mpich-4.3.2.tar.gz
