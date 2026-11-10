# Jupiter2：llama-benchのビルド・実行手順

作成日：2026-11-10（JST）。対象はMilk-V Jupiter2 / SpacemiT K3、Bianbu / Ubuntu系の64-bit Linuxです。

標準のCPU / RVV経路で、X100の8コアまたは1コアを測定します。
**MPIは不要**です。HPL用のMPICHやOpenBLASのインストールも必要ありません。
本手順はIME専用行列演算を無効にした比較基準であり、公称60 TOPSの測定ではありません。

## 1. 前提と作業場所

以下のコマンドはbashで実行します。ソース・ビルド生成物・約2.1 GBのモデルを置ける、
十分な空き容量のある作業ディレクトリを使用してください。本リポジトリ内へのcloneは不要です。

```bash
sudo apt update
sudo apt install -y git cmake build-essential curl libcurl4-openssl-dev libssl-dev util-linux

uname -m
gcc --version | head -1
g++ --version | head -1
cmake --version | head -1
lscpu -e=CPU,ONLINE,MAXMHZ
grep -E 'processor|model name|isa' /proc/cpuinfo
```

Jupiter2では`uname -m`が`riscv64`であることを確認します。
GCC / G++はRVV 1.0 intrinsicに対応した同じ世代を使用してください。GCC 15以降を推奨します。
古いコンパイラでRVV intrinsicが未定義になる場合、まずコンパイラとbinutilsを更新します。

本リポジトリに保存された実機記録では、LinuxのCPU番号0–7がX100、8–15がA100です。
CPU番号の配置が違う環境では、上記の出力を確認してから`taskset`の指定を変えます。

## 2. llama.cppを取得し、版を固定する

調査時点で確認した上流コミットを使用します。後日更新して比較する場合も、比較対象では同じコミットを使います。

```bash
git clone --depth 1 https://github.com/ggml-org/llama.cpp.git
cd llama.cpp

git fetch --depth 1 origin 3b4d6ab3f79651935fba1dd93732f29022a7f5f5
git checkout --detach 3b4d6ab3f79651935fba1dd93732f29022a7f5f5

mkdir -p results models
git rev-parse HEAD | tee results/llama-commit.txt
```

以後、特記しない限り`llama.cpp/`をカレントディレクトリにします。

## 3. Jupiter2用のCPU / RVV版をビルドする

```bash
set -o pipefail

taskset -c 0-7 cmake -S . -B build-x100 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGGML_NATIVE=OFF \
  -DGGML_CPU_ALL_VARIANTS=OFF \
  -DGGML_RVV=ON \
  -DGGML_RV_ZFH=ON \
  -DGGML_RV_ZVFH=ON \
  -DGGML_RV_ZICBOP=OFF \
  -DGGML_RV_ZIHINTPAUSE=ON \
  -DGGML_RV_ZVFBFWMA=OFF \
  -DGGML_XTHEADVECTOR=OFF \
  -DGGML_CPU_RISCV64_SPACEMIT=OFF \
  -DGGML_OPENMP=OFF \
  -DGGML_BLAS=OFF \
  -DGGML_CUDA=OFF \
  -DGGML_VULKAN=OFF \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_SERVER=OFF \
  -DLLAMA_BUILD_TOOLS=ON \
  2>&1 | tee results/configure-x100.log

taskset -c 0-7 cmake --build build-x100 --target llama-bench -j4 \
  2>&1 | tee results/build-x100.log

./build-x100/bin/llama-bench --help > results/llama-bench-help.txt
cp build-x100/CMakeCache.txt results/CMakeCache-x100.txt
```

### 設定の意味

