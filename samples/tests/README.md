# 上流examples/testsのMakefile版

ホストで`make -C samples/tests`。生成物は`build/tests.ke`。
SDK/Eyrieの準備とQEMUの手順は[全体README](../README.md)。

| 実行ファイル | 内容 | 期待値 |
| --- | --- | --- |
| test-stack | stackへのstore | 12345 |
| test-loop | ループ | 54321 |
| test-malloc | SDK tiny-malloc | 11411 |
| test-long-nop | 長い命令列 | 12345 |
| test-fibonacci | Fibonacci(35) | 14930352 |
| test-fib-bench | Fibonacciのtimebase tick計測 | 可変（値を表示） |
| test-attestation | nonce付きreportの検証 | 0 |
| test-untrusted | OCALLとhost文字列の共有コピー | 13 |
| test-data-sealing | sealing key取得（鍵自体は表示しない） | 0 |

展開先で`bash run-tests.sh`、暗号サービスなしの場合は`bash run-tests.sh --basic`。
個別実行例：`./test-runner test-stack eyrie-rt loader.bin 12345`。
`KEYSTONE_PUBLIC_DIR`はattestation用で、起動firmwareに対応する外部公開metadataを指定する。
SDKエラー、期待値不一致、report検証失敗、DESTROY失敗は非0で終了し、scriptも停止する。

C/assemblyの入口を現在のSDK CRTへ揃えた。旧app.ldsは参照用に残し、ビルドでは使わない。
`heap.c`で小さなmalloc arenaを用意する。long-nopはチェックイン済みの命令列をビルドする。
`generate_func.sh`で再生成する場合もmain/CRT方式に対応する。

fib-benchはLinuxから継承したscounterenでcycleが不許可だったため、rdtimeを使う。
戻り値はCPU cycle数ではなくtimebase tick数。volatile結果により計算の削除を防ぐ。
上流の別案`data-sealing_with_output.c`も参照用に保持するが、CMakeの9対象にも含まれず、
このMakefileの既定パッケージにも含めない。

元のKeystoneソースからの変更理由と改善／劣化の評価は[CHANGES.md](CHANGES.md)を参照。
