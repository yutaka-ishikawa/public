# OpenSBI＋Keystone SMのTPM 2.0形式Quote設計

- 作成日：2026年11月10日（日本時間）
- 調査基準：`dfd3cf0e0`。**設計段階。Quote・AK管理は未実装、動作検証も未実施。**
- 対象：K3 QEMUとMilk-V Jupiter2／SpacemiT K3。
- 前提：[ソフトウェアPCR・IMA設計](../com/keystone-pcr-ima-design.md)。このPCR機能も設計段階。
- 今回の追加要件：`TPM2B_ATTEST`と`TPMT_SIGNATURE`によるQuote、Jupiter2のAK秘密鍵管理。

## 1. 採用方針と互換性の範囲

SMが保持するSHA-256 PCR 24本からQuoteを作り、独立したAttestation Key（AK）で署名する。
応答はTPM 2.0規定のwire形式でmarshalした`TPM2B_ATTEST`と`TPMT_SIGNATURE`とする。
署名は **ECDSA NIST P-256／SHA-256**、PCR bankもSHA-256に固定する初期profileを採用する。

| 対象 | 初期設計の対応 |
| --- | --- |
| `TPM2B_ATTEST`／内部`TPMS_ATTEST` | Quote用の正規フィールド・marshal・署名対象に対応 |
| `TPMT_SIGNATURE` | `TPM_ALG_ECDSA`、`TPMS_SIGNATURE_ECDSA`に対応 |
| 公開鍵とName | `TPM2B_PUBLIC`、Name、Qualified Nameを一貫して生成 |
| PCRとdigestの計算 | 24本・SHA-256 bank、選択PCRをTPM規則の順序で連結してhash |
| TPMデバイス・コマンド | `/dev/tpm0`、TPM2_Quoteコマンド全体、TCTI、session、NV、EKは実装しない |
| TPMのroot of trust | 形式互換だけでは物理TPM・TCG認証・TPM endorsementとは認めない |
| clock／再起動counter | 初期profileは揮発。TPMの永続clock semantics全体は満たさない |

従って呼称は「TPM 2.0形式のsoftware Quote」。物理TPMを完全にエミュレートしたという意味ではない。
標準構造を使うためmagic/typeも規定値にするが、その値はハードウェア由来の証明にならない。
検証者は、登録時にこのsoftware profileと鍵providerを区別して受け入れる。

既存SBI 3002の1352-byte Keystone reportとEd25519署名は維持する。
既存reportを`TPM2B_ATTEST`で包んだり、Ed25519をECDSAと表示したりしない。
PCR Quoteとenclave measurementの証明は別API。両方を要求する検証者は同一challenge/sessionへ結び付ける。

## 2. 処理経路

```text
検証者：challenge nonce＋PCR selection、登録済みAKと許可firmwareの情報
  ↓
Linux userspace Quote client
  ↓ 新規の限定ioctl
Linux組み込みKeystone PCR/Quote backend
  ↓ 新規host用SBI（数値FIDは実装時に割当て）
OpenSBI → SM Quote builder
  ├─ 保護PCR状態のatomic snapshot
  ├─ TPMS_ATTEST構築・SHA-256
  └─ AK provider：SM software署名 または secure service内署名
  ↓
TPM2B_ATTEST、TPMT_SIGNATURE、選択PCR snapshot、署名で結合された補足context
  ↓
検証者：構文・AK identity・nonce・context・署名・PCR digest・IMAログを照合
```

Linuxに任意digestのAK署名APIや、AK秘密鍵を返すAPIを設けない。
AK providerの内部`sign_digest`は、SMが自分で構築したQuoteにのみ使う。
PCR READをLinuxで繰り返してから署名依頼する方式では、PCR間の同時性を保証できないため採用しない。

## 3. TPM構造とwire形式

TCGの型名は仕様の型を指す。RV64 little-endianのC構造体をそのまま送信しない。
整数はすべてbig-endian。可変長要素は宣言されたsize分だけ送る。
`sizeof(TPMS_ATTEST)`、union全体、Cのpaddingはwire長ではない。

### 3.1 `TPM2B_ATTEST`

```text
UINT16 size                         # attestationDataだけの長さ
BYTE   attestationData[size]        # marshal(TPMS_ATTEST)
```

