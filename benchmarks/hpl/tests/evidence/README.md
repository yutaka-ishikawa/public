# 実行検証の範囲（2026-11-09）

- `make -C benchmarks test`：メモリ分散、最大N境界、不正CPU指定、残差失敗・skip・NaN・不正rankの拒否等、5 test PASS。
- RISC-V GCC 14.2.0でMPICH 4.3.2、OpenBLAS 0.3.34 RISCV64_ZVL256B、HPL 2.3をクロスビルド。
- ELFはRISC-V LP64D。`xhpl`、`mpiexec`、`hydra_pmi_proxy`のNEEDEDは`libc.so.6`のみ。
- MPI・OpenBLASの共有ライブラリ依存なし。GLIBCの最大要求版は2.38。
- RISC-V用MPIバイナリをQEMU user 10.0.13で実行し、2 ranksのMPI_Allreduceを確認。
- 未strip版HPL：QEMU user、VLEN=256、N=768、NB=192、P=1、Q=2、2 ranksで残差PASS。
- 配布archiveを別ディレクトリへ展開し、SHA256SUMSを検証。そのコピー内のstripped ELFをQEMU wrapper経由で実行。
- 配布物の`run-hpl --smoke`：N=1536、NB=192、P=1、Q=2、2 ranks、CPU0/1で残差PASS。
- 最終smokeのscaled residual：0.00617515326、1 test passed、0 failed、0 skipped。

[配布物smokeのログ](hpl-riscv64-qemu-user.log)を保存。
QEMU wrapperは開発環境だけに作成し、配布archiveの実行ファイルはRISC-V ELFのまま。

これはLinux user-mode emulationによるHPL/MPI検証であり、Keystoneのシステムゲスト検証ではない。
ログのTime/GFLOP/sはエミュレーターの値で、Jupiter2の性能値として使用してはいけない。
Jupiter2実機、X100×8、N約6万の最大メモリ測定、実機の冷却・周波数は未検証。
