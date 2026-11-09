# Milk-V Jupiter2 技術仕様・比較スライドの調査記録

調査日：2026-11-09（JST）

PowerPoint：`milkv-jupiter2-specs-comparison.pptx`（24枚）

## 読み方

- メーカー公称、指定リポジトリの実機記録、第三者ベンチマークを区別。
- 新たな実機測定は行っていない。
- EEMBC一覧877件を25件ずつ取得。JH7110 #13585を確認、SpacemiT/K3/P550該当を確認できず。
- EEMBC API値はscore／mhz／cmpmhzを10000で除算。threads=0は一覧JSでは1扱い。
- RuyiSDK Jupiter2：GCC 15228.812914、Clang 9520.512377。P550：GCC 8062.742799、Clang 6000.436395。
- 実行hart・周波数が不明のため、同条件のCPU性能比としては扱わない。
- Security Engineと独立セキュアCPU、RT24管理コア、OP-TEEを区別。
- 起動シーケンスはBSP/U-Boot系とK3 Ubuntu UEFI系を分離。

## スライドと出典

### 1. Milk-V Jupiter2 技術仕様とRISC-V機比較

出典：S1, S2, S3



### 2. 先に押さえるべき4点

出典：S1, S2, S3, S20, S21



### 3. アーキテクチャモデル：用途別の3サブシステム

出典：S2, S3, S27, S25



### 4. キャッシュ階層：X100とA100を分けて読む

出典：S2, S4



### 5. メモリ仕様とストレージ

出典：S1, S2, S3



### 6. Jupiter2のI/O：ボード上に出ているもの

出典：S1



### 7. OpenSBIのバージョン：実機とソースを照合

出典：S3, S5



### 8. Security processor：確認できた仕様と役割

出典：S2, S26, S30, S31



### 9. RT24管理コア、OP-TEE、Keystone SMの関係

出典：S2, S26, S27



### 10. Boot sequence：ベンダー系の起動と常駐サービス

出典：S2, S3, S28



### 11. Boot sequence：UEFI構成とセキュアブートの境界

出典：S28, S29, S26



### 12. 購入候補との比較①：CPU・ISA・キャッシュ

出典：S1, S2, S6, S7, S8, S9, S10, S25



### 13. 購入候補との比較②：メモリ・I/O

出典：S1, S6, S7, S8, S9



### 14. 購入可能性：販売経路と在庫を分けて確認

出典：S11, S12, S13, S14, S15, S16, S7



### 15. CoreMark：EEMBCへの登録を確認

出典：S17, S18, S19

取得APIのscore、mhz、cmpmhzは10000で除算。threads=0は一覧JSで1として表示。JH7110は4コア搭載機だが登録値は単一実行。

### 16. CoreMark：Jupiter2とP550の公開実行例

出典：S20, S21

Jupiter2：Bianbu4.0.1、文書更新2026-07-22。P550：Fedora44、2026-09-17。GCC16.1.1 20260624とClang22.1.8。-O2だがmarchは異なる。周波数・affinity未固定なので純粋なコア性能比ではない。

### 17. CoreMark以外：何を比較するとよいか

出典：S23, S24, S25



### 18. 公開HPL結果：比較の出発点になる

出典：S23



### 19. K3の公開測定と、追加できる比較

出典：S22, S24, S25



### 20. 推奨する実測条件：X100とA100を必ず分離

出典：S2, S3, S20, S21, S24



### 21. 選定上の結論

出典：S1, S2, S3, S20, S21, S23



### 22. 出典一覧 1/3

出典：S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11



### 23. 出典一覧 2/3

出典：S12, S13, S14, S15, S16, S17, S18, S19, S20, S21, S22



### 24. 出典一覧 3/3

出典：S23, S24, S25, S26, S27, S28, S29, S30, S31



## URL一覧