`TPMS_ATTEST`は以下の順序でmarshalする。

| フィールド | 型・値 |
| --- | --- |
| magic | UINT32、`TPM_GENERATED_VALUE = 0xff544347` |
| type | UINT16、`TPM_ST_ATTEST_QUOTE = 0x8018` |
| qualifiedSigner | `TPM2B_NAME`、このprofileのAK Qualified Name |
| extraData | `TPM2B_DATA`、初期profileは64 bytes。第4節のnonce＋context hash |
| clockInfo.clock | UINT64、SM起動後の経過ms |
| clockInfo.resetCount | UINT32、初期profileでは0。永続カウンタではない |
| clockInfo.restartCount | UINT32、初期profileでは0。永続カウンタではない |
| clockInfo.safe | 1 byte、初期profileではNO（0） |
| firmwareVersion | UINT64、管理されたfirmware version値。測定hashの代用品にしない |
| attested.quote.pcrSelect | `TPML_PCR_SELECTION`、下記の固定bank形式 |
| attested.quote.pcrDigest | `TPM2B_DIGEST`、size=32、下記のSHA-256結果 |

初期profileの`TPML_PCR_SELECTION`：

```text
UINT32 count = 1
UINT16 hash = TPM_ALG_SHA256 (0x000b)
UINT8  sizeofSelect = 3
BYTE   pcrSelect[3]
```

PCR nは`pcrSelect[n / 8]`のbit`n % 8`。例：PCR10のみは`00 04 00`。
空のselection、複数bank、重複bank、SHA-256以外、sizeofSelect≠3は初期profileで拒否する。
将来のbank追加はprofile versionを上げ、実装と検証者の両方を更新する。

```text
pcrDigest = SHA256(PCR[i0] || PCR[i1] || ... || PCR[ik])
  i0 < i1 < ... < ik、選択された各PCRは32 bytes
```

selection自体・PCR番号・長さprefixをpcrDigestのhash入力へ混ぜない。
複数bank対応時にはTPMLのbank順とbank内PCR昇順が関係するが、v1は1 bankのみ。
署名はselection自体も含む`TPMS_ATTEST`全体に掛かる。

### 3.2 署名対象と`TPMT_SIGNATURE`

```text
M = marshal(TPMS_ATTEST)
D = SHA256(M)
(R, S) = ECDSA_P256_Sign(AK_private, D)
```

署名対象は`TPM2B_ATTEST.attestationData`のsize bytes。
**外側のUINT16 sizeを含めず、SHA-256を二重に掛けない。**
暗号libraryの「messageを受け取るAPI」と「hashを受け取るAPI」を区別する。

`TPMT_SIGNATURE`のwire形式：

```text
UINT16 sigAlg = TPM_ALG_ECDSA (0x0018)
UINT16 signature.ecdsa.hash = TPM_ALG_SHA256 (0x000b)
UINT16 signatureR.size = 32
BYTE   signatureR.buffer[32]       # unsigned big-endian R
UINT16 signatureS.size = 32
BYTE   signatureS.buffer[32]       # unsigned big-endian S
```

出力は固定32-byte R/S、全体72 bytes。DER署名でも`R||S`だけでもない。
検証者の汎用parserはTPMが許す短いunsigned表現にも対応できるが、このprofileの生成物は32 bytes。
R/Sが0またはcurve order以上なら拒否する。ASN.1 DERが必要な検証library向けには
先頭ゼロと符号bitを正しく変換し、DERをTPMT_SIGNATUREと混同しない。
low-S正規化は必須条件にしない。ECDSA nonceは暗号libraryのRFC 6979経路を採用候補とし、
side-channel対策に必要な乱数も暗号品質を満たすproviderから供給する。

このprofileではQN=34、extraData=64なので、`TPMS_ATTEST`は177 bytes、
`TPM2B_ATTEST`は179 bytes。参考としてextraData=32なら145／147 bytesだが、v1の応答ではない。
一般parserはこれらの値を全TPM構造の固定長と決め付けない。

## 4. nonce、補足context、PCR snapshotの結合

challengeは検証者が生成する32-byte nonce。再利用せず、発行時刻と有効期限を検証者側で管理する。
Quote自身にrequest contextを結合するため、extraDataを次の64 bytesに固定する。