| 設定 | 意味 |
|---|---|
| `GGML_RVV=ON` | 標準RVV 1.0のCPU kernelを使用 |
| `GGML_NATIVE=OFF` | RISC-V用の拡張を明示して再現性を確保。RVVの無効化ではない |
| `GGML_RV_ZFH/ZVFH=ON` | X100の半精度スカラー／ベクトル拡張を利用 |
| `GGML_RV_ZICBOP=OFF` | 基準測定では追加のprefetch拡張を要求しない |
| `GGML_CPU_RISCV64_SPACEMIT=OFF` | CPU内のSpacemiT IME専用実装を無効にする |
| `GGML_OPENMP=OFF` | 追加のOpenMP実行経路を無効にする。ggml自身のthread poolは利用可能 |
| `GGML_BLAS=OFF` | 外部BLASを利用しない |
| `GGML_CUDA/VULKAN=OFF` | GPU backendを利用しない |

確認した上流ソースのRISC-V分岐では、`GGML_RVV`等から`-march`を組み立てます。
したがって、`GGML_NATIVE`のON/OFFだけでRVVの使用を判断しないでください。
`build-x100/compile_commands.json`でも実際の`-march`を確認できます。

実行ファイルは`build-x100/bin/llama-bench`です。
まずビルドツリー内から使用します。実行ファイルだけを別環境へコピーすると、
共有ライブラリの探索やCPU命令対応が合わなくなる場合があります。

## 4. 比較用GGUFモデルを取得する

例としてQwen公式のQwen2.5-3B-Instruct、量子化`Q4_K_M`を使用します。
同一のモデル・量子化・ファイルを全マシンで使用してください。

以下はモデル側のrevisionも固定したURLです。取得時点のモデルAPIでは2,104,932,768 bytesでした。

```bash
curl --fail --location --retry 3 \
  'https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/7dabda4d13d513e3e842b20f0d435c732f172cbe/qwen2.5-3b-instruct-q4_k_m.gguf' \
  -o models/qwen2.5-3b-instruct-q4_k_m.gguf

printf '%s  %s\n' \
  '626b4a6678b86442240e33df819e00132d3ba7dddfe1cdc4fbb18e0a9615c62d' \
  'models/qwen2.5-3b-instruct-q4_k_m.gguf' | sha256sum --check

sha256sum models/qwen2.5-3b-instruct-q4_k_m.gguf \
  > results/model-sha256.txt
```

すでにGGUFを持っている場合はダウンロードを省略し、以後の`-m`をそのファイルに置き換えます。
Safetensorsや元の学習用重みを、そのまま`-m`に指定することはできません。

## 5. X100の8コアで実行する

```bash
set -o pipefail

taskset -c 0-7 ./build-x100/bin/llama-bench \
  -m models/qwen2.5-3b-instruct-q4_k_m.gguf \
  -ngl 0 \
  -t 8 \
  -p 512 \
  -n 128 \
  -b 512 \
  -ub 512 \
  -ctk f16 \
  -ctv f16 \
  -fa off \
  -r 5 \
  2>&1 | tee results/llama-bench-x100-8core.log
```

| 引数 | 内容 |
|---|---|
| `taskset -c 0-7` | プロセス起動時からX100のCPU集合へ制限。子threadもその集合を継承 |
| `-ngl 0` | GPUへのlayer offloadなし |
| `-t 8` | 演算用thread数を8に固定 |
| `-p 512` | prompt processingを512 tokensで測定 |
| `-n 128` | text generationを128 tokensで測定 |
| `-b 512 -ub 512` | logical batch / micro batchの条件を固定 |
| `-ctk f16 -ctv f16` | KV cacheのデータ型を固定 |
| `-fa off` | flash attentionを明示的に無効化 |
| `-r 5` | 各testを5回反復 |

既定のwarmupは有効のまま使用します。比較時に`--no-warmup`を追加しないでください。

### 結果の読み方

- **`pp512`**：prompt processingのtokens/s。演算・並列化の効果が出やすい測定です。
- **`tg128`**：text generationのtokens/s。モデル重みの読み出し等、メモリ性能の影響も強く受けます。
- どちらも大きいほど高速です。`-r 5`の出力は平均と標準偏差で、JSONには各回の値も含まれます。
- `-p 512 -n 128`はppとtgの独立したtestです。512-token promptを処理した後の会話応答全体の遅延を表すものではありません。
- llama-benchは速度測定です。生成文章の品質評価や、HPLの残差チェックに相当する検証とは別です。

## 6. X100の単一コアで実行する