- S1 [Milk-V Jupiter2 製品仕様](https://milkv.io/jupiter2)
- S2 [SpacemiT K3 Datasheet v1.9（CPU・メモリ・Security）](https://github.com/spacemit-com/docs-chip/blob/main/en/key_stone/k3/k3_docs/k3_ds.md)
- S3 [指定リポジトリ：Jupiter2実機調査](https://github.com/yutaka-ishikawa/public/blob/b98b974b7415db6060d47930a1de79ce91f7e54e/doc/jupiter2-hardware-compatibility.md)
- S4 [指定リポジトリ：K3 CPU Device Tree](https://github.com/yutaka-ishikawa/public/blob/b98b974b7415db6060d47930a1de79ce91f7e54e/linux-k3/arch/riscv/boot/dts/spacemit/k3-cpus.dtsi)
- S5 [OpenSBI 1.6 ソース定義](https://github.com/yutaka-ishikawa/public/blob/b98b974b7415db6060d47930a1de79ce91f7e54e/opensbi/include/sbi/sbi_version.h)
- S6 [Milk-V Megrez 仕様](https://milkv.io/megrez)
- S7 [HiFive Premier P550 公式](https://www.sifive.com/development-platforms/hifive-premier-p550)
- S8 [VisionFive 2 公式仕様](https://doc-en.rvspace.org/VisionFive2/Quick_Start_Guide/General/specification_pb.html)
- S9 [Orange Pi RV2 公式](http://www.orangepi.org/html/hardWare/computerAndMicrocontrollers/details/Orange-Pi-RV2.html)
- S10 [SpacemiT K1 キャッシュ仕様](https://github.com/spacemit-com/docs-chip/blob/main/en/key_stone/k1/k1_docs/k1_ds.md)
- S11 [ARACE Jupiter2 販売・variant情報](https://arace.tech/products/milk-v-jupiter-2.js)
- S12 [ARACE 現行販売製品・在庫](https://arace.tech/products.json?limit=250)
- S13 [Milk-V Jupiter NX 仕様（キャリア別途）](https://milkv.io/jupiter-nx)
- S14 [Orange Pi RV2 販売ページ](https://www.anyuno.com/en/products/orange-pi-rv2)
- S15 [VisionFive 2 販売ページ](https://youyeetoo.com/collections/top-sellers/products/visionfive-2-v13b-local-warehouse)
- S16 [P550 Arrow 販売ページ](https://www.arrow.com/ja/products/hf106-001/sifive-inc.html)
- S17 [EEMBC CoreMark 登録一覧](https://www.eembc.org/coremark/scores.php)
- S18 [EEMBC CoreMark 一覧API（25件ずつ取得）](https://www.eembc.org/coremark/ajax/get_coremark_scores.php?itemsPerPage=25&offset=0&stableSortStack=%5B-11%5D)
- S19 [EEMBC JH7110 登録 #13585](https://www.eembc.org/viewer/?benchmark_seq=13585)
- S20 [RuyiSDK Jupiter2 CoreMark 実行例](https://github.com/ruyisdk/board-docs/blob/main/Jupiter2/Coremark/README_zh.md)
- S21 [RuyiSDK Premier P550 CoreMark 実行例](https://github.com/ruyisdk/board-docs/blob/main/Premier_P550/Coremark/README_zh.md)
- S22 [K3 CoM260 CoreMark 投稿](https://openbenchmarking.org/result/2604063-NE-K3335184577)
- S23 [HPL 公開結果一覧（community）](https://github.com/geerlingguy/top500-benchmark/blob/master/README.md)
- S24 [Phoronix RV2／P550／VisionFive2比較（2025-04-23）](https://www.phoronix.com/review/orange-pi-rv2-benchmarks)
- S25 [CNX K3初期測定（2026-01-23、試作環境）](https://www.cnx-software.com/2026/01/23/spacemit-k3-16-core-risc-v-soc-system-information-and-early-benchmarks/)
- S26 [K3 Security Engine調査と固定ソースへのリンク](https://github.com/yutaka-ishikawa/public/blob/b98b974b7415db6060d47930a1de79ce91f7e54e/doc/jupiter2-security-engine-rng.md)
- S27 [ESOS：RT24×2、614.4 MHz、各5MB実行RAM](https://github.com/spacemit-com/esos/blob/e5cd04556b98c0054256f165cc2c6d4d25326a15/README.md)
- S28 [K3 Ubuntu Images：起動シーケンス](https://github.com/spacemit-com/K3-Ubuntu-Images/blob/main/README.rst)
- S29 [U-Boot：K3 BootROM認証形式](https://docs.u-boot.org/en/latest/board/spacemit/smtimage.html)
- S30 [OP-TEE K3 platform（WTM／TE200）](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/platform_config.h)
- S31 [OP-TEE K3 RNG driver](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/k3_prng.c)