```text
extraData = nonce[32] || SHA256(marshal(keystone_quote_context_v1))[32]
```

補足contextの正規形式もbig-endian、paddingなし、固定順とする。

| 項目 | 長さ・内容 |
| --- | --- |
| domain | 16 bytes、ASCII `KEYSTONE-QUOTE-1`の16 bytes（NULなし） |
| context_version | UINT32、1 |
| profile_id | UINT32、software-PCR/ECDSA-P256/SHA256 profileの管理値 |
| provider_class | UINT32、unavailable／qemu-dev／SM wrapped-key／secure-serviceを区別 |
| boot_id | 32 bytes、SM起動epoch識別子 |
| pcr_update_counter | UINT64、全PCR extend成功回数。overflow時は更新・Quoteを失敗させる |
| flags | UINT32、SMのPCR異常latch等。未知bitは拒否 |
| ak_generation | UINT64、鍵rotationの世代。単なるnonce代わりにはしない |
| firmware_hash_alg | UINT16、初期profileはSHA-256 |
| firmware_hash | 32 bytes、定義したOpenSBI/SMのimmutable測定範囲のhash |

domainのbyte数、各enum値、測定するlinker sectionと長さprefixはABI headerと生成ツールで
一意に定義し、実装開始時に仕様へ固定値を追記する。任意のLinux申告hashを署名しない。
firmware measurementはmutable dataを除く測定範囲をcold initで計算し、公開manifestと照合する。
単なるhash計算はsecure bootや測定値の真正性を保証しない。第8節の鍵解放条件も必要。

boot_idは実機profileでは信頼できるentropyで生成し、再起動で変更する。counter backendを
identityやECDSA blinding用のentropyとして使わない。qemu-devの乱数は実機保証の対象外。
clockはOpenSBI timer値と検証済みfrequencyからmsへ変換し、逆行を検出したら異常扱いにする。
safe=0、永続reset/restart counterなしなので、検証者はfreshnessをnonceで判定し、
clockだけで再起動をまたぐrollback防止を主張しない。

SMは選択PCR、update counter、異常latch、boot_id等を同じsnapshotに収める。
PCR値を補足応答に返し、その値のdigestが署名済みpcrDigestと一致することを検証者が確認する。
これによりQuote後にPCR_READした値との競合を避ける。

IMAの測定失敗は既存PCR設計の異常latchと接続する。SMが観測した失敗とLinuxから申告された
失敗を区別し、一度立てたlatchはLinuxからclearできない設計とする。
ただし悪意あるkernelが測定・異常申告を省略することまでSMが検出できるとはしない。

## 5. AKの公開領域、Name、登録

AKはQuote専用P-256署名鍵。既存SMのEd25519秘密鍵・sealing keyをそのまま流用しない。
公開情報に`TPM2B_PUBLIC`を追加し、TPM型parserや外部検証libraryで読み取れるようにする。

初期`TPMT_PUBLIC`の案：

- type=`TPM_ALG_ECC (0x0023)`、nameAlg=`TPM_ALG_SHA256`。
- objectAttributes=`signEncrypt | userWithAuth`（0x00040040）、authPolicyはempty。
- fixedTPM、fixedParent、restricted、sensitiveDataOriginは立てない。未実装のTPM属性を偽装しない。
- symmetric=`TPM_ALG_NULL`、scheme=`ECDSA/SHA256`、curveID=`TPM_ECC_NIST_P256 (0x0003)`、kdf=`TPM_ALG_NULL`。
- uniqueはP-256公開点X/Y。各TPM2B_ECC_PARAMETERを32 bytesのbig-endianで出す。

この定義のpublicAreaは88 bytes、外側のTPM2B_PUBLICは90 bytes。
userWithAuthはここでTPM session実装を提供する意味ではなく、software serviceの利用権限は
Linux ioctl／SM／secure providerのアクセス制御で規定する。generic TPM object作成APIへ
このpublicAreaだけを渡して同一鍵を作れるとは保証しない。

```text
Name(AK) = BE16(TPM_ALG_SHA256) || SHA256(marshal(TPMT_PUBLIC))
```

