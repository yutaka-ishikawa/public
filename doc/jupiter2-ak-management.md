# Jupiter2のAttestation Key管理設計：HUK暗号化とSPI NOR保存

- 作成日：2026年11月11日（日本時間）。
- 対象：Milk-V Jupiter2／SpacemiT K3、修正版OpenSBIに統合したKeystone SM。
- 状態：**設計・修正計画。実装、実機での鍵保存、電源断試験は未実施。**
- 対象鍵：TPM 2.0形式Quote専用のECDSA NIST P-256 AK（Attestation Key）。
- 既存SBI 3002のEd25519 SM鍵、device root、sealing keyとは独立させる。
- 範囲：AKの生成、暗号化保存、読込み、署名時の保持、世代管理、更新、失効、消去。
  PCR／IMA、Quote wire形式、汎用counterの設計は扱わない。
- 関連正本：[PCR・counter・Quote統合設計](keystone-tpm2-quote-design.md)。
  本書はAK管理の詳細案。既存仕様を変更する箇所は実装時に両文書を同期する。

## 1. 結論と成立条件

AKはSM内で生成し、HUK由来の鍵でAEAD暗号化したblobだけを専用SPI NOR区画へ保存する。
起動後はSM内で認証・復号し、署名時もAKの平文をLinuxへ渡さない。

ただし、現時点で「現在のfirmwareのまま安全に永続保存できる」とは判断しない。
以下の成立条件がある。

1. 製品固有で非公開の正規HUK、またはHUKからのderive／wrap serviceが利用できる。
2. 承認されたSMだけがAK用derive／unwrapを利用できる。公開の開発用HUKは拒否する。
3. 実機のSPI NOR容量、erase単位、partition構成、書込み経路と予約領域を確定する。
4. secure boot、firmware anti-rollback、debug／DMAの扱いを確認する。
5. ロールバックをローカルで拒否するprofileには、SPI NORと独立した保護anchorがある。

**HUKによる暗号化・認証だけでは、過去の正しいAK blobへの巻き戻しを防げない。**
保護anchorが未提供なら、機密性・改ざん検出を目指す段階とし、
「rollback保護あり」「製品用の永続trusted AK対応済み」と表示しない。

## 2. 保存・保持方式の候補

ここでの「利用可能」は、媒体の存在、Linuxからの利用、SMからの安全な利用を区別する。

| 方式 | 平常時のAK平文 | 永続保存先 | 現状／必要な追加 |
| --- | --- | --- | --- |
| QEMU開発seed／開発firmware埋込み | SM RAM。開発image等から抽出可能 | host state／開発image | 既存の開発鍵方式を参考にできるが、独立P-256 AK providerは追加が必要。実機の秘密保持には使わない |
| SM揮発AK | PMP保護SM RAM | なし | 信頼できるentropyと生成処理が必要。再起動で変わり、固定AK登録には不適 |
| Linux経由の暗号化ファイル保存 | SM RAM | Linuxが使用できる既存filesystem | OpenSBI側block driverを不要にできる。blob転送ABIと正規HUKが必要。rootfsの保存媒体の実機型番は既存調査で未確定 |
| **HUK wrap＋既存SPI NOR** | **SM RAM** | **専用NOR区画の暗号化blob** | **本書の詳細検討対象。NORは実機ログで確認済み。空き領域、SM側driver、HUKは未確認／未実装** |
| HUKからAKを直接導出 | SM RAM、または安全なderive service | HUKと公開導出parameter | blob不要。ただしP-256 scalarの正しい導出、rotation、firmware更新時identityの規則が必要。世代parameterもrollback対象 |
| eMMC RPMB／HUK-backed secure storage | SM RAMまたはtrusted service | 認証されたsecure storage | K3はeMMC controller対応。実機のeMMC搭載、RPMB、鍵設定、安全な経路は未確認 |
| secure processor／key engineの非exportable AK | engine内 | 保護slot／secure NVM | 長期候補。P-256署名API、永続slot、SM専用認可は未確認 |
| OP-TEE trusted service | OP-TEEまたはengine内 | HUK-backed storage、可能ならRPMB | platformは存在するが、正規HUK・backend・SM共存・認可の実証が必要 |
| 外付けTPM／secure element | 外付けdevice内 | deviceの保護NVM | 既存boardへの搭載は未確認。追加hardware、driver、provisioningが必要 |

