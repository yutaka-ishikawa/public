# SBI 3001検証時のSDK memset再帰化とスタック枯渇

## 報告された停止

乱数matrixのcounterケースで、QEMU/Linuxの起動、9p共有、ドライバロード、
パッケージ展開、EPM確保までは成功し、その後次のログで終了しました。

```text
EPM allocated: 4194304 bytes ... (buddy)
[runtime] page fault at 0x10438 on 0x3ffdfff0 (scause: 0xf)
FAIL RNG: SDK=0 exit=18446744073709551615 calls=0 failed=0
```

`scause=15` はstore page faultです。SDK=0はドライバの実行操作が成功したことを示し、
eappの成功ではありません。exit値は64ビットの-1で、Eyrieの例外終了に対応します。
callbacks=0なので最初のOCALLより前に停止しています。

Eyrieのユーザースタックは `0x3ffe0000` ～ `0x40000000` の128 KiBです。
障害アドレス `0x3ffdfff0` は下端より16バイト低く、スタックを使い切った症状です。

## 実オブジェクトで確認した不具合

`keystone/sdk/src/app/string.c` はfreestanding eapp用のmemset等を自前実装しています。
従来のSDK eappライブラリのコンパイル設定では、GCCの最適化がこの実装のbyte loopを
memset呼び出しへ変換していました。定義している関数自身への再帰になります。
修正前の実際のRISC-Vオブジェクトを逆アセンブルして、memsetとstrlenの呼び出しを確認しました。

修正前の検証eappのmemsetは、たとえば次の形でした。

```text
memset:
    addi sp,sp,-16
    sd   ra,8(sp)
    ...
    jal  memset
```

検証eappの `storage` は4098バイトです。その初期化loopも最適化でmemsetへ変換されます。
4098は8の倍数でないためSDK実装のbyte loop側を使い、そこから自己再帰に入ります。
各呼び出しでスタックを消費し、下端を超えた保存命令でstore page faultとなります。
最初のデータ取得・OCALLより前の共通処理なので、乱数backendを変えても同じ停止を起こし得ます。

報告されたPCそのものの厳密な関数対応にはユーザー環境で作成したELFが必要ですが、
報告ログのスタック下端超過、OCALL前の停止、手元の実SDK機械語で確認した自己再帰は整合します。

## 修正

SDKのeapp用ライブラリに以下を追加しました。

- `-ffreestanding -fno-builtin`：通常のホストlibcを前提にした最適化を避ける。
- GCCのstring.cに `-fno-tree-loop-distribute-patterns`：自前のbyte loopを
  libc呼び出しへ置換する最適化を禁止する。
- `scripts/check-keystone-eapp-string.py`：SDK build時に実アーカイブのstring.c.oを
  逆アセンブルし、memset/memcpy/memmove/memcmp/strlenへの意図しないcall relocationを検出する。

ホスト用SDKライブラリのコンパイル設定は変更していません。

## 確認と再試験

修正前の実オブジェクトでは新しい機械語チェックが失敗し、strlen/memset呼び出しを検出しました。
修正後は同じチェックが成功し、検証eappへリンクされたmemsetにも自己呼び出しがないことを確認しました。
SDK、Eyrie/examples、verificationsのゲストプログラムの再ビルドも成功しています。
これはバイナリ生成の確認であり、報告者のQEMUでの再実行PASSはまだ確認していません。

ホストのリポジトリ直下で実行します。

```bash
git pull
make eyrie JOBS=4
make -C verifications/random guest
python3 verifications/random/run-qemu-matrix.py --case counter
```

SDKの出力先を変更している場合は両方のmakeに同じ `KEYSTONE_BUILD_DIR=/absolute/path` を指定します。
今回の修正はeappへリンクするSDKの問題なので、OpenSBI/QEMU/Linux/ドライバの再ビルドや
rootfsの再インストールは不要です。matrixは9p上の再生成済みrandom.keを使います。
counterケースが成功したら `--case counter` を外して全方式を再実行します。