v1ではsoftware owner hierarchy配下のprimaryという名前空間を明示的に定義する。
owner hierarchyのQNはBE32(`TPM_RH_OWNER = 0x40000001`)とし、

```text
QualifiedName(AK) = BE16(TPM_ALG_SHA256) || SHA256(BE32(TPM_RH_OWNER) || Name(AK))
```

をqualifiedSignerに格納する。NameとQualified Nameは34 bytes。
単にNameをqualifiedSignerへコピーしない。親鍵階層を変更した場合はQNも計算し直す。
これはsoftware hierarchyの命名規約であり、実在するTPM owner hierarchy・EK証明書を意味しない。

検証者はAK公開鍵、publicArea/Name/QN、provider class、許可firmware、機体識別と鍵世代を
初期登録で結び付ける。Quoteに同梱された公開鍵を無条件で信用しない。
初期段階は管理者が認証した経路で登録するpin方式。製品ではprovisioning CAによる証明書等を
別途設計し、発行元・用途・有効期限・失効を検証する。物理TPMのEK証明書とは区別する。

## 6. SBIとLinux API

既存host用Keystone EID `0x08424b45`に以下のsymbolic機能を追加する。
PCR設計のFIDも未確定のため、2001〜2006、3002、4000、検証専用4900を流用しない。
数値はPCR/Quoteを一緒に実装する際に重複検索して確定する。

| 機能 | 目的 |
| --- | --- |
| QUOTE_GET_CAPS | ABI/profile、アルゴリズム、最大長、provider準備状態 |
| QUOTE_GET_AK_PUBLIC | 公開鍵、Name/QN、登録用公開metadataを返す。秘密鍵は返さない |
| PCR_QUOTE | nonceとselectionからSM自身がQuoteを生成 |

PCR_QUOTEのRV64 ABI案：a0=requestのS-mode VA、a1=request bytes、
a2=responseのS-mode VA、a3=response capacity、a4/a5=0。
既存SMと同じ正のKeystone statusをa0、成功時の実response長をa1に返す。
Linux層でerrnoへ変換する。未対応・引数不正・copy失敗・provider不調・BUSYを区別する。

requestはversion、bytes、nonce_len=32、nonce、1 bankのselection、予約値を持つ。
C ABIは固定幅整数と明示padding、長さ上限を定義する。wire TPM整数とは別のABI。
応答は固定header＋長さで区切る以下のblobとし、各TPM blob内は第3節のwire形式とする。

1. marshal(TPM2B_ATTEST)
2. marshal(TPMT_SIGNATURE)
3. 正規補足context
4. selectionに対応するPCR snapshot（昇順）

外側headerにはversion、total bytes、各blob長を持たせ、合計・overflow・上限を検査する。
初期最大PCR snapshotは24×32=768 bytes。request上限256、response上限2048 bytesを設計上限とし、
最終ABI定義後に最大値を静的検査する。小さい出力capacityは署名開始前に拒否する。
必要長照会はGET_CAPSから算出可能にし、PCRを別時点で取得して結果を組み立てない。

通常Linux S-modeのみ許可し、enclaveからは拒否。Linux側は専用の限定ioctlを設け、
初期運用では管理者だけが利用する。ユーザーVAを直接SMへ渡さず、kernel bufferを使う。
同じkernel内の悪意ある呼び出し元を、SBI番号だけでIMA daemonに限定できるとはしない。

## 7. 排他、copy、エラー、library

処理順：requestを一度SMへcopy → 構文検査 → bounded Quote workspaceを予約 →
PCR lockでsnapshot → lock解除 → context/TPM marshal → hash/署名 → 応答全体をSM内で完成 → copy out。
callerのrequestをcopy後に再読しない。出力は入力と重なってもSM内copy済みの入力を使う。

PCR lockを署名、乱数待ち、secure RPC、S-mode copy中に保持しない。
既存3002/3003のsecurity_lock/workspaceとは分離し、競合で既存reportを上書きしない。
初期実装は固定数workspaceとtry予約、枯渇時BUSY。グローバルspinlockのまま
OP-TEE/processor応答を待たない。RPCのdeadlock、timeout、再送をprovider契約に含める。

