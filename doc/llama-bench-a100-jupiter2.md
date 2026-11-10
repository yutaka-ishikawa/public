# Jupiter2：A100 / IME2向けllama.cppのビルドとベンチマーク

作成日：2026-11-10（JST）。Jupiter2の32 GiBモデル、Bianbu系riscv64 Linux、bashを対象にします。

既存の[Qwen用手順](llama-bench-jupiter2.md)と[Llama 2 7B / Discussion #4167用手順](llama-bench-4167-jupiter2.md)を残し、その2種類のモデル・測定条件をA100にも適用する追加手順です。
本書はコンパイル・実行手順を示します。Jupiter2実機での本手順のビルド・実行確認は未実施です。

## 1. 比較の設計

A100はVLEN=1024、X100はVLEN=256です。A100で専用行列命令IME2を利用するため、公開実測とソースを確認できた`opensolvers/llama.cpp`のforkを使用します。
上流版を単に`taskset -c 8-15`で起動する方法とは分けます。

| 測定 | ソース・ビルド | CPU |
|---|---|---|
| 本書のA100 | 下記固定fork、SpacemiT ON、IME2検出必須 | 8–15から1 / 2 / 4 / 8コア |
| 本書のX100対照 | 同じ固定fork、SpacemiT OFF、RVV ON | 0–7から1 / 2 / 4 / 8コア |
| 既存Qwen結果 | 既存文書の上流`3b4d6ab` | X100 |
| 既存4167結果 | 既存文書の上流`c1d0e7a` | X100、Apple投稿は主にMetal GPU |

本書の固定fork：`107ead2f23b83f1ab9da06d317558ec159e0f9be`（branch `x60-ime-rvv`）。
これは同じK3のBanana Pi SM10でIME2の公開結果がある版です。SM10での結果はJupiter2での検証の代わりにはなりません。
**本書のA100とX100を比較してから、既存結果を参考として併記してください。**
Discussion #4167とはモデル・pp512 / tg128を合わせますが、ソースコミットとbackendは一致しません。
速度の差にはCPU、IME、コンパイラ、カーネル、メモリ帯域等が含まれます。

## 2. カーネルとツールチェーンの前提

CPU 0–7がX100、8–15がA100という配置を前提にします。最初に実機で確認してください。

```bash
uname -a
lscpu -e=CPU,ONLINE,MAXMHZ
grep -E 'processor|model name|isa' /proc/cpuinfo
ls -l /proc/set_ai_thread
gcc --version
g++ --version
as --version
```

`/proc/set_ai_thread`がない場合は、この手順のA100起動は実施できません。A100ユーザー空間実行を提供する対応Bianbu / ベンダーカーネルを用意してください。
単なるCPU affinity指定で代替したことにはしません。A100がonlineでない場合も、使用中のカーネル・ファームウェアの対応を確認します。

公開SM10実測のツールチェーンはBianbu GCC 15.2.0とbinutils 2.46です。
**GCCの版番号だけではIME2対応を保証しません。** `vmadot`、`vpack.vv`、`vnspack.vv`等を受け付けるコンパイラ・assemblerが必要です。
本書はJupiter2上のネイティブビルドを扱い、まず対応するBianbuのパッケージを使用します。
使用中OSの提供範囲は`apt-cache policy gcc g++ binutils`で確認してください。
別のネイティブツールチェーンを使う場合は`CC`と`CXX`にその絶対パスを指定します。

forkのビルド文書にはSpacemiTのクロスツールチェーン取得先もありますが、掲載アーカイブは**x86_64ホスト用**です。
Jupiter2上でその実行ファイルは起動できません。本書のコマンドに混ぜず、クロスビルドする場合は別途sysroot・glibcと対象ABIを合わせます。

## 3. 依存パッケージと固定ソースの取得

```bash
sudo apt update
sudo apt install -y git cmake build-essential binutils curl python3 util-linux \
  libcurl4-openssl-dev libssl-dev
mkdir -p "$HOME/llama-a100"
cd "$HOME/llama-a100"
git clone --depth 1 --branch x60-ime-rvv https://github.com/opensolvers/llama.cpp.git src
cd src
git fetch --depth 1 origin 107ead2f23b83f1ab9da06d317558ec159e0f9be
git checkout --detach 107ead2f23b83f1ab9da06d317558ec159e0f9be
mkdir -p models results
```

以下はこの`src`内で実行します。既存の2つの作業ツリーは変更しません。
モデル4ファイルは合計約24.72 GiBです。ビルド2種類と一時ファイルもあるため、40 GiB以上のディスク空きを推奨します。
32 GiBのRAMで全モデルを同時ロードせず、1つずつ実行します。実行前に他の大きな処理を停止します。

## 4. A100とX100を別ビルドする

以下のスクリプトを作り、実行します。ビルドは通常のX100側で行い、生成物のA100実行は後段で行います。
A100のconfigureログに`RISCV64_SPACEMIT_IME2`が出ない場合は停止します。CMake変数を手動で成功扱いに上書きしないでください。

```bash
cat > build-a100.sh <<'BUILD'
#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$(realpath -- "$0")")"
expected=107ead2f23b83f1ab9da06d317558ec159e0f9be
test "$(git rev-parse HEAD)" = "$expected"
git diff --quiet
git diff --cached --quiet
mkdir -p results
for target in a100 x100; do
  build_dir="build-$target"
  on=OFF
  if [[ $target == a100 ]]; then on=ON; fi
  (
    set -x
    date -u --iso-8601=seconds
    git rev-parse HEAD
    "${CC:-gcc}" --version
    "${CXX:-g++}" --version
    as --version
    cmake --version
    taskset -c 0-7 cmake -S . -B "$build_dir" \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DCMAKE_C_COMPILER="${CC:-gcc}" -DCMAKE_CXX_COMPILER="${CXX:-g++}" \
      -DGGML_NATIVE=OFF -DGGML_CPU_ALL_VARIANTS=OFF \
      -DGGML_CPU_RISCV64_SPACEMIT="$on" -DGGML_CPU_REPACK=OFF \
      -DGGML_RVV=ON -DGGML_RV_ZFH=ON -DGGML_RV_ZVFH=ON \
      -DGGML_RV_ZBA=ON -DGGML_RV_ZICBOP=ON -DGGML_RV_ZIHINTPAUSE=ON \
      -DGGML_RV_ZVFBFWMA=OFF -DGGML_XTHEADVECTOR=OFF \
      -DGGML_OPENMP=OFF -DGGML_BLAS=OFF -DGGML_CUDA=OFF -DGGML_VULKAN=OFF \
      -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF \
      -DLLAMA_BUILD_SERVER=OFF -DLLAMA_BUILD_TOOLS=ON
  ) 2>&1 | tee "results/configure-$target.log"
  if [[ $target == a100 ]]; then
    grep -E '^RISCV64_SPACEMIT_IME_SPEC:.*RISCV64_SPACEMIT_IME2' \
      "results/configure-$target.log"
  fi
  (
    set -x
    taskset -c 0-7 cmake --build "$build_dir" \
      --target llama-bench llama-cli -j4
  ) 2>&1 | tee "results/build-$target.log"
  git rev-parse HEAD > "$build_dir/source-commit.txt"
  sha256sum "$build_dir/bin/llama-bench" "$build_dir/bin/llama-cli" \
    > "$build_dir/binary.sha256"
done
BUILD
bash -n build-a100.sh
bash build-a100.sh
```

`GGML_CPU_REPACK=OFF`はこのforkのSpacemiT用ビルド手順に合わせています。対照X100も同じ値にします。
そのため過去の上流X100ビルドと条件が全て同一ではありません。
IME2検出失敗時は`build-a100/CMakeFiles/`のconfigureエラーログと`results/configure-a100.log`を確認します。
コンパイラを変更する際は新しいビルドディレクトリを使うか、作成した`build-a100` / `build-x100`だけを取り除いて再設定してください。
ビルドがOOMで停止する場合は`-j2`に減らします。バイナリだけを移動せず、共有ライブラリを含むビルドツリーを維持します。

## 5. モデルの再利用または取得

既存手順のモデルがある場合は、以下と同じファイルを`models/`へコピーまたはシンボリックリンクし、必ずハッシュ検証します。
ハッシュが一致すれば再ダウンロードは不要です。検証時の読み込みは測定時間に含めません。

```bash
cat > models/SHA256SUMS <<'SUMS'
626b4a6678b86442240e33df819e00132d3ba7dddfe1cdc4fbb18e0a9615c62d  qwen2.5-3b-instruct-q4_k_m.gguf
8f20162e86d65aa0f3b799e326149d6fe247eef2e2d0af5626e1942b8acb8851  Llama-2-7B-F16.gguf
62cfe8faf481dae6e84f01763fc84f1a5a4ffaaf4800a54f2ab81ea7128deae1  Llama-2-7B-Q8_0.gguf
1f85eb184e6a4e531e1419fe1a9c43dd4914ebadf4452c6b1f2cc829679fe8f4  Llama-2-7B-Q4_0.gguf
SUMS
cat > models/SOURCES.tsv <<'SOURCES'
Qwen/Qwen2.5-3B-Instruct-GGUF 7dabda4d13d513e3e842b20f0d435c732f172cbe qwen2.5-3b-instruct-q4_k_m.gguf
ggml-org/Llama-2-7B-GGUF 89336df15f5c87d43a345a4830e4b6a97a0001b4 Llama-2-7B-F16.gguf
ggml-org/Llama-2-7B-GGUF 89336df15f5c87d43a345a4830e4b6a97a0001b4 Llama-2-7B-Q8_0.gguf
ggml-org/Llama-2-7B-GGUF 89336df15f5c87d43a345a4830e4b6a97a0001b4 Llama-2-7B-Q4_0.gguf
SOURCES
(
set -euo pipefail
while read -r repo revision file; do
  if [[ ! -f models/$file ]]; then
    curl --fail --location --retry 3 --continue-at - \
      --output "models/$file.part" \
      "https://huggingface.co/$repo/resolve/$revision/$file"
    mv -- "models/$file.part" "models/$file"
  fi
done < models/SOURCES.tsv
(cd models && sha256sum --check SHA256SUMS)
)
```

既存ファイルのハッシュが不一致の場合は停止し、該当ファイルだけを退避して再取得します。
Qwenだけを先に試す場合はSOURCES.tsvのQwenの1行のみを取得しても構いません。
以下の実行スクリプトは選んだモデルのみ検証するため、Llama 2 7Bの取得前でもQwen測定が可能です。
速度測定用の文章データセットは不要です。モデル利用条件は各取得元のモデルカードで確認してください。

## 6. A100への登録とCPU固定を行うランチャー

`/proc/set_ai_thread`へは**実際にexecするプロセスのPID**を書きます。
シェルで登録した後、同じPIDを引き継ぐ`exec taskset ...`で起動します。
`sudo echo ... > /proc/...`ではリダイレクトにsudoが適用されないため、下記ランチャーを使用します。

```bash
cat > launch-a100.sh <<'LAUNCH'
#!/usr/bin/env bash
set -euo pipefail
cpus=$1
shift
[[ -e /proc/set_ai_thread ]] || { echo 'Missing /proc/set_ai_thread' >&2; exit 2; }
# 先にPIDを確定する。パイプ内でBASHPIDを評価しない。
pid=$BASHPID
if [[ -w /proc/set_ai_thread ]]; then
  printf '%s\n' "$pid" > /proc/set_ai_thread
else
  sudo -n sh -c 'printf "%s\n" "$1" > /proc/set_ai_thread' sh "$pid"
fi
printf 'ai_registration_pid=%s cpu_list=%s\n' "$pid" "$cpus" >&2
exec taskset -c "$cpus" "$@"
LAUNCH
bash -n launch-a100.sh
```

一般ユーザーで書けない場合は実行前に`sudo -v`で認証します。ランチャーの登録処理だけがsudoを使い、llama本体は元のユーザーのままです。
権限を全ユーザーへ恒久的に開放する必要はありません。
内部のIMEワーカーがCPU affinityを再設定する可能性があるため、次節では`SPACEMIT_PERFER_CORE_ID`も同じコア集合に設定します。
綴りは実装どおり`PERFER`です。`SPACEMIT_CORE_ARCH`で実機の検出結果を偽装しません。

## 7. Qwen / Llama 2 7Bの実行と比較用ログ

下記スクリプトを保存します。モデル・測定条件とCPUを選択でき、各回の専用フォルダーにログとビルド情報を保存します。
A100では`-v`でIME状態を出力し、完了後に`use_ime2: 1`が見つからなければ測定を不成立とします。

```bash
cat > run-a100-bench.sh <<'RUN'
#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$(realpath -- "$0")")"
target=${1:-a100}
suite=${2:-qwen}
threads=${3:-8}
case $target in a100) base=8;; x100) base=0;; *) exit 2;; esac
case $threads in 1|2|4|8) ;; *) echo 'threads: 1,2,4,8 only'; exit 2;; esac
case $suite in
  qwen) models=(qwen2.5-3b-instruct-q4_k_m.gguf); batch=512; fa=off; delay=0;;
  llama2) models=(Llama-2-7B-F16.gguf Llama-2-7B-Q8_0.gguf Llama-2-7B-Q4_0.gguf)
          batch=2048; fa=auto; delay=30;;
  *) echo 'suite: qwen or llama2'; exit 2;;
esac
cpus=$(seq -s, "$base" "$((base+threads-1))")
expected=107ead2f23b83f1ab9da06d317558ec159e0f9be
build_dir="build-$target"
mkdir -p results
run_dir=$(mktemp -d "results/${target}-${suite}-t${threads}-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")
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
  echo '=== after ==='; snapshot
  printf 'exit_status=%s run_directory=%s\n' "$status" "$run_dir"
  exit "$status"
}
trap finish EXIT
printf 'target=%s suite=%s threads=%s cpu_list=%s\n' "$target" "$suite" "$threads" "$cpus"
echo '=== before ==='; snapshot
uname -a
cat /etc/os-release
lscpu
cat /proc/cpuinfo
printf 'source_commit=%s\n' "$(git rev-parse HEAD)"
git status --short --untracked-files=no
test "$(git rev-parse HEAD)" = "$expected"
git diff --quiet
git diff --cached --quiet
test "$(cat "$build_dir/source-commit.txt")" = "$expected"
sha256sum --check "$build_dir/binary.sha256"
cat models/SOURCES.tsv
args=()
for model in "${models[@]}"; do
  awk -v name="$model" '$2==name {print}' models/SHA256SUMS > "$run_dir/check.sha256"
  test -s "$run_dir/check.sha256"
  (cd models && sha256sum --check "../$run_dir/check.sha256")
  stat -Lc '%n %s bytes' "models/$model"
  args+=(-m "models/$model")
done
cp models/SOURCES.tsv models/SHA256SUMS "$run_dir/"
cp "$build_dir/CMakeCache.txt" "$build_dir/compile_commands.json" "$run_dir/"
cp "results/configure-$target.log" "$run_dir/configure.log"
cp "results/build-$target.log" "$run_dir/build.log"
cp -- "$0" launch-a100.sh "$run_dir/"
echo '=== binary and configuration ==='
ldd "$build_dir/bin/llama-bench"
cat "$build_dir/CMakeCache.txt"
# 親シェルからのSpacemiT設定を除去し、この測定の設定だけを渡す。
envargs=(env -u SPACEMIT_CORE_ARCH -u SPACEMIT_PERFER_CORE_ARCH
  -u SPACEMIT_PERFER_CORE_ID -u SPACEMIT_MEM_BACKEND
  -u SPACEMIT_DISABLE_TCM -u SPACEMIT_HYBRID)
if [[ $target == a100 ]]; then
  envargs+=(SPACEMIT_DISABLE_TCM=1 SPACEMIT_PERFER_CORE_ARCH=a064
    "SPACEMIT_PERFER_CORE_ID=$cpus")
  launcher=(bash launch-a100.sh "$cpus")
  printf 'SPACEMIT_DISABLE_TCM=1 SPACEMIT_PERFER_CORE_ARCH=a064 SPACEMIT_PERFER_CORE_ID=%s\n' "$cpus"
else
  launcher=(taskset -c "$cpus")
fi
for name in OMP_NUM_THREADS OMP_PROC_BIND OPENBLAS_NUM_THREADS GGML_LOG_LEVEL; do
  printf '%s=%s\n' "$name" "${!name-<unset>}"
done
cmd=("${envargs[@]}" "${launcher[@]}" "$build_dir/bin/llama-bench"
  "${args[@]}" -ngl 0 -t "$threads" -p 512 -n 128 -b "$batch" -ub 512
  -ctk f16 -ctv f16 -fa "$fa" -r 5 --delay "$delay" -v -o md)
echo '=== exact command ==='
printf '%q ' "${cmd[@]}"; printf '\n'
echo '=== stdout + stderr ==='
set +e
"${cmd[@]}" 2>&1 | tee "$run_dir/bench.log"
status=${PIPESTATUS[0]}
set -e
test "$status" -eq 0 || exit "$status"
if [[ $target == a100 ]]; then
  grep -Eq 'use_ime2: *1' "$run_dir/bench.log" || {
    echo 'UNVERIFIED: no use_ime2: 1; do not report as IME2 result'; exit 3;
  }
fi
echo 'Benchmark completed; review core mask, output rows, and correctness separately.'
RUN
bash -n run-a100-bench.sh

# /procへの登録にsudoが必要な場合、実行前に認証する
sudo -v
bash run-a100-bench.sh a100 qwen 8
bash run-a100-bench.sh x100 qwen 8
```

`run.log`には環境、日時、完全なコマンド、ソース版、モデルrevision / SHA-256、バイナリハッシュ、CMake設定、結果と終了コードが入ります。
`bench.log`はllama-benchの標準出力・標準エラーのみです。診断を残すため`2>/dev/null`にしません。
同じ測定フォルダーにビルド・configureログ、compile_commands.json、実行スクリプトも保存します。
IME設定の`mem_backend`はこの版の既定値を使い、実際の値を起動ログで確認します。TCMは無効にしています。
`use_ime2: 1`はbackendの初期化を示し、全ての演算や全量子化がIME2で処理されたことまでは保証しません。

A100の起動ログで、8コア測定なら次を確認してください。1コアならmaskは`100`、2コアは`300`、4コアは`f00`です。

```text
use_ime2: 1
use_ime1: 0
perfer_core_arch_id: a064
cpu_mask: ff00
```

内部ワーカーの配置に疑義がある場合は実行中に別端末で`ps -eLo pid,tid,psr,comm`で確認し、対象TIDに`taskset -pc TID`を実行してください。
実行前後の温度・周波数だけでは、実行中にスロットリングがなかったとは判断できません。

## 8. 1 / 2 / 4 / 8コア測定とLlama 2 7B

1コア測定では、A100はCPU 8、X100はCPU 0を使います。内部のコア選択も同じ集合に制限します。
生成は8コアより4コア等が速い場合もあるため、入力処理と生成を別々に評価してください。

```bash
(
set -euo pipefail
sudo -v
for threads in 1 2 4 8; do
  # sudoの認証が切れていたら、A100登録前に更新する
  sudo -v
  bash run-a100-bench.sh a100 qwen "$threads"
  bash run-a100-bench.sh x100 qwen "$threads"
done
)

# Llama 2 7Bの3種類。まず8コアで実行する
sudo -v
bash run-a100-bench.sh a100 llama2 8
bash run-a100-bench.sh x100 llama2 8

# Llama 2 7Bの1コア測定
sudo -v
bash run-a100-bench.sh a100 llama2 1
bash run-a100-bench.sh x100 llama2 1
```

Qwenは2行、Llama 2 7Bは3モデル×2テストの6行が必要です。
`pp512`と`tg128`、各平均と標準偏差、threads、モデル名、FA、buildを確認します。
Qwenは既存手順と同じ`b=512 / ub=512 / fa=off`、Llama 2 7Bは4167用手順と同じ`b=2048 / ub=512 / fa=auto`です。
ただしfork版なのでDiscussionの固定コミットと一致する結果とは記載しません。
**生成品質が正常と確認できない結果は、速度比較の有効な結果として扱わないでください。**

## 9. 実際の文章生成による確認

llama-benchは合成トークンの速度測定で、回答の正しさや反復異常を検証しません。
公開forkでは別量子化で反復異常が報告されたこともあるため、各モデルを短い文章生成で確認します。
Qwenの例を示します。必要に応じて3つのLlama 2ファイルにも置き換えて実行してください。
base Llama 2は指示応答用モデルではないため、文章継続としての妥当性を確認します。

```bash
(
set -euo pipefail
sudo -v
quality_log="results/quality-a100-$(date -u +%Y%m%dT%H%M%SZ)-$$.log"
cmd=(env SPACEMIT_DISABLE_TCM=1 SPACEMIT_PERFER_CORE_ARCH=a064
  SPACEMIT_PERFER_CORE_ID=8,9,10,11,12,13,14,15
  bash launch-a100.sh 8,9,10,11,12,13,14,15 build-a100/bin/llama-cli
  -m models/qwen2.5-3b-instruct-q4_k_m.gguf
  -ngl 0 -t 8 -tb 8 -c 1024 -n 64 --temp 0 --single-turn
  -p 'Explain RISC-V vector extensions in two short sentences.')
{
  date -u --iso-8601=seconds
  git rev-parse HEAD
  sha256sum models/qwen2.5-3b-instruct-q4_k_m.gguf
  printf '%q ' "${cmd[@]}"; printf '\n'
  "${cmd[@]}"
} 2>&1 | tee "$quality_log"
)
```

異常な文字・同じトークンの連続・NaN・クラッシュを確認します。正常な短文が出ても包括的な正確性検証にはなりません。
速度ログと品質ログは別々に残し、結果表に対応関係を記入します。

## 10. トラブルシュートと確認範囲

| 症状 | 確認・対応 |
|---|---|
| IME2がCMakeで検出されない | 対応するBianbu gcc / g++ / assemblerか。configureエラーと実際の-marchを確認 |
| `/proc/set_ai_thread`がない | 対応するベンダーカーネルが必要。tasksetだけで代用しない |
| sudo認証エラー | A100起動直前に`sudo -v`。ログを確認して再実行 |
| Illegal instruction | IME2検出、A100登録、CPU mask、対応カーネルと命令、CPU配置を確認 |
| `use_ime2: 1`がない | `-v`とbackendログを確認。不成立として扱う |
| cpu_maskが指定と異なる | `SPACEMIT_PERFER_CORE_ID`とCPU配置、各TIDのaffinityを確認 |
| OOM / Killed | 実行中の他処理、swap、メモリ使用、IME用変換領域を確認。スレッド数やubを変える場合は別条件として記録 |
| 8コアの生成が遅い | 1 / 2 / 4コア結果を確認。入力処理と生成を分ける |
| 出力が反復・破損する | 速度比較を保留し、モデルハッシュと量子化対応を確認 |

本書の作成時は固定forkのソース、IME2検出条件、起動処理、モデルメタデータ、bash構文を確認しました。
Jupiter2上でのコンパイル・モデル取得・IME2実行・正確性・速度測定は未実施です。実機のログで検証範囲を更新してください。
既存の2つのベンチマーク文書は変更していません。

## 11. 出典

- [固定fork](https://github.com/opensolvers/llama.cpp/tree/107ead2f23b83f1ab9da06d317558ec159e0f9be)
- [SpacemiTビルド手順](https://github.com/opensolvers/llama.cpp/blob/107ead2f23b83f1ab9da06d317558ec159e0f9be/docs/build-riscv64-spacemit.md)
- [IMEコンパイラ検出](https://github.com/opensolvers/llama.cpp/blob/107ead2f23b83f1ab9da06d317558ec159e0f9be/ggml/src/ggml-cpu/cmake/FindSMTIME.cmake)
- [コア選択とIME環境設定](https://github.com/opensolvers/llama.cpp/blob/107ead2f23b83f1ab9da06d317558ec159e0f9be/ggml/src/ggml-cpu/spacemit/ime_env.cpp)
- [SM10 / K3のツールチェーン・起動・コア数別測定](https://github.com/opensolvers/benchmarks/blob/main/llamacpp/results_sm10_q4_0.md)
- [同じforkコミットでのモデル別測定と品質確認](https://github.com/opensolvers/benchmarks/blob/main/llamacpp/results_sm10_model_suite.md)
- [Discussion #4167](https://github.com/ggml-org/llama.cpp/discussions/4167)
- [Qwenの固定revision](https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/tree/7dabda4d13d513e3e842b20f0d435c732f172cbe)
- [Llama 2の固定revision](https://huggingface.co/ggml-org/Llama-2-7B-GGUF/tree/89336df15f5c87d43a345a4830e4b6a97a0001b4)