AKを平文でrootfs、DT、boot引数、共有buffer、Linux keyringだけに保存する方式は採用しない。
SPI NOR内のfirmwareへAKや共通wrap keyを埋め込む方式も製品用の鍵保護にはしない。
K3 eFuse ARCNはfirmware向けのbit消費型anti-rollback資源であり、AK保存領域や汎用更新counterとして扱わない。

## 3. 現在確認できているSPI NORとHUKの制約

### 3.1 SPI NOR

[既存storage調査](jupiter2-persistent-storage-investigation.md)の実機Linuxログでは、
`d420c000.spi`のMTD partitionは以下である。

| 範囲（開始を含み終了を含まない） | 用途 |
| --- | --- |
| 0x000000–0x020000 | bootinfo |
| 0x020000–0x0a0000 | fsbl |
| 0x0a0000–0x0b0000 | env |
| 0x0b0000–0x1b0000 | esos |
| 0x1b0000–0x210000 | opensbi |
| 0x210000–0x800000 | uboot |

これは既存partitionの境界であり、NORの全物理容量や空き領域の証明ではない。
特に`uboot`区画の未使用に見える末尾や`env`をAK用に流用しない。
既存のOpenSBI区画は0x60000 bytesで、AK機能やdriver追加後のimage／boot containerの収容性も別に検査する。

### 3.2 HUK

K3 OP-TEEの調査対象設定には、`CFG_TE200_USE_DUMMY_OTP`、
`CFG_K3_HUK_ALLOW_UNFUSED`等の開発用設定がある。
OP-TEEが起動するだけでは、HUKが製品固有で秘密である証拠にはならない。

製品profileでは、実際に選択されたprovider、fuse／lifecycle状態、開発fallbackの不使用を
公開可能なhealth情報で確認する。HUKの生値をdumpして確認する手順は設けない。
HUKが利用できないとき、timestamp、counter、公開device ID、開発固定鍵へfallbackしない。

## 4. 脅威モデルと守る対象

Linux rootによるblobの読取り、改変、削除、コピー、保存応答の偽装、過去版の復元を想定する。
NOR全体の外部書換え、古い正規firmwareへの差戻し、電源断も検討する。

- 守るもの：AK平文、wrap key、AKのdevice／用途への結合、受理する鍵世代。
- Linuxへ公開してよいもの：AK公開鍵、Name、generation、provider quality、暗号化blob。
- AEADでも防げないもの：Linuxによる保存拒否・削除等のDoS。
- PMPだけでは保証しないもの：悪意あるM-mode、debug、DMA、物理的なRAM観測。
- 侵害済みLinuxが署名を要求できる範囲は、Quote側の認可・署名対象制限で管理する。
  本書のAK providerはLinux向け任意digest署名APIを公開しない。

承認外firmwareが同じHUK derive／unwrapを自由に呼べると、blob暗号化はAK流出を防がない。
secure bootに加え、古い脆弱な承認済みfirmwareへのanti-rollbackとservice側認可が必要となる。

## 5. HUK暗号化＋SPI NOR保存の詳細案

### 5.1 providerの分離

1. HUK／wrap provider：domain separationされたwrap key導出、または非exportable wrap／unwrap。
2. NOR storage provider：固定されたAK専用slotのread／write／erase／flush／readback。
3. AK manager：blob形式、認証、公開鍵整合、generation、rotation、healthを管理。
4. anchor provider：存在する場合、確定済みの鍵世代とblob digestを保護する。
5. Quote内部署名provider：現在有効なAKだけで署名し、内部で公開鍵との整合を確認する。

公開statusは`unavailable`、`unprovisioned`、`ready`、
`rollback_detected`、`storage_error`等を区別する。
provider名、開発／製品区分、rollback保証の有無は返してよいが、秘密materialは返さない。
鍵が利用できなくても通常のenclave基本実行を不要に停止させない。

### 5.2 HUKからのwrap key

製品HUKは原則としてSMやLinuxへexportせず、hardware／trusted service内でderiveする。
実APIに応じて、SMへ限定された派生鍵を返す方式と、service内でAEADを行う方式を選ぶ。