copyには既存OpenSBIのtrap-aware load/store helperを利用し、S-mode VA／ページ境界／PMPを検証する。
既存3002のEPM PA専用copyを無条件に転用しない。
copy out失敗では一部の出力が書かれ得るため、callerは失敗応答を使用しない。
QuoteはPCRを更新しないのでcopy失敗・署名失敗でPCR値を変えず、workspaceと秘密の中間値を消去する。

ソース調査ではOpenSBIにP-256/ECDSA/SHA-256の再利用可能な実装を確認できなかった。
既存SMはEd25519、SHA3、HKDF-SHA3系であり、TPMのSHA-256/ECDSAとして流用できない。
SHA-256はPCRと共通実装を一つ導入する。P-256/ECDSA、HMAC-SHA256/RFC6979には
保守されたC library（第一候補Mbed TLSの保守対象release）を最小構成で使う。
実装前にrelease/commit、ライセンス、advisory、定数時間性、RV64 ABI、heap/stack上限を固定する。
独自ECC演算を新規作成しない。providerが利用できない実機では未対応を返し、
qemu-devやtimestamp/counter鍵へ自動fallbackしない。

## 8. Jupiter2のAK秘密鍵をどこで管理するか

### 8.1 現時点で分かっていること

[既存ハードウェア調査](jupiter2-hardware-compatibility.md)と
[WTM/OP-TEE調査](jupiter2-security-engine-rng.md)で、K3向けWTM RNGのベンダーdriver、
OP-TEE platform、TE200等の関連定義を確認している。
一方、**P-256の非exportable key slot、device-unique HUK API、secure storage、RPMB鍵、
AK signing RPCがJupiter2実機で利用可能であることは確認していない。**

WTM RNG、暗号エンジン、独立セキュアCPU、RT24管理コア、AP上のOP-TEE trusted domainを
同一物として扱わない。RNGがあることは、秘密鍵を永続保存できることの根拠にならない。
OpenSBI MPXYにはOP-TEE dispatcherがあるが、それだけでAK serviceが完成するわけではない。

### 8.2 管理方式の比較

| 方式 | 平常時の秘密鍵 | 永続保存 | 評価 |
| --- | --- | --- | --- |
| QEMU開発seed | SM RAM。開発firmware/seedから取得可能 | hostの開発用seed | 実装検証用。実機の機密性・identityの証明に使わない |
| SM揮発鍵 | PMPで保護したSM RAM | なし | rebootでAKが変わる。試験・session限定で、固定AK登録には不適 |
| HUKでwrapしたAK | 署名時にSM RAMへ復号 | eMMC等にはAEAD暗号化blobだけ | 実機の現実的候補。HUKの非export性、secure boot、rollback保護を要確認 |
| HUKからAKを導出 | SM RAMで導出、または安全なderive service内 | HUKと導出parameter | blob不要だがrotationとfirmware更新時identityの扱いが必要。P-256 scalarの正しい導出をlibraryと規定する |
| secure processor内の非exportable AK | processor/key engine内、SMへ秘密鍵を渡さない | 保護slot／secure NVM | 長期の第一候補。ただし実際のAPI・P-256対応・呼出し制御が未確認 |
| OP-TEE trusted service | OP-TEE内、またはそこから非exportable engineを利用 | HUK-backed secure storage、可能なら認証・rollback保護付きRPMB | 製品でOP-TEEを併用する場合の候補。trusted domain/PMP共存とstorage backendの実証が必要 |

平文のAKをrootfs、DT、9p共有、boot引数、Linux keyringだけに保管する方式は採用しない。
SM RAMのPMP保護もDMA、debug port、悪意あるM-mode firmware、電源断後の残留等まで
自動的に防ぐとは言えない。実機のDMA/IOPMP、debug lifecycle、secure bootを別に評価する。

### 8.3 推奨する段階とprovider境界

1. **QEMU**：独立したqemu-dev AKを使い、wire互換・検証者・異常系を先に確認する。
2. **Jupiter2調査**：非exportable P-256署名serviceの存在と利用権限を最初に確認する。
3. 利用可能なら**secure service内保管・署名**を採用。SMはkey handleと公開情報のみ保持する。
4. 不可能でもHUK/secure storageが使えるなら**HUK wrap＋SM RAM署名**を実機候補とする。
5. どちらも実証できなければ、実機の永続trusted AKは未対応と明示する。
   揮発鍵を製品用の永続identityに見せない。

