# edgecall専用検証：データ往復とprivateメモリの参照拒否

## 構成と現在の結果

| 試験 | 対象 | この環境での結果 |
| --- | --- | --- |
| `test.c` | 実SDKと実Eyrie OCALL関数によるデータ受け渡し | 10グループPASS |
| `sm-test.c` | 実SMのSTOP_EDGE_CALL_HOSTとRESUME、PMP APIの呼び出し | 2グループPASS（PMP APIは模擬） |
| `guest/eapp.c`＋`guest/host.cpp` | Keystoneゲストでの8種類のデータ往復・不正戻り値拒否 | RISC-Vクロスビルド成功。ゲスト実行は未確認。 |
| `kernel/edgecall_pmp_probe.c` | OCALL中、Linux S-modeから元のEPM物理ページを読む試験 | オブジェクトのコンパイル成功。実PMP拒否は未確認。 |

既存SBIの65グループと合わせ、`verifications/run-all.sh`はホスト試験77グループを実行する。**ホスト試験のPASSだけで、実PMPによる参照拒否が確認できたとはしない。**

この環境には起動用Linux Imageと完全な対応カーネルの`Module.symvers`がなく、実行可能なKeystoneゲストも起動していない。カーネル試験は`.o`の生成までで、ロード可能な`.ko`の生成・ロード・実行は未確認である。

[実行結果と入力ハッシュ](results.json)、[ホスト実行ログ](evidence/host-run.log)、[ソース検証](source-review.md)も参照。

## 「領域を渡す」と「内容をコピーする」の違い

通常のOCALLは、EyrieがenclaveのprivateデータをUTM（共有バッファ）へ**コピー**し、hostにはUTM内のoffsetと長さを渡す。コピーされた内容をhostが読めるのは意図した動作である。

一方、元のprivateメモリへのポインタを値として渡しても、そのメモリが共有に変わるわけではない。以下を別々に検査する。

1. SDKの範囲検査：privateポインタをUTMへのポインタとして扱う操作を拒否する。
2. データとして渡されたポインタ：host wrapperはそれを参照せず、UTM範囲外と判定する。
3. 実アクセス：host側Linuxが、対応するenclaveのEPM内の物理ページを正規のRAM direct map経由で読むと失敗する。

1と2は範囲検査であり、悪意のあるhostが範囲検査を無視した場合のPMP保護の証明ではない。3のカーネル試験は、その違いを確認するために用意した。

## ホスト試験

リポジトリルートで実行する。

```bash
bash verifications/edgecall/run-tests.sh
# 既存のSBI試験も含める場合
bash verifications/run-all.sh
```

64ビットLinux、Cコンパイラ、Make、Python 3、Bashが必要。SDKの事前ビルドやroot権限は不要。

試験では同じ共有メモリを二つの異なる仮想アドレスへmmapし、runtime側とhost側でSDKの共有メモリ基準アドレスを切り替える。同一アドレスでしか成立しないpointer受け渡しになっていないことを確認する。

### データ試験の内容

- サイズ0、1、7、8、31、63、64、255、256、512、4096。
- NULを含むバイナリを渡し、hostが各バイトをXOR変換して返し、enclave側相当のprivate出力と照合する。
- 元のprivate入力が変わらないことと、戻り先の前後のcanaryを確認する。
- UTMの最大payloadちょうどを受け付け、1バイト超過をSTOP前に拒否する。
- 不正なreturn offset、巨大return length、hostエラーでprivate出力へコピーしない。
- 短いreturnは存在する分だけ、長いreturnは呼び出し側の容量までコピーする。
- 戻り値不要、STOP失敗、privateポインタの共有領域への登録拒否。
- offsetが終端・overflow・領域跨ぎ・privateアドレス由来の場合を拒否する。

SDKの`edge_call.c`と`edge_dispatch.c`を直接コンパイルし、Eyrieの`dispatch_edgecall_ocall()`は現在のソースから関数本文をそのまま抽出する。SBI STOPとuser-copyの境界は模擬する。

### SM試験の内容

実SMのRUN→STOP_EDGE_CALL_HOST→RESUMEを実行し、hostへ戻ると`EDGE_CALL_HOST`になり、EPMの権限取り消しAPIが呼ばれた状態でhost callback相当へ進むことを確認する。UTMへhostが書いた値がRESUME後にも残ることを確認する。

この試験のPMPは既存の共通ハーネスの状態モデルであり、物理メモリ参照を拒否する機構ではない。

## ゲスト試験のビルド：開発用ホスト