導出は実証されたKDFを用い、入力contextをversion付きで固定する。

```text
K_wrap = KDF(HUK, label="Jupiter2/AK-wrap/v1",
             context=product_domain || device_binding || wrap_key_epoch)
```

- 公開device IDは結合用であり、秘密entropyの代用にはしない。
- AK用、sealing用、counter用、その他firmware用の鍵を分離する。
- KDFの方式、入力のencoding、長さ、serviceの認可を実装前に固定する。
- firmware hashを無条件にKDFへ含めない。通常更新で既存AKを復号不能にしないため、
  version付きpolicyと承認されたmigrationで更新する。
- `wrap_key_epoch`は公開可能だが、旧epochの受理制御はanchor／serviceのpolicyで行う。
  数字をblobに付けるだけでは失効しない。
- 派生鍵をSMへ返す場合、SM専用RAMで保持し、不要後に消去する。

### 5.3 AK生成とblob形式

AKはprovisioning専用操作で、信頼できるentropyと保守対象の暗号libraryを使って生成する。
P-256のscalar範囲をlibraryで検証し、独自のmod演算による生成を追加しない。
通常のboot／署名要求で「鍵がないから自動生成」は行わない。

初期案はAES-256-GCM（96-bit nonce、128-bit tag）とし、採用library／HUK serviceの対応、
RV64での実装、nonce規則を確定してからformatを固定する。algorithm IDは固定allowlistとする。

```text
固定header:
  magic, format_version, header_length, total_length, algorithm_id
  product_domain, device_binding, profile_id, wrap_key_epoch
  AK_generation, transaction_id, AK_public_Name, nonce
ciphertext:
  P-256 private scalar || 必要最小限のprivate metadata
tag:
  AEAD authentication tag
```

- headerの全security関連fieldをcanonical encodingのAADに含める。
- 公開鍵本体を保持する場合もAADで認証し、復号後に秘密scalarから再計算して一致を検査する。
- AK generationはprovisioning単位の64-bit世代とし、overflowを拒否する。
- nonceは同一wrap key内で再使用しない。認証済みのentropy sourceで96-bit乱数を生成し、
  暗号化回数の上限・衝突確率を採用規格に従って評価する。NOR内sequenceだけから作らない。
- 同じblobの再送・readbackは再暗号化せず、transaction内では完成済みbytesを再利用する。
- 暗号化blobのcanonical bytesにSHA-256を掛け、anchorが参照するdigestとする。
- 形式上限、endianness、予約field、padding規則を固定する。
- 認証前はheaderを未信頼として扱い、長さ・overflow・versionを検査する。
  generationが大きいだけで鍵を採用しない。
- tag失敗や公開鍵不一致で署名不可にする。秘密の部分復号結果を出力しない。

AEAD nonceの乱数が信頼できない場合は保存不可にする。
nonce misuse耐性のある別方式への変更は、対応libraryとformatを別途レビューする。

### 5.4 専用partitionとslot配置

AK専用の予約partitionを新設し、slot Aとslot Bをそれぞれ異なるerase block群へ置く。
必要容量は実機のerase geometry、blob最大長、更新頻度、wear条件から決める。
offsetやサイズは現段階では確定しない。

- bootloader、firmware image、環境変数、crash dumpとerase blockを共有しない。
- bootloader、Linux DT／MTD、OpenSBI、更新toolで同じpartition定義を使用する。
- firmware更新、factory reset、recovery toolがAK区画を消去する条件を明文化する。
- program／eraseは予約範囲内のみ許可し、全chip eraseをAK経路へ公開しない。
- NORのwrite protectionは実際のSoC／chipの仕様を確認する。
  Linuxのread-only partition指定だけを悪意あるLinuxからの保護にはしない。
- blobだけのためにfilesystemをSMへ持ち込まず、固定長上限のslot recordを使う。

### 5.5 NORへのアクセス方法

**初期実装候補はLinux MTDによる保存代行**とする。
Linuxは暗号化blobだけを運び、SMが暗号認証、世代とanchorの一致を判定する。
SMからLinuxへ同期callbackする仕組みを前提にせず、管理toolが
「SMで候補作成→MTD書込み→候補再投入→SMで確定」を段階的に呼ぶ。

