# Jupiter2のAK管理設計：OP-TEEを使わないM-mode HUK導出とSPI NOR保存

- 更新日：2026年11月11日（日本時間）。
- 対象：Milk-V Jupiter2／SpacemiT K3、OpenSBIに統合したKeystone SM。
- 状態：**再設計・実装計画。HUK移植、AK保存、実機の保護・電源断試験は未実施。**
- 本版は従来のOP-TEE／trusted serviceによるHUK供給案を置き換える。
- 対象鍵：TPM 2.0形式Quote専用ECDSA NIST P-256 AK。
  既存SBI 3002のEd25519 SM鍵、device root、sealing keyとは独立させる。
- 関連：[Quote統合設計](keystone-tpm2-quote-design.md)。
  同文書第9節の一般候補のうち、本開発で採用する構成は本書で具体化する。
- 初期のlifecycle方針：端末の運用期間中はAK固定、通常rotationなし。
  これは開発案の明示的な仮定であり、将来rotationを追加するときは設計を改訂する。

## 1. 採用構成

OP-TEE、TA、tee-supplicant、OP-TEE MPXY dispatcherをAK管理に使用しない。
U-BootからHUK、派生鍵、AKの平文を渡す経路も採用しない。

**OpenSBI／SMがM-modeでeFuse bank1・2を読み、HUKとAK専用wrap keyを自分で導出する。**
HUKとwrap keyをSBIで返さない。Linuxの役割はSPI NORの暗号化blobを運ぶことだけである。

```text
初期boot（Linux／S-mode U-Bootへ制御を渡す前）
  OpenSBI：全hartのPMP、DMA／IOPMP、共有clock/resetの保護を確立・検査
    → M-mode eFuse reader：bank1・2を限定読出し
    → SM：HUK → AK専用K_wrapを導出、bank平文とHUKを消去
    → K_wrapだけをSM保護RAMへ保持
    → Linux boot（AKはまだ未ロード）

Linux起動後、最初の署名前
  Linux管理tool → MTDでak-storeのslot A/Bを読む
    → 限定SBI LOADで暗号化blobをSMへ渡す
    → SM内でAEAD検証・AK復号・公開鍵再計算
    → 外部登録済みidentityとの運用上の照合
    → AK ready → SM内でQuoteを署名
```

OpenSBIからLinux関数を呼び出さない。Linux側がSBIを呼び出す。
Linuxがblobを渡すまでAK署名はunavailable。通常boot／enclave基本実行は継続可能とする。

## 2. 既存ソースから分かったこと

### 2.1 HUKの生成経路

参照するベンダーOP-TEEの固定commit：
`2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59`。

- [k3_huk.c](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/k3_huk.c)：
  bank1＝SoC vendor、bank2＝OEM、各32 bytes。
- 既存式：`HUK = SHA-256("k3-huk-v1" || bank1 || bank2)[0:16]`。
- [k3_efuse.c](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/k3_efuse.c)：
  GEUのclock/reset、reload、FUSE_READY待ちとbank読出し。
- [k3_ccu.c](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/k3_ccu.c)：
  TE200とeFuseが共有するclock/resetの初期化。
- これは非exportable hardware deriveサービスではない。
  bank平文とHUKは、処理するtrusted softwareのRAMに現れる。

本案はそのhardware手順とHUK式を参考にM-mode向けに移植する。
OP-TEE OS自体を実行時依存にしない。BSD-2-Clauseの出典・著作権・licenseを保持する。
ベンダー実装の存在は、対象実機でM-modeから正規値を読めることや隔離の実証ではない。

#### 2.1.1 eFuseはMMIO経由で読む

**K3のGEU/eFuse controllerは物理アドレス空間へメモリマップされている。**
CPUはcontrollerのデータレジスタを32-bit MMIO loadで読む。
SPI／I2C経由でも、SPI NORの読出しでもなく、LinuxのAPIも必要ない。
ただし、eFuse arrayそのものを通常RAMとして直接読むわけではない。
controllerへreloadを要求して、読出し用レジスタへ反映された値を取得する。