既存のKeystone版環境と、SDK・hello-native用Eyrieのビルド成果物を利用する。

```bash
# SDK/Eyrieが未準備なら、リポジトリルートで一度実行
bash scripts/build-install-keystone.sh --only sdk

make -C verifications/edgecall guest
```

生成物は`verifications/edgecall/build/guest/edgecall.ke`。CMakeは呼ばず、通常のMakefileで作る。

別のSDKビルド先を使う場合：

```bash
make -C verifications/edgecall guest \
  KEYSTONE_BUILD_DIR=/absolute/path/to/keystone-build
```

`SDK_DIR`、`RUNTIME_DIR`、`CROSS_COMPILE`も指定できる。ビルド設定を変更するときは先に`make -C verifications/edgecall clean`する。

## EPM参照試験用カーネルモジュール

**実際にゲストで起動するカーネルと一致する完全なビルド出力**が必要。通常は既存の`.build-k3/linux`を指定する。

```bash
make -C verifications/edgecall kernel-module \
  KERNEL_OUT="$PWD/.build-k3/linux"
```

生成物は`verifications/edgecall/kernel/edgecall_pmp_probe.ko`。`Module.symvers`がなければこのターゲットはエラーにする。カーネルオブジェクトだけのコンパイル確認は次のターゲットだが、これではゲストへロードできるモジュールの準備完了にはならない。

```bash
make -C verifications/edgecall kernel-object \
  KERNEL_OUT="$PWD/.build-k3/linux"
```

モジュールは`/dev/keystone_edge_probe`をmode 0600で作成し、ioctlにもCAP_SYS_RAWIOを要求する。試験対象はSDKが保持するEPM先頭物理アドレスとUTM物理アドレスであり、MMIOではなく有効なRAM/direct mapであることを検査する。モジュールは読み取りだけを試みる。

## 実行：Keystoneゲスト内

開発用ホストからQEMUを起動する。

```bash
bash scripts/run-k3.sh
```

ゲストのrootシェルで実行する。

```bash
mount-hostshare
cd /mnt/host/verifications/edgecall
bash run-guest.sh
```

スクリプトはKeystone driverとprobeモジュールを必要に応じてロードし、パッケージをゲストの新しい一時ディレクトリへ展開してrunnerを実行する。

正常なら以下の両方が表示される。

```text
PASS GUEST: kernel direct-map EPM read denied; UTM/control readable
PASS GUEST: 8 binary OCALL round trips and invalid return rejection
```

### 参照拒否の合格条件

OCALLでenclaveがSTOPPEDになってhost callbackへ戻った時点で、次を実行する。

1. 通常のkernelメモリを読み、成功し値が`0x6d`であることを確認。
2. UTMの物理ページを読み、成功しhostの共有バッファと値が一致することを確認。
3. EPM先頭の物理ページを読み、`copy_from_kernel_nofault()`が`-EFAULT`を返すことを確認。

EPMが読めた場合、RAMとして無効だった場合、deviceがない場合、ioctlが失敗した場合は、すべて試験失敗になる。user processからenclaveの仮想アドレスをそのまま参照して失敗しただけの結果は使わない。probeはLinux S-modeのRAM direct mapで元のEPMを読む。

試験はEPM先頭の1バイトへの読み取りを代表としており、全ページ・全hart・書き込み・命令実行・DMAの拒否まで網羅したものではない。必要なら`taskset`でrunnerを各hartへ固定して繰り返す。

### データ往復だけ実行する場合

probeモジュールを準備できない場合、明示的に次を指定できる。

```bash
bash run-guest.sh --data-only
```

この場合、PMP試験は`SKIP`と表示する。データ往復がPASSしてもメモリ隔離の確認済みとは扱わない。オプションなしの通常実行ではprobeが使えなければ失敗し、黙って試験を省略しない。

## 後片付け

runnerのSDK destructorがenclaveを破棄し、一時展開先はスクリプトが削除する。probeモジュールはロード状態で残るため、不要になったらゲスト側で次を実行する。

```bash
rmmod edgecall_pmp_probe
```

Keystone driver自体をアンロードする必要はない。

ホスト単体試験とゲスト統合試験の違い、ファームウェア更新からの一連の手順は[検証方法](../../doc/keystone-verification-methods.md)にもまとめています。

## ゲストでEPM確保に失敗する場合

`failed to allocate 566 page(s)` の原因、メモリ予算の修正と再実行手順は
[調査記録](../../doc/keystone-edgecall-epm-allocation.md)を参照。