この方式ではSM単独で物理書込み・durabilityを確認できない。
readbackの偽装や書込み抑止は可能であり、anchor更新後にblobが失われれば復旧不能／DoSとなる。
Linux代行でlocal rollback保護を提供するには、**Linuxから独立したanchorとSM側での照合**が必要である。

後段でSM内の直接NOR driverを検討する場合は、SPI controllerとNOR chipの両方に対応し、
clock/reset、pinmux、chip-select、busy polling、timeout、erase geometry、cache／DMAを実装する。
Linuxとの同時操作を通常のmutexだけで調停しない。controllerの専有または信頼できる仲介を設け、
Linux側driverのunbind／再初期化、multi-hart、割込み、suspend／resumeまで検証する。
直接アクセスにしても、外部からのNOR全体差戻しに対するrollback保護は追加されない。

### 5.6 初回provisioningと通常boot

初回provisioning：

1. operatorの認証と製品lifecycle、HUK healthを確認する。
2. 明示的に新規AKを生成し、公開identityと世代を確定する。
3. inactive slot用blobを生成し、書込み・完了待ち・readbackを行う。
4. SM内でreadbackのAEAD、公開鍵、世代、transaction、digestを再検証する。
5. protected profileではanchorを確定し、その後にAKを利用可能にする。
6. 外部検証者へ公開鍵／Name、世代、device binding、provider qualityを登録する。
   blob中の自己申告公開鍵だけを信頼起点にしない。

通常boot：

1. HUK／storage／anchorの能力を照会する。
2. 各slotを上限付きで検査する。
3. protected profileはanchorが指定する正確な世代・digestのblobだけを受理する。
4. AEAD検証後に秘密scalarと公開identityを検査する。
5. 現在のAKをSM保護RAMに置き、起動sessionで使用する。

初期化済みanchorがあるのにblobが無い場合は、新規AKを自動生成しない。
明示的な復旧または再provisioningを要求する。
anchorの無いprofileでは「未provision」と「攻撃による全消去」をローカルでは区別できない。

### 5.7 rotation、更新、電源断

protected profileではanchorに少なくとも
`device_binding, provisioning_epoch, AK_generation, blob_digest`を結合する。
anchor更新は期待旧値を指定したatomic compare-and-commit、または同等に検証された方式とする。
HUKでMACしたrecordを同じNORに置くだけでは独立anchorにならない。

更新順序：

1. 現active blobを残し、別erase blockのinactive slotへ新blobを書く。
2. 完了待ち・readback・SM内検証を済ませる。
3. anchorを新世代と新digestへatomic commitする。
4. 新AKをRAM上のactiveとして公開し、利用者へ結果を返す。
5. 外部登録・旧鍵失効を管理手順に従って完了し、必要な保持期間後に旧slotを回収する。

commit前のcandidateは通常bootで有効鍵にしない。
commit後に対応blobが読めない場合は古いslotへfallbackしない。
commit後・応答前の電源断では、再起動後に新AKのstatusを照会して結果を確定できるようにする。
transaction_idによる再試行は同じ操作の再実行として扱い、重複生成や二重世代更新を避ける。

外部検証者への登録はNOR／anchorと分散transactionになる。
新鍵を事前登録して切替後に旧鍵を失効する等、availabilityと旧鍵受理期間を明示する。
rotationのたびに外部登録済みと仮定しない。

### 5.8 RAM保持と消去

- AKとwrap keyの平文はSM専用RAMだけで扱い、Linux共有bufferへ置かない。
- provisioning／storage待ちの間にPCR lockや長時間spinlockを保持しない。
- 署名とrotationを同期し、処理中の鍵世代をsnapshotして混在を防ぐ。
- エラー、鍵切替、shutdown等で不要になった秘密bufferを明示的に消去する。
- stack、panic dump、debug log、compiler最適化、暗号library内部temporaryも確認する。
- secure processor内での非exportable署名方式なら、SMにはhandleと公開identityだけを保持する。

NOR eraseは、backupや外部imageに残ったAK blobまで消去しない。
同じHUKで旧blobを復号可能なままなら、物理eraseだけを「鍵の不可逆な失効」と説明しない。
旧世代の受理拒否、service側unwrap policy、外部の失効policyを組み合わせる。

## 6. ロールバック攻撃の検討

### 6.1 具体的な攻撃

