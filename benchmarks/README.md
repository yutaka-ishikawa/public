# Jupiter2 benchmarks

[HPL 2.3（FP64 Linpack）](hpl/README.md)を収録しています。

Jupiter2のBianbu/Ubuntu上で：

```sh
sudo apt-get update
sudo apt-get install -y build-essential python3
cd benchmarks
make JOBS=4
make plan
make smoke
make run
```

`make`だけでは測定を実行しません。HPLの公式ソースパッケージを同梱し、
MPICH 4.3.2とOpenBLAS 0.3.34はビルド時にSHA-256検証付きで取得します。
設定、メモリ上限の計算、比較条件、出典は[HPLの説明](hpl/README.md)を参照してください。

ビルド不要で使う場合は[実行ファイル一式](hpl/dist/README.md)を利用してください。MPIの開発パッケージは不要です。