内部providerは`get_public_identity`、`sign_quote_digest`、`get_health`を持ち、
公開identityはTPMT_PUBLIC/Name/QNと一貫させる。key handleはLinux指定の任意keyへ切り替えない。
各providerは署名結果を対応する公開鍵で内部検査し、失敗時に成功応答しない。
AK生成・import・rotationはprovisioning専用経路とし、通常PCR_QUOTEから呼べないようにする。

secure serviceが「渡されたdigestを何でも署名する」だけで、Linuxにも同じAKで呼び出せるなら、
偽PCRのQuoteを作れてしまう。keyを非exportableにしただけでは解決しない。
SM専用の認証された呼出経路、mailbox/MMIO所有権、用途制限を設ける。
processor側がPCRを所有しない構成では、processorもSMのQuote構築・snapshotを信頼する。
これをSMが不要な独立attesterと説明しない。

### 8.4 HUK wrap方式の具体案

- 初期provisioningで信頼できるentropyからP-256 AKを生成する。
- HUKをLinuxへ公開せず、hardware derive serviceでQuote用wrap keyを導出する。
- AKはAEADで暗号化し、device identity、profile、public Name、key generation、blob versionを
  associated dataへ結び付けてeMMC等に保存する。別機体のblobを復号できないことを確認する。
- boot時は承認済みSMだけがunwrapし、PMP保護RAMで保持または署名時だけ展開する。
  stack、log、panic dump、通常Linuxメモリへ平文を残さない。
- 暗号化blobの改変検出とrollback防止は別。古いblob/鍵世代の復元は、実証されたRPMBや
  secure monotonic state、検証者のgeneration/失効policyで制限する。
- secure bootとanti-rollbackが無ければ、攻撃者が改変M-mode firmwareを起動して
  HUK serviceを利用できる可能性が残る。HUKがあるだけでtrusted Quoteとはしない。

HUKから直接AK scalarを導出する場合、単純なdigestのmod nだけで済ませず、
標準的な鍵導出・range検査・domain separationを採用する。wrap方式との混在・暗黙切替をしない。
既存Keystone Ed25519/sealingから独立した用途labelと世代を使う。

### 8.5 実機で確認したい情報

- SoC security manualとベンダーAPI：key slot、P-256/ECDSA、HUK/KDF、署名、zeroize、権限設定。
- BootROM→firmwareの認証、debug無効化、anti-rollbackの実際の設定と検証済みimage。
- security processor/TE200/WTM/RT24/OP-TEEの正確な担当、firmware version、mailbox ABI。
- Linuxがkey engineや署名serviceへ直接アクセスできないことを示すfirewall/PMP/IOPMP設定。
- OP-TEEのHUK取得とsecure storage backend、eMMC RPMBのprovisioning・認証・書換耐性。
- key生成後のreboot、電源断、firmware更新、鍵rotation、失効、他boardへのblobコピーの結果。
- 暗号entropyのhealthと失敗時挙動、署名throughput、最大応答時間、timeout後のkey状態。

機密鍵そのものの提供は不要。API仕様・公開設定・公開鍵・試験結果で確信度を上げる。

## 9. 検証者の手順

1. 応答長と各blobの境界を検査し、TPM型をunmarshal。trailing bytes、size違反、未知profileを拒否。
2. 登録済みAK公開情報とName/QN、署名方式、provider classを照合する。未登録公開鍵を信用しない。
3. magic、type、SHA-256 bank、要求と完全一致するselectionを確認する。
4. extraDataのnonceが未使用のchallengeと一致し、context hashが補足contextから再計算した値と一致することを確認。
5. TPM2B外側sizeを除くattestationDataのSHA-256についてECDSA署名を検査する。
6. 返されたPCR snapshotを規定順にhashし、署名済みpcrDigestと比較する。
7. firmware measurement、鍵世代、異常flags、software clockの制限をpolicyで判定する。
8. IMA binary logのprefixをPCRごとにreplayし、Quote時点のsnapshotと一致するprefixを探す。
   Quoteとlog取得は同時ではないため、後続entryの存在だけで偽造と判定しない。
