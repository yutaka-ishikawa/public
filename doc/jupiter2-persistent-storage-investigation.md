# Jupiter2のpersistent storageとNV Counter調査

- 調査日：2026年11月10日（日本時間）。新たな実機操作・書込み試験は行っていない。
- 結論：**永続領域が存在しないとは言えない。K3には資料上NV Counterが存在する。**
  ただし、TPM相当counterの安全な保存先として使えることはまだ確認していない。
- 設計：[PCR・counter・Quote統合設計の第9.6〜9.12節](keystone-tpm2-quote-design.md)。

## 1. 公式K3 datasheet

出典：[k3_ds.md（固定commit）](https://github.com/spacemit-com/docs-chip/blob/3ea42a6af47f2595428e2870122829dc5e8fc856/en/key_stone/k3/k3_docs/k3_ds.md)

- commit：`3ea42a6af47f2595428e2870122829dc5e8fc856`。
- 取得内容SHA-256：`e9a202a1064cd5a4273f6c65b5831f21f7ade63529b72c8b59043bcb863d4032`。
- 第2.8.3節 eFuse：4096 bit、256-bit bank×16、user customization 256 bit。
- 同節の機能欄に **“Supports 256-bit non-volatile counter (NV Counter)”** と明記。
- 第2.2.4節：eMMC 5.1 host controller対応。外部eMMCを接続する機能の記載。

これらはSoC機能の仕様であり、個々のJupiter2 boardのfuse残量・権限・provisioning・
eMMC搭載やRPMB利用可能性を示す実機試験結果ではない。
「256-bit counter」は、数値幅・bit消費型・更新上限が不明なので、2^256回のincrementが
できるという解釈をしない。user customization領域とcounterの領域が別かも未確認。

## 2. 以前提供された実機ログ

根拠：[hardware inventory](jupiter2-hardware-inventory.json)でhashを管理している
`jupiter2-info.tar.gz`の`dmesg.txt`と`running.dtb`。
ログを再読した結果であり、今回実機を動かして採取したものではない。

```text
[    2.282648] spi-nor spi4.0: supply vcc not found, using dummy regulator
[    2.319323] 6 cmdlinepart partitions found on MTD device d420c000.spi
[    2.319334] 0x000000000000-0x000000020000 : "bootinfo"
[    2.320117] 0x000000020000-0x0000000a0000 : "fsbl"
[    2.320903] 0x0000000a0000-0x0000000b0000 : "env"
[    2.321679] 0x0000000b0000-0x0000001b0000 : "esos"
[    2.322431] 0x0000001b0000-0x000000210000 : "opensbi"
[    2.323182] 0x000000210000-0x000000800000 : "uboot"
```

SPI NORが使われていることは確認できる。末尾partitionの境界は0x800000。
ただしここから全物理容量や空き領域を決め付けない。
既存領域はboot用なので、counter journalを無断で割当てない。
flashに保存できることと、過去imageへの巻き戻しを検出できることは別である。

今回参照した既存資料から、搭載eMMCの型番・RPMB容量・鍵設定は確認できなかった。
ログに見えないことを、boardにeMMC/RPMBの選択肢が存在しないという証拠にはしない。

## 3. repository内のeFuse定義とOP-TEE設定

- [K3 DT source](../linux-k3/arch/riscv/boot/dts/spacemit/k3.dtsi)に
  `fuse@f0702800`、efuse bank0/bank7、die ID等の定義がある。
  読出し用のDT nodeの存在だけでは、NV Counterの安全な更新APIは分からない。
- [K3 OP-TEE conf.mk](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/conf.mk)
  に`CFG_K3_EFUSE ?= y`とread-only accessのcommentがある。
- 同じconf.mkには`CFG_TE200_USE_DUMMY_OTP ?= y`、`CFG_K3_HUK_ALLOW_UNFUSED ?= y`があり、
  blank fuse時にpublic development HUKを使うという説明がある。
  これは開発設定であり、実機のHUKが秘密かつprovision済みであることの証拠ではない。

## 4. 次に確かめる事項

1. K3のNV Counter programming manual：表現、残量、更新単位、権限、lock、power-fail semantics。
2. 実機のfuse lifecycleとpublicな残量/能力情報。生の秘密fuse dumpは不要。
3. eMMC搭載の有無と型番、RPMB対応/容量、key provisioning、認証I/O経路。
4. OP-TEE/secure processorの正規HUK・storage backend・SM専用アクセス制御。
5. protected counter stateとAK blobの一貫したcommit・recovery・rollback拒否。

eFuse/RPMB keyのprogrammingは不可逆になり得る。今回の調査では実施していない。
文書だけの確認、QEMUでのfile-backed復旧、Jupiter2実機での耐久性・権限試験を分けて記録する。