| 攻撃 | AEADだけで防げるか | 対策／残る限界 |
| --- | --- | --- |
| ciphertext／header／公開鍵の改変 | 認証対象が完全なら検出可能 | parser検査、AAD、tag、復号後の公開鍵一致 |
| 他deviceのblobをコピー | 正規device固有HUKなら拒否を期待 | device bindingも認証。公開開発HUKや共有HUKでは成立しない |
| AK rotation前の正規blobを復元 | 防げない | 独立anchorで世代とdigestを照合 |
| slot A/BとNOR内管理recordを全部復元 | 防げない | A/Bは電源断対策。NOR外anchorが必要 |
| 同じ世代の別candidateへ置換 | tagが正規なら世代だけでは不十分 | anchorをblob digest／transactionへ結合 |
| 古い正規firmwareとblobを組で復元 | 防げない | boot anti-rollback、HUK service認可、format downgrade拒否 |
| blob／両slotを削除 | 防げない | 自動再生成拒否。初期化済みanchorで異常を検出。DoSは残る |
| Linuxが保存成功を偽装 | 防げない | trusted I/Oか独立anchor。欠落時は停止し、旧鍵へfallbackしない |

### 6.2 保証profile

- **development**：開発HUK／開発鍵。機能検証専用で、AKの機密性を主張しない。
- **wrapped-no-local-rollback**：正規HUK＋AEAD。保存中の機密性と改ざん検出を目指すが、
  過去の正規blobの復元をローカルで拒否できない。
- **wrapped-protected**：上記に独立したatomicな保護anchor、認可されたboot／unwrap経路を加える。
  実機で成立した条件と試験範囲を明記して提供する。

製品profileからdevelopmentへ、protectedからno-local-rollbackへ自動降格しない。
能力照会と外部登録にprofileを含め、要求を満たさなければAK利用を拒否する。

### 6.3 anchor候補と外部検証者

RPMBを使うtrusted service、secure processorの保護NVMなどを優先調査する。
RPMB内部write counterをAK generationとして直接流用せず、認証recordとatomic更新をproviderで管理する。

K3 eFuse ARCNはbit消費型かつ既存boot chainの用途がある。
AK rotationのたびに消費する実装は行わない。
低頻度anchorへの転用も、正式API、符号化、残量、所有権、電源断復旧が確認されるまで未採用とする。

外部検証者がdeviceごとの現行AK Name／generationと失効履歴を保存すれば、
登録済み旧AKの再使用を外部で拒否できる。ただし以下は残る。

- offline時のローカルなrollback防止にはならない。
- 検証者側stateのbackup差戻しや、device再登録のなりすましも防ぐ必要がある。
- challenge nonceは通信replay対策であり、古いAK状態の復元そのものを防がない。
- 同じAKを維持したまま別の保存状態だけ巻き戻す問題を、AK登録だけで解決しない。

## 7. 修正計画

実装前に最新HEADとAGENTS.mdを再確認する。以下のmodule名は責務案であり、既存providerへ統合する。
既存のEd25519 report／sealing、通常bootの既定設定を意図せず変更しない。

| 段階 | 修正内容 | 完了条件 |
| --- | --- | --- |
| P0 調査 | NOR chip／容量／erase単位、全partition、更新tool、Linux MTD経路、正規HUK API、認可とlifecycleを確認 | 固定資料／実機read-only結果を記録。AK partitionとHUKの可否が判断可能 |
| P1 契約 | blob形式、AAD、KDF、nonce、profile、status、世代／transactionと内部APIを固定 | security reviewを通し、上限・失敗時動作が仕様化 |
| P2 暗号 | 保守対象libraryのP-256／AEAD／KDFを最小構成で導入し、AK managerとHUK adapterを追加 | 非公開materialの境界、消去、公開鍵整合、開発fallback拒否を確認 |
| P3 保存 | 固定slot backend、管理用の限定SBI入口、Linux MTD管理toolを追加 | AK平文を転送せず、予約区画以外へ書けない。電源断時に旧確定版または新確定版へ復旧 |
| P4 rollback | 独立anchor adapterとcompare-and-commit、世代／digest照合を追加 | 全NOR差戻し拒否。anchor未提供時はprotectedをadvertiseしない |
| P5 lifecycle | provisioning、rotation、復旧、失効、firmware migrationを実装 | 運用手順と再試行規則が一致。通常署名から生成／import／eraseできない |
| P6 実機 | 正規HUKと予約NORで段階検証。必要なら直接NOR driverを別変更で追加 | 実機の保護・電源断・boot更新条件を確認してprofileを有効化 |