参照する[platform_config.h](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/platform_config.h)
には以下の物理アドレスがある。

| 対象 | 物理アドレス／範囲 | 用途 |
| --- | --- | --- |
| eFuse MMIO mapping window | [0xf0702000, 0xf0703000)、4 KiB | 参照OP-TEEのpage単位mapping |
| GEU/eFuse register base | 0xf0702800 | register offsetの基準 |
| GEU/eFuse register window | [0xf0702800, 0xf0702c00)、0x400 bytes | controllerの定義範囲 |
| GEU_CONFIG | base＋0x004＝0xf0702804 | reload制御 |
| EFUSE_STATUS | base＋0x184＝0xf0702984 | FUSE_READY等の状態 |
| bank1の読出しwindow | [0xf0702924, 0xf0702944)、32 bytes | base＋0x124、HUK元のvendor bank |
| bank2の読出しwindow | [0xf0702944, 0xf0702964)、32 bytes | base＋0x144、HUK元のOEM bank |

bank1は0xf0702924から0xf0702940まで、bank2は0xf0702944から0xf0702960までを、
それぞれ4-byte刻みで8回読む。各wordをlittle-endianの4 bytesとして並べ、各32 bytesを構成する。
bank番号から単純にbase＋bank×32を計算してはいけない。
bank0等の配置も含め、元コードはbank別のregister offset tableを使っている。

参照OP-TEEは物理MMIOをsecure仮想アドレスへmappingしてio_read32()を使う。
OpenSBIへの移植ではOP-TEEのmapping APIを使わず、
実行時の物理アクセス条件を確認してOpenSBIのreadl()/writel()等のMMIO helperへ置き換える。
M-modeは通常のload/storeでS-modeのsatpによるページ変換を使わないが、
MPRV等の実効特権、PMPのlock／Smepmp、SoC側bus権限、clock/reset状態によってアクセスは制限される。
M-modeなら無条件に読めると仮定しない。

#### 2.1.2 読出し前の手順

参照k3_efuse.cの順序は以下である。値は固定commitの実装根拠であり、
現在の修正版OpenSBIに実装済み、または対象実機で確認済みという意味ではない。

1. k3_ccu_ensure(K3_CLK_TE200, ...)で関連clock/resetを準備する。
   eFuseはTE200／WTM側の共有制御に依存する。
2. GEU_CONFIG（0xf0702804）へ0x0c424000を書いてreloadを要求する。
3. 200,000 μs（200 ms）待つ。
4. 同じGEU_CONFIGへ0x0c024000を書く。
5. EFUSE_STATUS（0xf0702984）のbit1、FUSE_READYをpollする。
   readyを待つ上限は3,000,000 μs（3 s）。
   readyにならなければ読出し成功にせず、AK providerを利用不可にする。
6. ready後にbank1・2の32-bitレジスタを各8個読み、SM専用bufferへ格納する。
7. HUKとK_wrapを導出し、bank bufferとHUK temporaryを明示的に消去する。

```text
GEU clock/reset準備
  → GEU_CONFIGへreload要求を書込
  → settle待ち
  → reload開始
  → EFUSE_STATUS.FUSE_READY待ち
  → bank1・2のデータレジスタをMMIO read
  → SM RAMでHUK導出
```

この制御レジスタ書込みは**既存fuse内容を読み出すためのreload操作**であり、
新しい秘密値を焼くfuse programmingではない。
ただしhardware制御への書込みは伴うので、「一切のMMIO書込みを行わない読出し」とは区別する。
汎用efuse burn／program経路は移植対象にしない。

参照OP-TEEは初回reload後に全12 bankをRAM cacheし、以降はcacheから返す。
本案では秘密保持範囲を小さくするため、必要なbank1・2だけを読み、
全bankの長期cacheやLinuxへのraw bank返却を追加しない。

#### 2.1.3 MMIOであることと秘密保持は別

