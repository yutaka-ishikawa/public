# Jupiter2：llama.cpp Discussion #4167に合わせたベンチマーク

作成日：2026-11-10（JST）。対象：Milk-V Jupiter2、32 GiB、64-bit Bianbu / Ubuntu系Linux、bash。

[Discussion #4167](https://github.com/ggml-org/llama.cpp/discussions/4167)の現行指定に合わせ、Llama 2 7BのF16・Q8_0・Q4_0でpp512とtg128を測定します。
Jupiter2はX100のCPU / RVVで実行します。掲載されているAppleのMetal GPU結果との比較は、システムの推論性能比較です。CPUコア単体の優劣を示す比較にはなりません。

既存の[Qwen用手順](llama-bench-jupiter2.md)とはソース版・モデル・バッチ・Flash Attentionの設定が異なります。
Qwenのモデルは再利用せず、以下の3ファイルを新たに取得してください。MPIと外部OpenBLASは不要です。

## 1. 比較条件

| 項目 | 本手順 |
|---|---|
| llama.cpp | `c1d0e7a004015f23bc0233470b747b596f29b264`（Discussion現行指定） |
| モデル | ggml-org/Llama-2-7B-GGUF、F16 / Q8_0 / Q4_0 |
| 入力 / 生成 | `-p 512 -n 128`、独立した2テスト |
| 論理 / 実処理バッチ | `-b 2048 -ub 512`（指定版の既定値を明示） |
| KVキャッシュ | `-ctk f16 -ctv f16` |
| Flash Attention | `-fa auto`（指定版の既定値。実際の選択は出力を参照） |
| 反復 / 待機 | `-r 5 --delay 30`、warmupあり |
| Jupiter2 | X100 CPU 0–7、8スレッド、`-ngl 0` |
| Apple側との差 | 掲載手順は`-ngl 99`でGPUを利用。スレッド数やbackendも結果ごとに確認 |

Discussionには旧指定コミット`8e672ef`と別コミットの投稿もあります。比較表には必ず各結果の`build:`を記載します。
指定版を揃えても、OS・コンパイラ・backend・メモリ・冷却条件の差は残ります。

## 2. 依存パッケージとソース取得

以下はJupiter2本体で実行します。作業ディレクトリは任意ですが、既存のllama.cppとは分けます。
モデルだけで約22.75 GiB必要です。ソース、ビルド、ログ、一時ファイル用に合計30 GiB以上の空きを確保してください。
32 GiBメモリではモデルを1つずつロードします。他の大きな処理を止め、swap利用の有無を確認します。

```bash
sudo apt update
sudo apt install -y git cmake build-essential curl python3 util-linux \
  libcurl4-openssl-dev libssl-dev

mkdir -p "$HOME/llama4167"
cd "$HOME/llama4167"
git clone --depth 1 https://github.com/ggml-org/llama.cpp.git src
cd src
git fetch --depth 1 origin c1d0e7a004015f23bc0233470b747b596f29b264
git checkout --detach c1d0e7a004015f23bc0233470b747b596f29b264
mkdir -p models results
uname -m
gcc --version
g++ --version
lscpu -e=CPU,ONLINE,MAXMHZ
```

以後は`$HOME/llama4167/src`で実行します。`uname -m`は`riscv64`を想定します。
RVV 1.0 intrinsicに対応するgcc/g++とbinutilsが必要です。GCC 15以降を推奨します。
本リポジトリの実機記録に従いCPU 0–7をX100とします。配置が異なる環境はCPU番号を変更してください。
A100（8–15）や全16コアでの実行は、本手順の対象外です。

## 3. X100向けビルドとビルド情報の保存

既存の別コミットのビルドディレクトリは再利用しません。下記をまとめて実行します。
`set -e`と`pipefail`でビルド失敗時は停止し、成功した場合だけ版とバイナリハッシュを記録します。

```bash
(
set -euo pipefail
build_dir=build-4167-x100
mkdir -p results
{
  date -u --iso-8601=seconds
  git rev-parse HEAD
  gcc --version
  g++ --version
  cmake --version
  set -x
  taskset -c 0-7 cmake -S . -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DGGML_NATIVE=OFF -DGGML_CPU_ALL_VARIANTS=OFF \
    -DGGML_RVV=ON -DGGML_RV_ZFH=ON -DGGML_RV_ZVFH=ON \
    -DGGML_RV_ZICBOP=OFF -DGGML_RV_ZIHINTPAUSE=ON \
    -DGGML_RV_ZVFBFWMA=OFF -DGGML_XTHEADVECTOR=OFF \
    -DGGML_CPU_RISCV64_SPACEMIT=OFF \
    -DGGML_OPENMP=OFF -DGGML_BLAS=OFF \
    -DGGML_CUDA=OFF -DGGML_VULKAN=OFF \
    -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF \
    -DLLAMA_BUILD_SERVER=OFF -DLLAMA_BUILD_TOOLS=ON
  taskset -c 0-7 cmake --build "$build_dir" --target llama-bench -j4
  set +x
} 2>&1 | tee results/build-4167-x100.log

git rev-parse HEAD > "$build_dir/source-commit.txt"
sha256sum "$build_dir/bin/llama-bench" > "$build_dir/binary.sha256"
"$build_dir/bin/llama-bench" --help > results/llama-bench-4167-help.txt 2>&1
)
```

IMEを無効にしたRVV基準です。`GGML_NATIVE=OFF`でも、明示したRVV等のISA設定は有効です。
OpenMPを無効にしてもllama.cpp自身のスレッドによる並列処理は利用します。
ビルド時の`Killed`では`-j2`に減らします。未対応命令・intrinsicエラーはツールチェーンと実際のコンパイルフラグを確認します。
実行ファイルだけをコピーすると共有ライブラリが不足する場合があるため、ビルドツリーを維持してください。

## 4. 必要なモデルデータの取得

取得元はDiscussionと同じ[ggml-org/Llama-2-7B-GGUF](https://huggingface.co/ggml-org/Llama-2-7B-GGUF)です。
`-hf`による自動取得では取得時点の版が変わり得るため、ここでは取得元revisionを固定し、ダウンロード後にSHA-256を検証します。
モデル利用条件は取得元のモデルカードを確認してください。

| ファイル | バイト数 |
|---|---:|
| Llama-2-7B-F16.gguf | 13,478,105,632 |
| Llama-2-7B-Q8_0.gguf | 7,161,090,592 |
| Llama-2-7B-Q4_0.gguf | 3,792,015,904 |

```bash
(
set -euo pipefail
model_revision=89336df15f5c87d43a345a4830e4b6a97a0001b4
printf '%s\n' "$model_revision" > models/model-revision.txt
cat > models/SHA256SUMS <<'SUMS'
8f20162e86d65aa0f3b799e326149d6fe247eef2e2d0af5626e1942b8acb8851  Llama-2-7B-F16.gguf
62cfe8faf481dae6e84f01763fc84f1a5a4ffaaf4800a54f2ab81ea7128deae1  Llama-2-7B-Q8_0.gguf
1f85eb184e6a4e531e1419fe1a9c43dd4914ebadf4452c6b1f2cc829679fe8f4  Llama-2-7B-Q4_0.gguf
SUMS
for quant in F16 Q8_0 Q4_0; do
  file="Llama-2-7B-${quant}.gguf"
  url="https://huggingface.co/ggml-org/Llama-2-7B-GGUF/resolve/${model_revision}/${file}"
  printf '%s\n' "$url"
  curl --fail --location --retry 3 --continue-at - \
    --output "models/$file" "$url"
done
(cd models && sha256sum --check SHA256SUMS)
)
```

検証が失敗したら測定せず、該当ファイルを再取得します。別の量子化やBF16で代用しないでください。
モデル以外の入力文章・データセットは不要です。llama-benchは合成トークンによる速度測定であり、回答品質を評価する試験ではありません。

## 5. 比較用ログを残す実行スクリプト

次のブロックで`run-4167.sh`を作成します。実行するたびに専用ディレクトリを作り、既存ログを上書きしません。
モデルのハッシュ検証と環境情報の採取は速度測定の外側で行います。

```bash
cat > run-4167.sh <<'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$(realpath -- "$0")")"
expected=c1d0e7a004015f23bc0233470b747b596f29b264
build_dir=build-4167-x100
cpus=${BENCH_CPUS:-0-7}
threads=${BENCH_THREADS:-8}
[[ $threads =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid BENCH_THREADS'; exit 2; }
run_dir=$(mktemp -d "results/4167-$(date -u +%Y%m%dT%H%M%SZ)-t${threads}-XXXXXX")
exec > >(tee "$run_dir/run.log") 2>&1
snapshot() {
  date -u --iso-8601=seconds
  free -h
  for f in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor \
           /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq \
           /sys/devices/system/cpu/cpufreq/policy*/scaling_max_freq \
           /sys/class/thermal/thermal_zone*/temp \
           /sys/class/thermal/thermal_zone*/type; do
    if [[ -r $f ]]; then printf '%s: ' "$f"; cat "$f"; fi
  done
}
finish() {
  status=$?
  trap - EXIT
  set +e
  echo '=== environment after ==='
  snapshot
  printf 'exit_status=%s\n' "$status"
  printf 'run_directory=%s\n' "$run_dir"
  exit "$status"
}
trap finish EXIT
printf 'reference=%s\n' 'https://github.com/ggml-org/llama.cpp/discussions/4167'
printf 'cpu_list=%s threads=%s\n' "$cpus" "$threads"
echo '=== environment before ==='
snapshot
uname -a
cat /etc/os-release
lscpu
cat /proc/cpuinfo
for f in /sys/firmware/devicetree/base/model; do
  if [[ -r $f ]]; then tr '\0' '\n' < "$f"; fi
done
printf 'working_directory=%s\n' "$PWD"
printf 'source_commit=%s\n' "$(git rev-parse HEAD)"
git status --short --untracked-files=no
test "$(git rev-parse HEAD)" = "$expected"
git diff --quiet
git diff --cached --quiet
test "$(cat "$build_dir/source-commit.txt")" = "$expected"
sha256sum --check "$build_dir/binary.sha256"
echo '=== model provenance ==='
cat models/model-revision.txt
cat models/SHA256SUMS
(cd models && sha256sum --check SHA256SUMS)
for f in models/Llama-2-7B-{F16,Q8_0,Q4_0}.gguf; do stat -c '%n %s bytes' "$f"; done
cp -- "$0" "$run_dir/runner.sh"
cp "$build_dir/CMakeCache.txt" "$run_dir/"
cp "$build_dir/compile_commands.json" "$run_dir/"
cp results/build-4167-x100.log "$run_dir/build.log"
cp models/SHA256SUMS models/model-revision.txt "$run_dir/"
echo '=== compiler and linked libraries ==='
gcc --version
g++ --version
cmake --version
ldd "$build_dir/bin/llama-bench"
echo '=== selected environment variables ==='
for name in OMP_NUM_THREADS OMP_PROC_BIND OMP_PLACES OPENBLAS_NUM_THREADS \
            GGML_SCHED_MAX_COPIES GGML_LOG_LEVEL; do
  printf '%s=%s\n' "$name" "${!name-<unset>}"
done
echo '=== benchmark help: defaults ==='
"$build_dir/bin/llama-bench" --help
cmd=(taskset -c "$cpus" "$build_dir/bin/llama-bench"
  -m models/Llama-2-7B-F16.gguf
  -m models/Llama-2-7B-Q8_0.gguf
  -m models/Llama-2-7B-Q4_0.gguf
  -p 512 -n 128 -b 2048 -ub 512
  -ctk f16 -ctv f16 -fa auto
  -ngl 0 -t "$threads" -r 5 --delay 30 -o md)
echo '=== exact command ==='
printf '%q ' "${cmd[@]}"; printf '\n'
echo '=== benchmark output: stdout + stderr ==='
"${cmd[@]}"
SCRIPT

bash -n run-4167.sh
bash run-4167.sh
```

標準出力・標準エラーの両方が端末と`results/4167-日時-t8-固有文字/run.log`に残ります。
失敗時も終了コードをログ末尾に記録し、スクリプトも同じコードで終了します。
ログ内の完全なコマンドには、モデルパス、CPU固定、全ての明示設定が入ります。省略設定は同じログ内の`--help`と固定ソース版で確認できます。
CMakeCache・コンパイルコマンド・ビルドログ・実行スクリプトのコピーも測定ディレクトリに保存します。
環境変数は比較に関連するものだけを記録します。認証トークン等を含む環境全体のdumpは行いません。
温度と周波数は実行前後の採取であり、実行中のピークやスロットリング不在を保証しません。

コア単体測定は次のコマンドです。同じ3モデルについて1コアの結果を別ログに保存します。

```bash
BENCH_CPUS=0 BENCH_THREADS=1 bash run-4167.sh
```

## 6. 結果の読み方と比較時に残す情報

各モデルについて`pp512`と`tg128`の計6行が出ることを確認します。値はtokens/s、`±`は反復の標準偏差です。
`pp512`は入力処理、`tg128`は逐次生成であり、2つは独立した試験です。
`build:`が`c1d0e7a`に対応すること、CPU backend、threads、実際のFlash Attention設定も確認します。
`exit_status=0`は実行成功を示しますが、生成内容の正しさを示すものではありません。

| 比較表に入れる項目 | 記録元 |
|---|---|
| マシン、メモリ、OS、CPU、周波数、温度 | run.logの環境情報、必要なら手動で冷却・電源条件を追記 |
| ソースcommit、backend、threads、FA、バッチ、KV型 | run.logの版・コマンド・結果 |
| モデルrevisionとSHA-256 | run.logおよびSHA256SUMS |
| pp512とtg128の平均・標準偏差 | 各モデルの結果行 |
| コンパイラとビルド設定 | run.log、build.log、CMakeCache.txt、compile_commands.json |
| 比較相手 | Discussionの投稿リンク、モデル、commit、GPUの有無 |

まず同じ量子化同士で比較し、F16とQ4_0を混ぜないでください。
Apple側が同じコミットでも、過去のモデルファイルと今回のファイルのハッシュが一致しない場合はその差を注記します。
CPU単体比較には、相手機もGPUを無効にした1スレッド・1コア固定の測定が必要です。
モデルハッシュが一致しても、現在のQwen 3B結果とLlama 2 7B結果を直接の速度倍率として扱いません。

測定フォルダーを共有する場合は、次のようにまとめます。モデル本体やビルド全体を含める必要はありません。

```bash
# 実際のフォルダー名に置き換える
run_dir=results/4167-YYYYMMDDTHHMMSSZ-t8-XXXXXX
tar -czf "${run_dir}.tar.gz" "$run_dir"
```

ログにはホスト名やローカルパスが含まれます。公開前に内容を確認してください。

## 7. 確認範囲と出典

指定コミットのCMake・実行引数・既定値と、Hugging Face APIのモデルrevision・サイズ・SHA-256を確認しました。
掲載bashの構文を確認済みです。Jupiter2本体でのこのコミットのビルド、約24.43 GBのモデル取得、推論実行は本書作成時点では未実施です。
実測結果と実機検証済みの主張は、実行後に保存するログを根拠にしてください。

- [Discussion #4167](https://github.com/ggml-org/llama.cpp/discussions/4167)
- [固定ソースcommit](https://github.com/ggml-org/llama.cpp/commit/c1d0e7a004015f23bc0233470b747b596f29b264)
- [llama-bench引数・既定値の実装](https://github.com/ggml-org/llama.cpp/blob/c1d0e7a004015f23bc0233470b747b596f29b264/tools/llama-bench/llama-bench.cpp)
- [RISC-Vビルド設定](https://github.com/ggml-org/llama.cpp/blob/c1d0e7a004015f23bc0233470b747b596f29b264/ggml/src/ggml-cpu/CMakeLists.txt)
- [固定モデルrevision](https://huggingface.co/ggml-org/Llama-2-7B-GGUF/tree/89336df15f5c87d43a345a4830e4b6a97a0001b4)
- [モデルメタデータAPI](https://huggingface.co/api/models/ggml-org/Llama-2-7B-GGUF/revision/89336df15f5c87d43a345a4830e4b6a97a0001b4?blobs=true)