想定変更箇所：

- `keystone/sm/src/security/`：AK manager、HUK／storage／anchor adapter、署名provider。
- `keystone/sm/src/platform/spacemit_k3/`：K3のHUK／lifecycle／device binding接続。
- `opensbi/`：設定・link領域・限定SBI接続。直接NOR driver採用時だけdriverとcontroller保護を追加。
- Linux／管理tool：MTD保存代行、長さ検査済みblob転送、権限制御、transaction再開。
- `verifications/`：実Linux→OpenSBI→SM経路の専用検証。
- bootloader／partition設定：P0で所有元を確定してから修正。AK区画をupdate／recovery経路へ反映。
- `doc/`：本書、統合設計、storage調査、操作手順、実際の検証結果を同期する。

通常runtimeへ汎用NOR raw writeや任意HUK deriveを公開しない。
provisioningは通常Linux root権限だけで認可せず、lifecycleと署名済み管理要求、
物理presence等の具体方式をP1で選定する。

## 8. テスト計画

### 8.1 検証環境と判定

1. 暗号primitiveは公式known-answer vector、blob encodingは独立期待値で確認する。
2. 機能・異常系は実K3 QEMU guestのLinux→OpenSBI→SMを通して検証する。
   SBI／PMP／CSRをmockしない。production関数を抜き出した代替試験でPASS扱いしない。
3. QEMUでは開発providerと明示的なstorage fault制御を使用する。
   NORがmodel化されていなければfile-backed backendの検証に限定し、実NOR PASSとしない。
4. 実機はまずread-only調査、その後に確保済み専用試験区画で書込み・電源断試験を行う。
   eFuse burn、RPMB key programming、既存boot領域書換えを通常試験に含めない。

実行の基本は既存手順に従い、`make -C verifications guest`、
`make -C verifications kernel-modules`で準備し、
`bash verifications/run-all.sh`はsystem guest内だけで実行する。
AK用runner・test ID・期待profileは実装時に追加する。
build成功とQEMU PASS、実機PASSを別に記録する。

### 8.2 試験項目と期待結果

| ID | 試験 | 期待結果 |
| --- | --- | --- |
| AK-01 | 初回provision→保存→cold boot | 同じAK公開鍵／Name／世代で署名検証成功 |
| AK-02 | 別device／別HUKへblob移植 | 認証失敗し、AK利用不可 |
| AK-03 | header、AAD、nonce、ciphertext、tagを各1 bit改変 | 全て拒否。部分平文を返さない |
| AK-04 | 不正長、整数overflow、未知version、algorithm、予約field | 範囲外アクセスや過大確保なく拒否 |
| AK-05 | 正規AEADだがscalar不正／公開鍵不一致 | 鍵ロード拒否 |
| AK-06 | HUK unavailable、unfused／dummy provider、entropy故障 | 製品profileは拒否。開発鍵へfallbackしない |
| AK-07 | rotation後に旧blob、両slot、NOR全imageを復元 | protectedはrollback拒否。no-local-rollbackでは防止不能を実証・記録 |
| AK-08 | 同一世代でdigestの違う正規candidateを投入 | anchor指定以外を拒否 |
| AK-09 | anchor欠落／改変／更新失敗／新anchorと旧blob | 旧鍵へのfallbackや自動再生成を行わない |
| AK-10 | erase、page program、readback、anchor commit各境界で停止 | commit前は旧鍵、commit後は新鍵。確定blob欠落は明示error |
| AK-11 | commit後・成功応答前に電源断し同じtransactionを再送 | 確定状態を照会／再取得でき、二重rotationしない |
| AK-12 | Linuxの保存成功／readback偽装、未保存、転送欠落 | 偽装可能範囲を記録。秘密漏えい・旧鍵受理を起こさず、DoSを明示 |
| AK-13 | 署名とrotationを複数hartで並行 | 一つの処理の鍵世代が混在せず、deadlockしない |
| AK-14 | 許可外生成／import／erase、汎用derive／署名要求 | 認可・用途検査で拒否 |
| AK-15 | firmware更新／古い承認firmwareへの差戻し | 承認更新は既存AKを維持。禁止旧版はbootまたはunwrapで拒否 |
| AK-16 | 両slot消去、factory reset、鍵喪失 | 初期化済み状態なら異常検出。黙って新device identityにしない |
| AK-17 | log、共有buffer、panic経路、RAM消去の確認 | AK／wrap keyを外部へ出さない。stack／register監査の未確認範囲を記録 |
| AK-18 | partition境界、erase共有、timeout、bad read | boot領域を変更せず停止。失敗後も他SBI機能が動作 |
| AK-19 | 外部検証者の旧AK失効・世代記録 | 新AKを受理し旧AKを拒否。検証者state差戻しも別途確認 |
| AK-20 | nonce生成、再試行、暗号化回数上限 | 再暗号化でのnonce再使用を避け、entropy故障・上限超過は拒否 |