MMIO読出しでbank平文を取得できる方式なので、秘密保持は
「このMMIOとSM RAMへ誰がアクセスできるか」に依存する。
LinuxのDTからbank1・2を除くだけでは不十分である。
第4節でCPUのPMP、DMA／busのアクセス制御、clock/reset所有権、
MMIO fault emulation、debugとboot chainを一体で扱う。

0x400-byte controller範囲と4 KiB mapping windowは区別する。
page単位等で保護する場合、同じpage内にある他機能への影響も調べる。
参照ソースのアドレス定義は確認できたが、実機でのbank内容、
provisioning、read lock、bus許可は別の確認事項である。

### 2.2 SPI NORの実測

[採取手順・提供結果](jupiter2-spi-nor-inspection.md)を根拠とする。

| 項目 | ユーザー提供の観測値 |
| --- | --- |
| MTD master | mtd0、d420c000.spi |
| 公開容量 | 0x800000＝8 MiB |
| erase単位 | 0x1000＝4 KiB |
| 現uboot区画 | NOR [0x210000, 0x800000)、0x5f0000 bytes |
| 全0xffの連続末尾 | NOR [0x45e000, 0x800000)、3720 KiB |

元dumpのSHA-256、採取時刻、chip型番は提供されていない。
読出しと消去状態を確認した結果であり、書込み成功や予約用途なしを示すものではない。
現在のMTD区画表は全8 MiBを覆っており、未割当領域はない。

### 2.3 Linux eFuse公開範囲

[Linux DT](../linux-k3/arch/riscv/boot/dts/spacemit/k3.dtsi)はbank0とbank7をnvmemへ公開する。
bank1・2を通常nvmemとして公開していないことは参考になるが、隔離の証明ではない。
侵害されたLinuxはDT／driverを変更できる。CPUの物理アクセス、DMA、
clock/reset、MMIOエミュレーション経路まで含めて制限する必要がある。

## 3. 保存方式の候補と採否

| 方式 | 判定 |
| --- | --- |
| **M-mode HUK導出＋AEAD＋SPI NOR、Linux MTD保存代行** | **本案で採用** |
| OpenSBI直接NOR driver | 後段の選択肢。初期実装には不要。Linux起動前のAK署名が要件になれば再検討 |
| Linux filesystemへの暗号化blob保存 | 技術的には可能。本案の保存先は専用SPI NOR区画に統一 |
| HUKからAKを直接導出 | 未採用。初期provisioningで生成した独立AKをwrapして保存する |
| SM揮発AK | 永続identityを満たさないため未採用 |
| firmwareへ秘密鍵／共通wrap keyを埋込み | 開発専用。実機のAK保護には未採用 |
| OP-TEE storage／TA | 今回の設計では使用しない |
| RPMB／secure processor／外付けTPM | 初期設計の依存にしない。将来rotationの保護anchorを要する場合の別検討 |

## 4. M-modeのeFuse・HUK実装

### 4.1 起動順序とアクセス保護

秘密読出しは、SMの保護RAMを確保し、全hartのCPUアクセス制限とDMA制限を確認した後、
最初の非trusted S-mode実行前に、boot hartが一度だけ実施する。
S-mode U-BootもLinuxと同じ非trusted側として扱う。
前段で別のS-modeコードが先行する実機構成なら、その期間の保護も成立条件に加える。

保護対象：

1. SM code／data／stack、K_wrapとAKの平文領域。
2. GEU/eFuse controller（base 0xf0702800、参照実装のregister windowは0x400 bytes）。
3. eFuse reload、共有TE200／WTM clock/reset、アクセス権を変更する制御レジスタ。
4. IOPMP／bus権限設定をLinuxが解除できないための設定領域。

PMPの粒度・残entry数に応じ、最小の実現可能なpage／windowを予約する。
bank1・2の32-byte範囲だけをPMPで守れると仮定しない。
通常S-modeのU-Boot／Linuxの直接MMIOと、OpenSBIのfault emulationの両方で
保護領域の読書きを拒否する。既存の汎用MMIO代行を抜け道にしない。

