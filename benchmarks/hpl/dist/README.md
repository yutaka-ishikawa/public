# Jupiter2用の配布ファイル

- [hpl-jupiter2-riscv64.tar.gz](hpl-jupiter2-riscv64.tar.gz)：推奨。xhpl、対応MPICHランチャー・proxy、run-hpl、設定、licenseを含む。
- [xhpl-riscv64](xhpl-riscv64)：HPL本体だけのRISC-V ELF。MPIは静的リンク済み。複数rank実行には上のarchive内のランチャーを使用。
- [SHA256SUMS](SHA256SUMS)：ファイル検証用。

Jupiter2のBianbu/Ubuntu（glibc 2.38以降、Python 3.11以降）で：

```sh
sha256sum -c SHA256SUMS
tar -xzf hpl-jupiter2-riscv64.tar.gz
cd hpl-jupiter2-riscv64
./run-hpl --smoke
python3 hpl.py plan
./run-hpl
```

MPIとOpenBLASは静的リンク。必要な共有ライブラリはOS標準のlibc.so.6のみ。
32 GiBモデルでは空きメモリと作業領域を考慮して最大Nを自動選択する。
理想条件の例はN=61440。実際の値は実機上のplanで確認する。

[設定・出典](../README.md)、[検証範囲](../tests/evidence/README.md)を参照。