Cortex-A76等とのコア単体比較では、同じモデル・同じ引数でthread数を1にし、CPUも1個だけに固定します。

```bash
set -o pipefail

taskset -c 0 ./build-x100/bin/llama-bench \
  -m models/qwen2.5-3b-instruct-q4_k_m.gguf \
  -ngl 0 -t 1 -p 512 -n 128 \
  -b 512 -ub 512 -ctk f16 -ctv f16 -fa off -r 5 \
  2>&1 | tee results/llama-bench-x100-1core.log
```

8コアのtokens/sを8で割って、単一コアの性能として扱わないでください。
この比較にはRVV / NEONの実装、キャッシュ、メモリ、量子化kernelの差も含まれます。
ベクトル演算を除いた純粋なスカラーコア性能の測定ではありません。

## 7. JSON形式で保存する

機種間比較やExcel取り込みのため、標準出力をJSON、標準エラーを別ログへ保存できます。

```bash
taskset -c 0-7 ./build-x100/bin/llama-bench \
  -m models/qwen2.5-3b-instruct-q4_k_m.gguf \
  -ngl 0 -t 8 -p 512 -n 128 \
  -b 512 -ub 512 -ctk f16 -ctv f16 -fa off -r 5 \
  -o json \
  > results/llama-bench-x100-8core.json \
  2> results/llama-bench-x100-8core.stderr.log

python3 -m json.tool results/llama-bench-x100-8core.json >/dev/null
```

CSVが必要なら`-o csv`とし、出力先を`.csv`に変えます。
JSON / CSVのstdoutに`2>&1`でエラー出力を混ぜないでください。

## 8. 測定条件を保存する

モデルhash、commit、CMakeCache、configure/buildログに加え、OS・CPU・周波数条件を記録します。

```bash
{
  date -Iseconds
  uname -a
  git rev-parse HEAD
  gcc --version | head -1
  g++ --version | head -1
  cmake --version | head -1
  lscpu
  free -h
  for policy in /sys/devices/system/cpu/cpufreq/policy0 /sys/devices/system/cpu/cpufreq/policy8; do
    if [ -d "$policy" ]; then
      printf '\n%s\n' "$policy"
      cat "$policy/scaling_governor"
      cat "$policy/scaling_cur_freq"
      cat "$policy/scaling_max_freq"
    fi
  done
} > results/environment.txt
```

CPU0のpolicyがX100側であることを実機で確認します。
実行前後の周波数、温度、冷却条件を揃え、他の重い処理を止めて測定します。
本手順ではgovernorやシステム設定を自動変更しません。

## 9. Raspberry Pi 5との比較

Pi側は64-bit OS（`uname -m`が`aarch64`）を使用します。
同じllama.cppコミットと同じGGUFを用意し、Pi上でArm用にビルドします。
Jupiter2用の実行ファイルはPiでは動きません。

Piの`llama.cpp/`で、次の設定を使用します。

```bash
cmake -S . -B build-pi5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGGML_NATIVE=ON \
  -DGGML_CPU_KLEIDIAI=OFF \
  -DGGML_OPENMP=OFF \
  -DGGML_BLAS=OFF \
  -DGGML_CUDA=OFF \
  -DGGML_VULKAN=OFF \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_SERVER=OFF \
  -DLLAMA_BUILD_TOOLS=ON

cmake --build build-pi5 --target llama-bench -j4

taskset -c 0 ./build-pi5/bin/llama-bench \
  -m models/qwen2.5-3b-instruct-q4_k_m.gguf \
  -ngl 0 -t 1 -p 512 -n 128 \
  -b 512 -ub 512 -ctk f16 -ctv f16 -fa off -r 5
```

X100 1コアとCortex-A76 1コアを比較する場合、両機の標準最大周波数は2.4 GHzですが、
実際の動作周波数とthrottlingを確認します。
Pi側ではArmのnative検出を利用し、追加のKleidiAI経路は無効にしています。

マシン全体の比較では、Jupiter2を`taskset -c 0-7 ... -t 8`、
Pi 5を`taskset -c 0-3 ... -t 4`とし、単一コア比較とは別の表にします。