PMPはCPU側でありDMA対策ではない。GEU／SM RAMへ到達するDMA masterの
bus／IOPMP保護と、その再設定防止を実機仕様に基づいて構成する。
特定のIOPMP tableの存在やPMP設定成功だけで「SM専用」と判断しない。
debugと悪意あるM-modeは別境界であり、secure boot／debug lifecycleを要件とする。

### 4.2 clock/resetとreload

参照手順は、clock確保、reload要求、settle、reload開始、FUSE_READYの期限付きpoll。
元コードのsettle 200 ms、ready timeout 3 sは移植の出発点であり、実機仕様と照合する。
timeoutやbus faultはAK機能の失敗として扱い、無限待ちにしない。
長いspinlockを全待ち時間にわたって保持せず、初期化中stateとboot同期で直列化する。

参照CCU処理はAES関連制御へ0x1cff003fを全32-bit書き込む。
この値を既存Linux／OpenSBIとの共有状態へ無検討に適用しない。
保護後のM-mode側を唯一のwriterとし、初期値・更新範囲・readback・副作用を照合する。
Linuxのefuse、crypto、RNG、clock/reset driverが同じ制御を触る場合は
対象機能を無効化するか、用途を限定したM-mode仲介へ変更する。
Linux側の通常mutexだけではM-modeとの所有権を保証しない。

clock gating、reset、suspend／resumeで保護や鍵を失わない規則も定義する。
初期実装で安全なresumeを提供できなければ、そのAK profileでは該当遷移を明示的に未対応にする。

### 4.3 限定読出しと秘密消去

bank1のregister offsetは0x124、bank2は0x144、各32 bytes。
register offsetとU-Bootのflat bank offsetを混同しない。
M-mode readerは必要な2 bankだけを読み、全bankを長期cacheしない。
参照実装と同じlittle-endian byte streamを構築する。

どちらかのbankが全ゼロ、読出し失敗、timeout、未対応lifecycleなら正規providerを拒否する。
全ゼロでないことだけでprovision済み／十分なentropy／読出し正常と断定しない。
fuse lock／provisioning状態の意味はvendor仕様と照合する。

公開の開発HUKへのfallback、秘密bank／HUKの値やfingerprintをlogする検証コードは移植しない。
公開するhealthは初期化結果・provider・拒否理由の分類に限定する。
HUK、bank一時buffer、hash／KDF temporaryを明示的に消去する。

### 4.4 HUKとK_wrapの導出

初期案は参照ソースと同じHUK式を採用し、AK用KDFを明確に分離する。

```text
HUK16 = SHA-256(ASCII("k3-huk-v1") || bank1[32] || bank2[32])[0:16]

K_wrap[32] = HKDF-SHA256(
  IKM  = HUK16,
  salt = SHA-256(ASCII("Jupiter2/AK-wrap/extract/v1")),
  info = canonical_encode("Jupiter2/AK-wrap/v1",
                          product_domain, device_binding),
  L    = 32)
```

文字列末尾NULは含めない。canonical_encodeの長さprefix・endiannessは実装前に固定する。
device_bindingは非秘密の安定IDであり、秘密entropyの代用にはしない。
firmware hashは導出入力に含めず、承認された通常更新で既存AKを復号可能にする。
変更時は別format／migration設計を必要とし、無断でlabelや式を変更しない。

HUK16は128 bit長であり、AES-256用に32 bytesへ展開しても256 bitのentropyにはならない。
実効強度は元fuseのentropy・秘密保持・HUK16の長さに制約される。
OP-TEEから独立した新しい256-bit root式へ変更する場合は、互換性と脅威評価を別途行う。

K_wrapはSM内部だけに保持し、AK LOAD／wrapが不要になれば消去できる構成とする。
起動session中の再LOADを許すなら、K_wrap保持期間を明示して保護する。
非exportable hardware keyという表現は使用しない。

## 5. AKの生成・blob・署名

AKは信頼できるentropyと保守対象libraryのP-256生成処理でSM内生成する。
counter／timestampだけをseedにしない。WTM RNGの実機healthも別に確認する。
P-256演算、SHA-256、HKDF、AES-GCMは保守対象libraryの最小構成を使う。
TE200 acceleratorは必須依存にせず、ソフトウェア暗号を初期経路とする。

