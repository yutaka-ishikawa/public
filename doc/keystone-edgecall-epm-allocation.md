# edgecallゲスト試験のEPM割り当て失敗

## 後続修正

以下は256 KiBへ縮小した時点の調査記録である。その後、ドライバの境界条件、
DMA制約、CMA予約を修正し、256 MiBのEPM確保経路を追加した。
edgecallの既定free memoryも2 MiBに戻した。
現在の構成と更新・試験方法は
[EPM確保の検証](keystone-large-epm.md)を参照。

## 観測結果

ユーザーのQEMUゲストで `bash run-guest.sh --data-only` を実行すると、
`failed to allocate 566 page(s)`、`failed to initialize epm`、
`Cannot allocate memory`、`enclave init failed: 3` で終了した。
これはLinuxドライバのEPM割り当て段階の失敗であり、SMのCREATEや
OCALL/RESUMEを実行する前である。edgecallのデータ転送失敗ではない。

`--data-only` は直接EPM参照試験だけを省略する。
enclave作成やSMによる実行は必要なので、今回の割り当てエラーは回避しない。

## ソースと生成物から確認した原因

`guest/host.cpp` はfree memoryとUTMにそれぞれ2 MiBを要求していた。
SDKの `calculate_required_pages()` はeapp、runtime、loaderのファイルサイズ
由来のページ数と16ページの余裕を計算し、free memoryを加える。
2026-11-08のローカル生成物では次の計算となった。ファイルサイズ由来の
計算は現SDKの式に従う。異なるビルドではサイズは変わり得る。

| 項目 | 値 |
| --- | ---: |
| eapp | 7,592 bytes |
| eyrie-rt | 111,760 bytes |
| loader.bin | 41,008 bytes |
| free memory以外の要求 | 54 pages |
| 旧要求 | 512 + 54 = 566 pages |
| buddy allocatorで必要な連続領域 | order 10、1024 pages、4 MiB |
| 一時回避時の要求（現在は不使用） | 64 + 54 = 118 pages |
| 一時回避時の連続領域 | order 7、128 pages、512 KiB |

`keystone/linux-keystone-driver/keystone-page.c` の旧 `epm_init()` は
Linux 6.8以降で `order < MAX_PAGE_ORDER` のときだけbuddy allocatorを呼ぶ。
一方、現在の `linux-k3/include/linux/mmzone.h` はMAX_PAGE_ORDERを含めた
orderを有効とする。標準設定のMAX_PAGE_ORDER=10では、旧要求のorder 10は
有効なのに呼び出しがスキップされる。この境界条件の問題を確認した。

さらに旧CMAへのfallbackは `GFP_KERNEL | __GFP_DMA32` を渡す。
現在のQEMU Jupiter2構成のRAMは0x102000000以上にあり、4 GiB未満を対象とする
DMA32制約と整合しない。CMA領域やデバイスのDMA設定にも依存するため、
単に「ゲスト全体の空きメモリ不足」とは断定できない。
ログの566という値は、countを要求ページ数へ戻したfallback経路と一致する。

## 一時回避とその後の本修正

この試験の最大データは4096 bytesで、ヘッダ込み4120 bytes。
free memoryとUTMを、既存helloと同じ各256 KiBに変更した。
データサイズや8回の往復、不正戻り値の試験、PMP試験の条件は変更していない。
EPMにはプログラムとページテーブルなども含まれるので、256 KiBが
EPM全体のサイズという意味ではない。

`run-guest.sh` は展開前にprocfsとsysfsを確認し、未マウントならマウントする。
`init=/bin/bash` では通常の初期化処理がなく、makeself内のdfが
マウント情報を取得できない警告にも対応する。

その後、最大order境界条件とDMA32制約を修正し、CMAの高位RAM予約を追加した。
現在はfree memoryを2 MiBに戻し、さらに256 MiB設定の実SM試験を用意している。
更新・ビルド手順は [大容量EPM](keystone-large-epm.md) を参照。

## 現在の検証

過去の模擬試験とそのログは削除した。
[実QEMU統合試験](../verifications/README.md)で、実enclaveの作成・実行・
データ往復・破棄を確認する。ドライバ交換後、ユーザーの実ゲストで
4 MiB EPMのbuddy確保とOCALL往復PASSが報告された。
新しい256 MiB統合試験の実行成功は未確認。

## 再実行

開発用ホストのリポジトリルートで：

```bash
git pull
make -C verifications/edgecall guest
```

既に共有ディレクトリをマウントしたQEMUゲストで：

```bash
cd /mnt/host/verifications/edgecall
bash run-guest.sh --data-only
echo "exit=$?"
```

今回の変更にファームウェア、カーネル、rootfsの再ビルドは不要。
`PASS GUEST: 8 binary OCALL round trips and invalid return rejection` と
`exit=0` を確認する。PMPの実参照拒否は別途probe付きで確認する。
再び確保に失敗する場合は、新しい要求ページ数のログと
`cat /proc/meminfo`、`cat /proc/buddyinfo`、`dmesg` を保存する。