| 必ず揃える条件 | 例 |
|---|---|
| llama.cpp | 上記の同一commit |
| モデル | Qwen2.5-3B-Instruct Q4_K_M、同一SHA-256 |
| tokens | pp512 / tg128 |
| batch | b=512 / ub=512 |
| KV cache / flash attention | f16 / f16、off |
| 反復 | r=5、warmupあり |
| コア単体 | t=1、tasksetでCPU1個に固定 |

## 10. A100 / IMEは別の測定とする

A100はVLEN=1024、X100はVLEN=256です。
本手順のまま`taskset -c 8-15`や`0-15`に変更してよいとは保証していません。
kernelのVLEN前提、起動時のCPU検出、コア移動、実際に使用したbackendを確認した上で別途設計します。

確認した上流には`GGML_CPU_RISCV64_SPACEMIT`とIME1/IME2の命令対応検出がありますが、
IMEを有効にするだけで公称60 TOPSを利用した測定が成立するわけではありません。
対応コンパイラ・命令・実行コア・モデル量子化・実行ログを揃え、CPU / RVV基準とは分けて記録します。

## 11. よくある問題

| 症状 | 確認すること |
|---|---|
| RVV intrinsicや`-march`のエラー | gcc/g++/binutilsの版、buildログの実際のflagsを確認。GCC 15以降を推奨 |
| `Illegal instruction` | ISA・CPU番号・backendを確認。X100へ起動時から固定し、新しいbuild directoryで再設定 |
| モデルを開けない | GGUFのパスとSHA-256。HTTPエラー本文をモデルとして保存していないか |
| build中にKilled / OOM | `-j2`等へ減らす。buildと測定は同時に行わない |
| 実行時に`.so`が見つからない | buildツリー内から実行。別フォルダーへ本体だけコピーしない |
| JSONを解析できない | stdoutとstderrを別々に保存。まず終了コードとstderrを確認 |
| `GGML_*`が未使用と警告される | commitを確認。別の版へ変更した場合はその版のCMakeとhelpを読み直す |

## 12. 確認範囲と出典

本書では上流のCMake設定、llama-benchの引数・出力仕様、モデルAPIのrevision・ファイル名・サイズ・SHA-256を確認しました。
Jupiter2上での実ビルド、モデル取得・推論実行、tokens/sの測定は本書作成時点では未実施です。
本リポジトリのHPLの実測・検証結果を、llama-benchの検証結果として流用しません。

- [llama.cpp build documentation（固定commit）](https://github.com/ggml-org/llama.cpp/blob/3b4d6ab3f79651935fba1dd93732f29022a7f5f5/docs/build.md)
- [llama-bench README（固定commit）](https://github.com/ggml-org/llama.cpp/blob/3b4d6ab3f79651935fba1dd93732f29022a7f5f5/tools/llama-bench/README.md)
- [RISC-V用CPU CMake（固定commit）](https://github.com/ggml-org/llama.cpp/blob/3b4d6ab3f79651935fba1dd93732f29022a7f5f5/ggml/src/ggml-cpu/CMakeLists.txt)
- [ggmlのRVV等のoption定義（固定commit）](https://github.com/ggml-org/llama.cpp/blob/3b4d6ab3f79651935fba1dd93732f29022a7f5f5/ggml/CMakeLists.txt)
- [SpacemiT IME命令検出（固定commit）](https://github.com/ggml-org/llama.cpp/blob/3b4d6ab3f79651935fba1dd93732f29022a7f5f5/ggml/src/ggml-cpu/cmake/FindSMTIME.cmake)
- [Qwen公式GGUF repository（固定revision）](https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/tree/7dabda4d13d513e3e842b20f0d435c732f172cbe)
- [モデルのファイル情報API](https://huggingface.co/api/models/Qwen/Qwen2.5-3B-Instruct-GGUF/revision/7dabda4d13d513e3e842b20f0d435c732f172cbe?blobs=true)
- [Jupiter2実機記録](jupiter2-hardware-compatibility.md)