blobは最大4 KiB、AES-256-GCM（nonce 96 bit、tag 128 bit）を初期案とする。

```text
AAD:
  magic, format_version, header_length, total_length, algorithm_id
  product_domain, device_binding, profile_id, fixed_generation=1
  provisioning_id, AK_public_Name, public_key, nonce
ciphertext:
  P-256 private scalar || 必要最小限のprivate metadata
tag:
  GCM tag
```

- 形式、field長、encoding、reserved field規則を固定する。
- headerを認証前に信用せず、上限・overflow・versionを先に検査する。
- 認証後にscalarの範囲と公開鍵／Nameを再計算し、一致しなければ拒否する。
- nonceは信頼できる乱数で生成。同一wrap keyで再使用しない。
  再送・複製では完成済みblob bytesを再利用し、同じnonceで別内容を再暗号化しない。
- LOAD後は同じAKの再投入のみidempotentに許す。
  異なる公開鍵への実行中切替、任意秘密鍵import、汎用unwrap、任意digest署名は提供しない。
- Quote署名時はSMが構築した許可対象だけを署名する。
  Linuxには公開identityと署名だけを返す。
- workspace／stack／panic／log／共有bufferへの秘密残留を確認する。
  AEADは保存AKを守り、Quote署名の改ざんは外部の公開鍵検証で検出する。

## 6. SPI NOR区画とLinux保存代行

### 6.1 予約候補

| 領域 | NOR絶対範囲（終了を含まない） | サイズ |
| --- | --- | --- |
| 縮小uboot | [0x210000, 0x7f0000) | 0x5e0000＝6016 KiB |
| ak-store | [0x7f0000, 0x800000) | 64 KiB |
| slot A | [0x7f0000, 0x7f8000) | 32 KiB |
| slot B | [0x7f8000, 0x800000) | 32 KiB |

各slotは別の4 KiB erase block群とし、同じ完成済みAK blobを複製保存する。
A/Bは別世代の鍵ではない。32 KiBのうちblob上限4 KiB以外は0xff paddingとして予約する。
通常boot／署名では書込みを行わず、初回保存または同一blobの修復時だけ書く。

移行前にU-Boot container、env、backup、update／recoveryの用途と最大範囲を確認する。
cmdlinepart、DT、bootloader layout、update manifestを同じ配置へ変更する。
U-Boot image上限0x5e0000と、OpenSBI現区画上限0x60000をbuild時にも別々に検査する。
通常更新でak-storeをeraseしない。旧layout／旧recoveryの使用を制限する。
区画変更・NOR programは本設計作成時点で未実施。

### 6.2 管理toolと限定SBI

Linux MTD toolはak-storeというname、size 0x10000、erase size 0x1000を確認し、
mtd番号を固定しない。master mtd0やubootを指定された場合は拒否する。
悪意あるLinuxはこの検査を迂回可能なので、tool側検査は誤操作対策である。

内部責務に対応する操作名は以下とし、SBI番号は既存ABI確認後に固定する。

| 操作 | 責務 |
| --- | --- |
| STATUS | provider、HUK初期化状態、AK ready、Name、profileを返す |
| LOAD | 上限付きblobをSMへcopyし、認証・復号して固定AKをロード |
| PREPARE_ONCE | 認可済みprovisioning session内でAKと完成済みblobを一度生成 |
| VERIFY_SAVED | 再投入blobが準備済みbytes／公開identityと一致するか検証 |
| GET_PUBLIC | AK公開鍵／Nameと非秘密metadataを返す |
| Quote内部署名 | AK managerを内部利用。汎用の秘密鍵取得・digest署名APIはなし |

初回provisioning：
1. 管理者認証とlifecycle条件を満たしたsessionだけでPREPARE_ONCEを許可する。
2. Linuxが同じblobをAへerase／program、完了待ち、readbackする。
3. Bにも同じbytesを保存し、readbackする。
4. SMへ再投入して一致を確認し、外部検証者へAK公開identityを登録する。
5. 通常運用firmwareではPREPARE_ONCEを無効化し、LOADだけを提供する。