9. nonceを消費済みにする。署名が正しいだけでPCR内容を許可することにはしない。

`tpm2_checkquote`等との相互運用は、要求するmessage fileがTPMS_ATTESTそのものか、
外側size付きTPM2Bか、PCR fileがどの形式かを対象tool versionで確認する。
公式tpm2-tss MUによるunmarshalとOpenSSL等での独立署名検証を基本oracleとする。
TCTIやTPM handleを要するコマンド全体がそのまま動くとはしない。

## 10. 実装順序と検証計画

| 段階 | 作業・完了条件 |
| --- | --- |
| Q0 | PCR設計と合わせてFID、context/response ABI、enum、測定範囲、library versionを固定 |
| Q1 | PCR24本/SHA-256を実装し、atomic snapshot、update counter、異常latchを検証 |
| Q2 | C marshal/unmarshal、AK public/Name/QN、Quote builder、qemu-dev ECDSA provider |
| Q3 | Linux ioctlとC client、SBI受付、外部検証者、独立parser/署名検証 |
| Q4 | 実QEMUで正常・異常・競合・障害注入と既存3002/3003等の回帰 |
| Q5 | Jupiter2のAK provider選定、provisioning/保管/rotation、実機の境界・電源断・rollback検証 |

想定配置：`sm/src/pcr/`、`sm/src/quote/`、`sdk/include/shared/quote_abi.h`、
Linux組み込みbackend、C client/verificationを`verifications/quote/`へ置く。
既存[異常系計画](keystone-abnormal-concurrency-verification-plan.md)にも機能対を追加する。
この文書で既存の未了試験や製品不具合を完了扱いにはしない。

必須試験：

- 全PCR／単一PCR／PCR0/23／複数PCR、独立SHA-256値、marshal長とbyte order。
- tpm2-tss MUでTPM2B_ATTEST、内部TPMS_ATTEST、TPMT_SIGNATURE、TPM2B_PUBLICを読む。
- challenge32＋context hash32、replay、誤AK、誤QN、誤selection、report/context/PCR改変を拒否。
- ECDSA R/S、DER変換、曲線点検査、サイズ境界、未知algorithm、二重hash・外側size混入の検出。
- PCR_EXTEND対QUOTE：単一時点のsnapshotに線形化され、PCR値とcounter/contextが一致。
- 同時QUOTE、3002/3003対QUOTE、RNG不調、workspace枯渇、secure RPC timeout、copy失敗。
- Quote失敗でPCR不変、部分出力は成功扱いにしない、keyや秘密の中間値をログへ出さない。
- Sstc on/off・複数hartの実Linux→OpenSBI→SM経路。模擬SM/PMPを合格根拠にしない。
- 実機の鍵非export性、Linuxからの署名権限、別機体blob、旧鍵rollback、更新/rotation。

現時点の結果：**設計レビューのみ。上記のQuote実装・試験のPASS記録は存在しない。**

## 11. 参照

- [TCG TPM 2.0 Library Specification](https://trustedcomputinggroup.org/resource/tpm-library-specification/)：Part 2 Structures（TPMS_ATTEST、TPMS_QUOTE_INFO、TPMT_SIGNATURE、Name）、Part 3 Commands（TPM2_Quote）。実装時に採用revisionを固定する。
- [tpm2-tss型定義](https://github.com/tpm2-software/tpm2-tss/blob/master/include/tss2/tss2_tpm2_types.h)：2026-11-10取得。規範はTCG仕様とし、検証時はtpm2-tssのcommitも固定する。
- [tpm2_checkquote](https://tpm2-tools.readthedocs.io/en/latest/man/tpm2_checkquote.1/)：file形式とalgorithm対応を採用versionで確認する。
- [既存PCR/IMA設計](../com/keystone-pcr-ima-design.md)、[既存crypto ABI](../keystone/sdk/include/shared/crypto_abi.h)。
- [Jupiter2 WTM/OP-TEE調査](jupiter2-security-engine-rng.md)、[SBI 3003 provider設計](keystone-sbi-3003-plan.md)。
- [K3 OP-TEE platform](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/platform_config.h)、[OpenSBI MPXY dispatcher](../opensbi/lib/utils/mpxy/fdt_mpxy_opteed.c)。