### 8.3 実機固有の確認

- NOR manufacturer／device ID、SFDP／仕様、容量、erase／program単位、耐久条件。
- partition境界と更新toolの実際のアクセス範囲。書込み前後のboot領域hashを比較する。
- 正規HUKを使うことを非秘密health／vendor仕様で確認し、別deviceで復号できないことを試験する。
- SM／Linux／bootloaderのcontroller所有権、suspend／resume、reset、timeout。
- 電源断の注入位置を記録し、実NORのpartial program／eraseを含めて復旧を確認する。
- secure boot、firmware anti-rollback、debug、DMA／IOPMPの実際の設定と保証範囲。
- anchor providerを採用できなければ、wrapped-protectedの実機試験は未実施／未対応として残す。

### 8.4 受入条件

- AK平文をLinux、NOR、logへ保存しない。
- 正規HUKがない製品profile、壊れたblob、誤ったdeviceのblobを拒否する。
- 電源断で未確定AKを有効化しない。確定後に旧AKへ戻らない。
- protected profileは全NOR差戻しを拒否し、世代とdigestのatomicな結合を実証する。
- local rollback保護が無い構成は能力照会・外部登録・結果に明示する。
- image容量、通常SBI、既存attestation／sealingへの回帰がない。
- 実行結果にcommit、環境、provider、試験項目、未確認条件を残し、秘密値は記録しない。

## 9. 未確定事項

1. 実機NORの全容量・geometryと、安全に予約可能な区画。
2. 正規HUK API、derive／unwrap権限、blank fuse時の挙動、firmware更新時のpolicy。
3. secure boot／anti-rollbackとdebug／DMA制限の実機設定。
4. NORの保存代行から開始するか、trusted I/O経路を先に用意するか。
5. 独立anchorの利用可否とatomic commitの仕様。未確定の間はlocal rollback保証なし。
6. provisioning認証、外部登録・失効、factory reset／board交換時の運用。
7. 暗号library、blob形式、nonce規則、migration対応範囲の固定。

## 10. 根拠資料

- [PCR・counter・Quote統合設計 第9節](keystone-tpm2-quote-design.md#9-jupiter2のakcounterとpersistent-storage管理)：AK保存方式、HUK wrap、providerとrollbackの検討。
- [Jupiter2 storage調査](jupiter2-persistent-storage-investigation.md)：実機NOR partition、eMMC／RPMBの未確認事項、eFuse ARCNの制約。
- [実機inventory](jupiter2-hardware-inventory.json)：観測環境とNOR OpenSBI partition。
- [OP-TEE調査](optee-milk-v-jupiter2.md)：開発用HUK設定とsecure bootの区別。
- [既存3002／3003実装](keystone-sbi-3002-3003-implementation.md)：共通providerの既存責務。P-256 AK実装済みの根拠にはしない。
- [K3 OpenSBI設定](../opensbi/platform/generic/configs/k3_defconfig)、
  [K3 platform](../opensbi/platform/generic/spacemit/)、
  [OpenSBI drivers](../opensbi/lib/utils/)：直接storage driver追加時の調査対象。

本書のSPI NOR保存経路、P-256 AK manager、正規HUK接続、独立anchorは、
設計時点では実装済み・実機検証済みと扱わない。