Linuxのreadback成功は物理的なdurabilityの信頼できる証拠ではない。
偽装、保存抑止、削除によるDoSは残る。外部登録前のcold boot試験も管理手順に含める。
MTD character I/Oでerase／programを扱い、short write、timeout、driver完了規則を確認する。
OpenSBIがLinuxへ同期callbackする仕組みは作らない。

通常LOAD：
- A/B両方が正規で同じ公開identityなら一方をロードする。
- 片方だけ正規ならそのblobをロードし、劣化状態を報告する。
- 両方正規でも別identityならconflictとして拒否し、最大番号等で選ばない。
- 両方不正／欠落ならAK unavailable。自動生成しない。
- 修復は正規blob bytesの複製だけとし、異なる鍵の生成・置換を伴わせない。

公開鍵の外部pinがない状態では、同一device上で過去に作られた別の正規blobを
SMが確実に識別することはできない。登録済みNameの照合責務を外部検証者に置く。

## 7. 固定AKの仮定とロールバック

### 7.1 初期開発で省略するもの

端末の運用期間中、生成するAKは一つで、A/B、backup、修復は全て同じAKとする。
通常rotation、wrap key epoch更新、異なるAKへのmigrationを初期実装から外す。
この仮定内で同じblobを過去版へ戻してもAKは変わらないため、
**AK世代rollback用の独立counter／anchorを初期必須機能にしない。**

これは一般的なrollback防止を提供するという意味ではない。
profileは「固定AK・local rollback anchorなし」と表示する。

### 7.2 NORだけでは一度限りの生成を永続強制できない

PREPARE_ONCEのonceはprovisioning session内の再実行防止であり、
NOR全消去後にもhardwareで「生涯一度」を証明する機構ではない。
初期化済みflagや許可tokenを同じNORへ保存しても、過去へ戻せる。
署名付きprovisioning要求も、nonce／永続stateなしでは再送問題が残る。

したがって以下を必要とする：
- 通常運用imageは新規生成を提供せず、認可された製造／管理手順でだけ生成する。
- 外部検証者が端末とAK Nameを固定登録し、自動再登録を許さない。
- 古いprovisioning imageを起動させないfirmware lifecycleとboot認証を評価する。
- 別AKが作られる可能性までローカルで排除する要件なら、保護stateを追加し設計を変更する。

既に同じHUKで複数AKを発行したdeviceは、固定AK仮定を自動では満たさない。
旧blobを全て消したという前提に依存せず、外部pinまたは保護anchorが必要となる。

### 7.3 残る攻撃と復旧

| 操作／攻撃 | 結果・対策 |
| --- | --- |
| blob改変、別deviceのblob | AEAD／device binding検証で拒否 |
| 同じAKの古いblob・backup復元 | 仮定内では受理可能。鮮度は保証しない |
| 同一deviceの異なる正規AK blob | 固定AK仮定外。外部pinで拒否、局所検出には保護stateが必要 |
| NOR／両slot削除 | DoS。自動再生成しない |
| 古い脆弱なOpenSBI／provisioning image起動 | 固定AKでも危険。secure bootとfirmware anti-rollbackの対象 |
| 認可・失効policyやPCR／counterの差戻し | AK固定では解決しない。別の保護設計が必要 |
| AK漏えい・喪失 | 外部で失効し停止。同じAKのbackup復旧以外は新identityの管理手続き |

失効を復元可能なblob内のflagだけで管理しない。
再provisioning／board交換は旧identity失効と別identity登録を伴い、
初期仮定の継続ではなく明示的な運用変更として扱う。

## 8. 修正計画

最新HEAD、root／対象directoryのAGENTS.mdを確認して実装する。
以下はmodule責務案であり、既存のsecurity providerへ統合し二重frameworkを作らない。

| 段階 | 修正内容 | 完了条件 |
| --- | --- | --- |
| P0 保護設計 | GEU、SM RAM、clock/reset、bus／IOPMPを全hartで予約。Linux driverとの所有権を整理 | 秘密を読まずに保護設定と拒否経路を検査できる |
| P1 HUK移植 | eFuse限定reader、clock/reset、reload timeout、HUK16とHKDFをM-modeへ移植 | 正規providerを識別、開発fallbackなし、秘密temporary消去 |
| P2 暗号・AK | P-256、SHA256、HKDF、GCM、固定AK managerとblob parser | version・上限・公開鍵整合・entropy故障拒否を確認 |
| P3 NOR移行 | uboot縮小、ak-store、update／recovery修正、MTD管理tool | 64 KiB予約と既存boot起動、境界外書換えなし |
| P4 SBI・lifecycle | LOAD、STATUS、管理専用PREPARE_ONCE、VERIFY_SAVED、内部署名 | Linux起動後にロード。通常運用で新規生成不可。秘密exportなし |
| P5 実機確認 | clock競合、CPU／DMA隔離、cold boot、NOR書込み・電源断、更新回帰 | 確認できた保証だけをprofileへ反映 |
| P6 外部運用 | 公開Name pin、失効、backup、喪失／再配備手順 | 旧／未登録identityの自動受入れなし |

想定変更箇所：
- keystone/sm/src/platform/spacemit_k3/：eFuse、clock/reset、HUK導出。
- keystone/sm/src/security/：固定AK、blob、限定API、秘密消去。
- opensbi/：PMP／domain／MMIO emulation拒否、設定・初期化順、限定SBI接続。
- linux-k3/：GEUと共有clock/resetの所有権調整、partition。
  bank0／7の既存利用は必要性を確認し、直接読出しを残すか限定仲介するか決める。
- Linux管理tool：MTD I/OとSBI転送。OP-TEE libraryやdeviceへ依存しない。
- verifications/：実Linux→OpenSBI→SM検証、既存3001／3002／3003回帰。
- doc/：本書、統合設計、採取手順と実機結果を同期する。

OP-TEEのmutex／仮想MMIO／initcallをそのまま持ち込まず、
OpenSBIのtimer・MMIO・boot同期へ置き換える。
eFuse burn、汎用raw read、汎用deriveのSBIは追加しない。

## 9. テスト計画

### 9.1 環境と判定

暗号の既知vectorとblob encodingの独立期待値を確認した後、
実K3 QEMU guestのLinux→OpenSBI→SM経路で機能・異常系を検証する。
SBI／PMP／CSRのmockやproduction関数抽出による代替試験を作らない。
QEMUに実eFuse／IOPMP／NOR modelがない部分は、明示した開発provider／file backendでのみ試し、
正規HUK、実bus隔離、実NORのPASSと区別する。

既存手順はmake -C verifications guest、
make -C verifications kernel-modulesで準備し、
bash verifications/run-all.shはsystem guest内だけで実行する。
実機はまず保護・非秘密statusを確認し、その後に予約済みNOR区画で試験する。
eFuse burn、秘密dumpは試験に含めない。

### 9.2 受入試験

| ID | 試験 | 期待結果 |
| --- | --- | --- |
| NAK-01 | OP-TEEなしのbuild／boot／LOAD／Quote | /dev/tee、TA、supplicant、OP-TEE MPXYなしで動作 |
| NAK-02 | 全hartのGEU直接MMIOとfault emulation | S-modeからの秘密取得・制御変更を拒否。秘密をlogしない |
| NAK-03 | Linux DMA／clock/reset／IOPMP再設定の試行 | 保護を解除できない。実機仕様と対応する試験で評価 |
| NAK-04 | eFuse読出し失敗、zero bank、reload timeout | AK不可、公開開発HUKへfallbackせずboot基本機能は維持 |
| NAK-05 | HUK／HKDF／AEAD／P-256の既知vector | 独立期待値と一致。開発vectorと実機秘密を混同しない |
| NAK-06 | 初回生成→A/B保存→cold boot | 同じ公開鍵／Nameで署名検証成功 |
| NAK-07 | 別device、tag／AAD／nonce／ciphertext改変 | LOAD拒否、部分平文非公開 |
| NAK-08 | 不正長、overflow、未知format、scalar／公開鍵不整合 | 安全に拒否 |
| NAK-09 | Aだけ正規、Bだけ正規、両方欠落、別identityのA/B | 正規片側は劣化報告。欠落／identity競合は自動生成・切替なし |
| NAK-10 | erase／program／readback途中の電源断 | 片側正規なら同じAKへ復旧。両方不正ならunavailable |
| NAK-11 | LOAD再送、並行LOAD／署名、異なるAK投入 | idempotent、鍵混在なし、実行中切替拒否、deadlockなし |
| NAK-12 | Linuxが保存／readback成功を偽装 | 秘密流出なし。durability保証不能／DoSを結果へ明示 |
| NAK-13 | 同じAKの旧blobと同一device別AKの旧blob復元 | 同じAKは許容。別AKは外部pin拒否、local保証の限界を記録 |
| NAK-14 | 通常運用で新規生成／raw read／derive／任意署名 | 拒否。NOR全消去を初回生成許可の根拠にしない |
| NAK-15 | 通常firmware更新、旧provisioning imageへの差戻し | 前者は同じAKを維持。後者はboot policyで拒否することを実証 |
| NAK-16 | ak-store境界、MTD番号変化、旧update／recovery | boot領域非変更、AK保存維持、旧toolの制限を確認 |
| NAK-17 | log／panic／共有buffer／temporary消去 | bank、HUK、wrap key、AKを出力しない。監査範囲を記録 |
| NAK-18 | suspend／resume、RNG／crypto／nvmem／clockの競合 | 安全な再開か明示的未対応。鍵保護を失わない |
| NAK-19 | 既存SBI、enclave、乱数、attestation／sealing回帰 | 既存機能を維持。OP-TEE関連機能停止の影響は明示 |

CPU／DMA拒否試験では秘密bankの値を収集せず、管理された診断で拒否statusを確認する。
攻撃試験で保護を破壊する可能性がある場合は、試験環境と復旧手順を先に確保する。

### 9.3 結果の記録

commit、board revision、firmware設定、provider、全hart保護範囲、
未対応DMA／debug条件、NOR geometry、試験項目を記録する。
build成功、QEMU実行、実機実行、隔離保証を別の結果として記載する。
HUK元bankが非ゼロという結果だけで製品の秘密保持をPASSにしない。

## 10. 実装開始前に残る確認

1. bank1・2の正規provisioning状態と、M-modeからの許可された読出し。
2. GEU／関連clock/resetの正確な保護粒度、PMP資源、CPU／DMA経路。
3. vendor CCU全幅書込みの必要性と、Linux driverとの競合解消。
4. secure boot、旧firmware拒否、debug lifecycleの実機設定。
5. ak-storeの予約とupdate／recoveryの保存規則。
6. provisioning専用image／認可方式と外部AK pinの運用。
7. 暗号library version、encoding、entropy health、imageサイズ上限。

既存の実機inventoryとLinuxログを再採取することを前提にしない。
足りない項目は既存資料・ソースで照合した後、非秘密の実機診断で補う。
上記の保護が未実証の間、正規HUKで暗号化できても製品用隔離の完成とはしない。

## 11. 関連資料

- [実機構成の既存調査](jupiter2-hardware-compatibility.md)
- [実機inventory](jupiter2-hardware-inventory.json)
- [SPI NOR取得・解析手順](jupiter2-spi-nor-inspection.md)
- [storage／eFuse ARCN調査](jupiter2-persistent-storage-investigation.md)
- [WTM RNG調査](jupiter2-security-engine-rng.md)
- [OP-TEE調査](optee-milk-v-jupiter2.md)：移植元の参考資料であり実行時依存ではない。
- [K3 platform](../opensbi/platform/generic/spacemit/)
- [Linux eFuse driver](../linux-k3/drivers/nvmem/spacemit-efuse.c)

本版は設計文書の更新であり、firmware変更、実機fuse読出し、NOR書込みは実施していない。
