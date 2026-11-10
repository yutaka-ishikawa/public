# OpenSBI＋Keystone SMのPCR・counter・Linux IMA・TPM 2.0形式Quote統合設計

- Quote設計作成日・PCR設計統合日・counter設計追加日：2026年11月10日（日本時間）
- 統合元PCR設計：2026年11月9日作成、調査基準`ba27803c8`、Linux 6.18.3。
- 調査基準：`dfd3cf0e0`。**設計段階。PCR・counter・IMA連携・Quote・AK管理はいずれも未実装、動作検証も未実施。**
- 対象：K3 QEMUとMilk-V Jupiter2／SpacemiT K3。
- 正本：本書。従来の`com/keystone-pcr-ima-design.md`の設計内容は第2節へ統合した。
- 対象範囲：PCR保持・EXTEND/READ、Linux IMA、`TPM2B_ATTEST`／`TPMT_SIGNATURE`によるQuote、Jupiter2のAK秘密鍵・counterの永続管理。

本書ではPCRの更新・IMAログから、同一snapshotのQuote生成、AK管理、外部検証までを扱う。
第2節がPCR・IMAの詳細、第3〜8節がQuoteの経路・構造・ABI、第9節がJupiter2のAK・counter・persistent storage管理、
第10〜12節が検証者・実装計画・参照である。

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
| TPMデバイス・コマンド | `/dev/tpm0`、TPM2_Quoteコマンド全体、TCTI、session、汎用NV領域、EKは実装しない。限定NV counter APIは第9.8節で設計 |
| TPMのroot of trust | 形式互換だけでは物理TPM・TCG認証・TPM endorsementとは認めない |
| clock／reset/restart counter | 揮発開発profileとprotected-persistent目標profileを分離。第9.6〜9.12節 |
| NV counter | 64-bit READ/INCREMENTと永続化providerを設計。full TPM NV command全体は対象外 |

従って呼称は「TPM 2.0形式のsoftware Quote」。物理TPMを完全にエミュレートしたという意味ではない。
標準構造を使うためmagic/typeも規定値にするが、その値はハードウェア由来の証明にならない。
検証者は、登録時にこのsoftware profileと鍵providerを区別して受け入れる。

既存SBI 3002の1352-byte Keystone reportとEd25519署名は維持する。
既存reportを`TPM2B_ATTEST`で包んだり、Ed25519をECDSAと表示したりしない。
PCR Quoteとenclave measurementの証明は別API。両方を要求する検証者は同一challenge/sessionへ結び付ける。

<a id="pcr-design"></a>

## 2. ソフトウェアPCR・Linux IMA連携

PCR（Platform Configuration Register）は、SMの保護メモリに持つソフトウェアの測定値bankを指す。
RISC-Vの物理CSRや外付けTPMのレジスタを追加する設計ではない。
以下は先行PCR設計の目的・要件・ABI・IMA連携・受け入れ条件を統合し、Quoteとの接続条件を補ったもの。

### 2.1. 目的

OpenSBIに統合されたKeystone Security Monitor（SM）内に、SHA-256によるソフトウェアPCRを24本実装する。Linux IMAの測定ダイジェストを独自SBIで渡し、以下の計算で蓄積する。

```text
PCR[n] = SHA-256(PCR[n] || digest)
```

TPM2のPCR extendと同じ計算を提供するが、TPM2デバイスおよびTPM2コマンド全体は実装しない。

実装担当Codexは、最新HEADとAGENTS.mdを再確認し、既存ABIや進行中の変更との競合を確認すること。以下のABI案は実装レビュー後に固定する。

### 2.2. 確定要件

| 項目 | 仕様 |
|---|---|
| PCR本数 | 24本 |
| PCR番号 | 0〜23 |
| ハッシュ | SHA-256のみ |
| PCRサイズ | 各32バイト、合計768バイト |
| 初期値 | 全PCRをゼロ化 |
| 更新 | 指定PCRへのextendのみ |
| 読み出し | 指定PCRの現在値 |
| 能力照会 | PCR数を含む情報を返す |
| 永続化 | 行わない |
| Linuxからのリセット | 提供しない |
| IMA既定測定先 | PCR 10 |
| IMAポリシーによる指定 | PCR 0〜23を利用可能 |

PCR・IMA単体の対象外は、TPM2コマンド全体、NV領域、locality、認証セッション、ブートローダー等の自動測定、kexec・サスペンド。
Quote・署名付きPCR証明・AK管理は、本書の第3節以降で追加する。


全PCRを同一属性とする。物理TPMの同番号PCRと、用途・アクセス権・リセット条件まで互換であることは保証しない。

### 2.3. 全体構成

```text
Linuxカーネル組み込み
  IMA：ポリシー評価、測定、テンプレート生成、測定ログ
    ↓ テンプレートのSHA-256ハッシュ＋PCR番号
  Keystone PCR連携
    ↓ SBI ecall
OpenSBI：Keystone SBI受付
    ↓
Keystone SM：PCR_GET_CAPS／PCR_EXTEND／PCR_READ、保護PCR状態

既存Keystoneドライバ：enclave作成・実行等。モジュール利用可。
```

Linux IMAはカーネル組み込み機能であり、ロード可能なモジュールではない。本方式ではIMA本体を変更してカーネルを再ビルドする。既存Keystoneモジュールだけの変更では成立しない。

PCR連携部分はIMA初期化前に利用できるよう組み込みとし、enclaveドライバのロード・アンロードに依存させない。PCR処理にenclaveの作成は不要。

### 2.4. 変更対象

| 対象 | パス・変更内容 |
|---|---|
| SBI受付 | `keystone/sm/src/sm-sbi-opensbi.c` |
| 共通定義 | `keystone/sdk/include/shared/sm_call.h`。他の複製定義も検索する |
| SM初期化 | `keystone/sm/src/sm.c` |
| SMビルド | `keystone/sm/src/objects.mk`と関連スクリプト |
| PCR処理 | SM内に新規PCR状態・SHA-256・ロックを追加 |
| IMA初期化 | `linux-k3/security/integrity/ima/ima_init.c` |
| IMA extend | 同ディレクトリの`ima_queue.c` |
| ハッシュ | 同ディレクトリの`ima_crypto.c`等 |
| Linux設定 | IMAのKconfig／Makefile、組み込みPCR連携コード |
| ビルド | `scripts/build-linux-k3.sh`、必要に応じ統合ビルド |
| 検証 | `verifications/`へ実ゲスト試験を追加 |

### 2.5. SBI ABI案

既存Keystone SBI拡張のホスト用FID範囲に追加する。数値FIDは実装開始時に全定義を検索して未使用番号を割り当てる。3001〜3003等の既存番号は流用しない。

以下はRV64用。戻り値は既存Keystoneの規約に合わせる。既存受付は正のKeystoneステータスを扱うため、標準SBIの負のエラーと混在させない。Linux側でerrnoへ変換する。

#### 2.5.1 PCR_GET_CAPS

入力：`a0〜a5 = 0`。

出力：`a0 = ステータス`、`a1 = 能力情報`。

| a1のビット | 内容 |
|---|---|
| 0〜15 | ABIバージョン：1 |
| 16〜31 | PCR数：24 |
| 32〜47 | アルゴリズムID：SHA-256、0x000b |
| 48〜63 | ダイジェスト長：32 |

アルゴリズムIDは識別用にTPMの値を用いるものであり、TPM2コマンド対応を意味しない。

#### 2.5.2 PCR_EXTEND

```text
a0 = PCR番号（0〜23）
a1 = digest[0..7]
a2 = digest[8..15]
a3 = digest[16..23]
a4 = digest[24..31]
a5 = 0
```

各8バイトはリトルエンディアン整数として格納する。例えばa1のbits 7:0がdigest[0]、bits 63:56がdigest[7]。SMも同じ規則で32バイト列を復元する。数値文字列や16進文字列をハッシュしない。

出力：`a0 = ステータス`、`a1 = 0`。

成功時、指定PCRのみを1回更新する。不正番号、予約引数違反、権限違反では更新しない。

#### 2.5.3 PCR_READ

```text
a0 = PCR番号（0〜23）
a1 = Linux側出力バッファのS-mode仮想アドレス
a2 = 出力長（32固定）
a3〜a5 = 0
```

出力：`a0 = ステータス`、`a1 = 0`。成功時、バッファに32バイトを格納。

アドレスは物理アドレスではなく、呼び出し元S-modeの仮想アドレス。既存SMコピー機構のホストS-mode対応、例外処理、PMP制約を確認して使用する。受け取ったポインターをM-modeから無検証で直接参照しない。

ロック下でSM内一時バッファへスナップショットを取得し、解放後に呼び出し元へコピーする。コピー失敗でもPCRは不変。出力先が部分的に書かれる可能性があるため、呼び出し側は成功時だけ内容を使用する。

#### 2.5.4 権限・検証

- 通常のLinux S-modeから受け付ける。
- Keystone enclaveコンテキストからは拒否する。
- SM未初期化時は拒否する。
- PCR範囲等をLinuxとSMの両方で検証する。
- 通常のLinuxカーネル内の呼び出し元をIMAだけに限定するものではない。

### 2.6. SM内部設計

PCR状態をSM保護メモリに配置する。初期実装では全PCRで単一スピンロックを共有する。

extend手順：権限・引数確認 → レジスタから入力復元 → ロック → update counterのoverflow検査 → 旧PCRと入力の64バイトをSHA-256 → 指定PCRへ反映しcounterを1増加 → ロック解除。

Quote拡張では、PCR値、全PCR共通の64-bit update counter、SM異常latchを同じPCR lockで保護する。
失敗したextendは値もcounterも更新しない。counterが最大ならPCRを変更せず失敗を返す。
QUOTEは同じlockで選択PCRとcounter/latchを一度にcopyし、署名前にlockを解放する。
boot_idはcold init時に設定し、そのepoch内では不変とする。contextへの結合は第5節を参照。

ロック中にS-modeメモリへアクセスせず、他hartの完了を待つIPIも発行しない。readとextendの競合でも完全な32バイト値を取得する。

既存SMの`hash_*`はSHA3系なので流用しない。PCR用SHA-256を分離し、enclave測定、attestation、sealingの方式を変更しない。導入するSHA-256のライセンス、出典、既知値との一致を確認する。

PCR配置とPMP設定を調べ、Linuxから直接書き換えできないことを確認する。DMAを含む実機の保護範囲は別途検証する。

### 2.7. Linux IMA連携

#### 2.7.1 バックエンド選択

カーネル設定でSBI PCRバックエンドを利用可能にし、起動時に明示選択する。選択後は固定し、TPMとの二重extendや途中切り替えはしない。Kconfig名と起動パラメーター名は実装時に確定する。

IMA初期化時にGET_CAPSを呼び、ABI、SHA-256、32バイト、PCR数24を確認する。SBI方式を指定したのに使えない場合、黙ってTPM-bypassへ移行しない。初期化失敗と異常状態を明示する。

#### 2.7.2 測定ダイジェストと順序

PCRへ渡すのはファイルハッシュそのものではなく、IMA測定テンプレートのSHA-256ハッシュとする。初期構成はSHA-256／ima-ng、既定PCR 10。

既存テンプレートハッシュ処理を利用する。TPM不在時にも必要なSHA-256スロットが生成されるよう初期化を確認・調整する。

`ima_queue.c`の`ima_add_template_entry()`→`ima_pcr_extend()`→`tpm_pcr_extend()`の経路にSBIバックエンド分岐を追加する。TPM不在時の省略処理も変更対象。

既存`ima_extend_list_mutex`で、重複判定、ログへの追加、extendの順序を維持する。ファイルアクセスごとの無条件extendにはしない。PCR番号は測定エントリーから取り、10に固定しない。

0〜23以外を指定するポリシーは可能な限り読み込み時に拒否し、送信時も検証する。

#### 2.7.3 Violation

violation時は対象PCRへ32バイトの0xffをextendする。TPM不在時に省略されるviolation用ダイジェスト初期化も変更する。ログ再生ツールも特殊処理に対応させる。

#### 2.7.4 エラー処理

SBI失敗時はPCR番号と原因を監査ログに記録し、測定連続性が失われたことを示す、再起動まで保持するLinux側の異常状態を設定する。
SMが観測した更新失敗はSMのlatchへ記録する。Linuxだけで発生した測定/送信失敗をQuoteのcontextへ結合するには、
LinuxからSMへ異常をsetするhost用操作（仮称PCR_MARK_FAILED）を追加する。clear操作は設けず、SM自身の異常と申告由来の異常を区別する。
この操作のFID・引数・flags値・能力照会はABI確定時に定義する。申告しない悪意あるkernelの検出は保証しない。成功として表示せず、別バックエンドへ自動移行しない。結果不明な要求を無条件再送しない。

既存IMAでextend失敗がどのように伝播するか確認する。監査記録とファイル実行拒否は別要件であり、初期実装に実行拒否・panicを含めない。IMA初期化失敗でLinux起動自体が停止すると説明してはならない。

#### 2.7.5 PCR_READ

カーネル内APIを設け、検証モジュールから利用する。PCR READ単体の恒久的なユーザー空間向けデバイスAPIは必須にしない。Quote利用者向けの限定ioctlは第7節で追加する。検証用の操作口を設ける場合は検証専用と明示する。

### 2.8. Boot aggregateとライフサイクル

PCR 0〜7は用意するが、ブートローダー等の測定は追加しない。初期実装ではTPM不在時の既存IMA boot aggregate表現を維持する方針とし、ソフトウェアPCRの存在だけを理由にハードウェア由来の測定済み起動として扱わない。boot aggregateエントリー自体は通常どおり指定PCRへextendする。

全PCRゼロ化とupdate counter=0、異常latchの初期化はファームウェアcold-boot初期化で一度だけ行う。secondary hart初期化、hart停止・再開、enclave作成・破棄ではゼロ化しない。LinuxからのリセットSBIは設けない。

ファームウェア初期化を通る新規起動ではゼロから開始する。warm resetの経路はQEMUと実機で確認し、維持・初期化を推測で保証しない。kexecとサスペンドは初期対象外で、検証構成では無効化する。

### 2.9. ビルド統合

LinuxではPCR連携を組み込み、IMA、securityfs、SHA-256を有効にする。初期構成をSHA-256／ima-ngとする。

`scripts/build-linux-k3.sh`はdefconfigを再適用するため、手動.config変更だけで済ませず、設定の有効化・反映確認をスクリプトへ追加する。

SMでは新規ソースを登録し、既存の外部SMオブジェクトとOpenSBIリンク経路を維持する。PCR機能を無効にした場合の既存機能も確認する。

カーネル更新後はKeystoneドライバと検証モジュールを同じカーネル出力で再ビルドする。

### 2.10. 実装順序

1. 最新HEAD、AGENTS.md、既存FIDを確認する。
2. ABIと戻り値を確定する。
3. SMのSHA-256、PCR24本、初期化、排他制御を実装する。
4. GET_CAPS／EXTEND／READをSBIへ接続する。
5. Linux組み込みPCR連携を追加する。
6. IMA初期化、ダイジェスト、extend、violationを変更する。
7. バックエンド選択、エラー記録、異常状態取得を実装する。
8. ビルド設定と手順書を更新する。
9. K3 QEMU実ゲストで専用・回帰試験を行う。
10. 証跡と未検証範囲を記録する。
11. Jupiter2実機の起動経路、保護設定、PCR動作を確認する。

### 2.11. 検証計画

#### 2.11.1 SBI経路

IMAを無効にした専用起動で、実Linux検証モジュールから実SBIを呼ぶ。

| 試験 | 合格条件 |
|---|---|
| GET_CAPS | PCR数24、SHA-256、32バイト |
| 全初期値 | PCR 0〜23がゼロ |
| 1回・反復extend | 独立SHA-256計算と一致 |
| 独立性 | 指定PCR以外が不変 |
| 境界 | 0と23が利用可能 |
| 不正番号 | 24以上を拒否、全PCR不変 |
| 不正引数 | 予約値・出力長違反等を拒否 |
| 不正出力先 | SM停止せずread失敗 |
| 権限 | enclaveからの要求を拒否 |
| 同一PCR並行extend | 更新消失なし |
| 別PCR並行extend | 各PCRが独立した期待値 |
| read競合 | 更新前後いずれかの完全な値 |
| hart再開 | PCR維持 |
| 新規起動 | 全PCR初期化 |

同一PCR並行試験は同じダイジェストの反復等で、順序に依存しない期待値を使用する。

#### 2.11.2 IMA統合

1. 新規起動でSBI選択を確認。
2. PCR10の限定ポリシーでテストファイルを測定。
3. バイナリ測定ログを取得。
4. テンプレートハッシュを再計算。
5. PCRごとに順序どおりextendを再現。
6. SMのread結果と比較。
7. PCR0・23等のポリシーを試験。
8. violation、重複抑制、範囲外PCR、SBI非対応を試験。
9. extend失敗時の異常状態保持を確認。

表示ハッシュを無条件利用せず、テンプレートのバイナリ表現、ハッシュ方式、violationを正しく処理する。測定対象を限定し、ログ採取とPCR read間に新規測定が入らない制御された条件で照合する。

#### 2.11.3 回帰と証跡

既存CREATE／RUN／STOP／RESUME／EXIT／DESTROY、乱数、暗号機能の対応範囲、PMP、マルチhart、SBIバックエンド無効時の既存IMA経路を確認する。

AGENTS.mdに従い、模擬SBI・PMP・CSR試験を統合試験の代用にしない。`make -C verifications guest`、`make -C verifications kernel-modules`を利用し、`bash verifications/run-all.sh`はシステムゲスト内のみで実行する。

ビルド成功、実QEMU成功、実機成功を分けて記録する。新規のKeystone関連不具合を発見した場合は既存bug registerへ登録する。

### 2.12. セキュリティ範囲

提供するもの：SM内PCR状態、Linuxからの直接書き換えに対する保護、指定PCRのSHA-256 extend、ログとの整合性検査。

PCR・IMA単体では保証しないもの：Linuxによる測定の完全性、侵害後の測定省略・任意extend防止、ファームウェア真正性、物理TPMの耐タンパ性、署名付きリモート証明、実機DMAを含む全経路への保護。
署名付きリモート検証はQuote機能で追加するが、その信頼性はAKの登録・保護と許可firmware等の検証policyに依存する。

PCR extend自体はセキュアブートから独立して実装できる。実機QuoteのAK解放・firmware真正性を保証する条件は、第9節のAK管理設計で扱う。

### 2.13. 成果物と受け入れ条件

成果物：SM実装とSBI定義、Linux組み込み連携とIMA変更、再現可能な設定、実ゲスト試験、ログ再計算ツール、ビルド・起動手順、コミット・設定・成果物ハッシュ・実行ログ、未検証事項。

受け入れ条件：

1. PCR24本を独立操作できる。
2. GET_CAPSがPCR数24を返す。
3. EXTENDとREADでPCR番号を指定できる。
4. IMAログ再計算値とSMのPCRが一致する。
5. 不正要求でPCRが変わらない。
6. 既存機能の回帰試験結果を提示する。
7. QEMUと実機の検証範囲を明示する。

設計変更が必要になった場合は、その理由と最終仕様を本書へ反映すること。

## 3. 処理経路

```text
Linux IMA測定 → PCR_EXTEND → OpenSBI/SMのSHA-256 PCR bank
                                       ↓ 同じlock下でsnapshot
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

## 4. TPM構造とwire形式

TCGの型名は仕様の型を指す。RV64 little-endianのC構造体をそのまま送信しない。
整数はすべてbig-endian。可変長要素は宣言されたsize分だけ送る。
`sizeof(TPMS_ATTEST)`、union全体、Cのpaddingはwire長ではない。

### 4.1 `TPM2B_ATTEST`

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
| extraData | `TPM2B_DATA`、初期profileは64 bytes。第5節のnonce＋context hash |
| clockInfo.clock | UINT64、profile依存のClock。揮発開発profileはSM起動後の経過ms。第9.9節 |
| clockInfo.resetCount | UINT32、volatile-devでは0。protected-persistentは第9.7節で永続化 |
| clockInfo.restartCount | UINT32、volatile-devでは0。Reset/Restart相当イベントを第9.7節で定義 |
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

### 4.2 署名対象と`TPMT_SIGNATURE`

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

## 5. nonce、補足context、PCR snapshotの結合

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
| persistent_state_epoch | 32 bytes、provisioningで固定する永続状態のepoch。volatile-devは未対応値 |
| durability_class | UINT32、volatile／file-dev／protected-persistentを区別 |
| nv_index | UINT32、Quoteへ結合する固定NV counter index |
| nv_initialized | 1 byte、NV counter初期化済みか |
| nv_counter_value | UINT64、snapshot時点のdurable値。未初期化時0だがvalid値とは区別 |

このcontext v1は未実装の草案であり、今回counter用fieldを追加した。既存の配布済みABIを変更するものではない。
永続counterを返さないprofileでは未対応を明示し、0を本物のNV counterの証明としない。
domainのbyte数、各enum値、測定するlinker sectionと長さprefixはABI headerと生成ツールで
一意に定義し、実装開始時に仕様へ固定値を追記する。任意のLinux申告hashを署名しない。
firmware measurementはmutable dataを除く測定範囲をcold initで計算し、公開manifestと照合する。
単なるhash計算はsecure bootや測定値の真正性を保証しない。第9節の鍵解放条件も必要。

boot_idは実機profileでは信頼できるentropyで生成し、再起動で変更する。counter backendを
identityやECDSA blinding用のentropyとして使わない。qemu-devの乱数は実機保証の対象外。
clockはOpenSBI timer値と検証済みfrequencyからmsへ変換し、逆行を検出したら異常扱いにする。
初期volatile-devではsafe=0で永続reset/restart counterは無い。protected-persistentの保存契約は第9.7〜9.10節で定義する。
どのprofileでもfreshnessはnonceで判定し、clockだけで再起動をまたぐrollback防止を主張しない。

SMは選択PCR、update counter、異常latch、boot_id等を同じsnapshotに収める。
PCR値を補足応答に返し、その値のdigestが署名済みpcrDigestと一致することを検証者が確認する。
これによりQuote後にPCR_READした値との競合を避ける。

IMAの測定失敗は既存PCR設計の異常latchと接続する。SMが観測した失敗とLinuxから申告された
失敗を区別し、一度立てたlatchはLinuxからclearできない設計とする。
ただし悪意あるkernelが測定・異常申告を省略することまでSMが検出できるとはしない。

## 6. AKの公開領域、Name、登録

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

## 7. SBIとLinux API

既存host用Keystone EID `0x08424b45`に以下のsymbolic機能を追加する。
第2.5節のPCR操作のFIDも未確定のため、2001〜2006、3002、4000、検証専用4900を流用しない。
数値はPCR/Quoteを一緒に実装する際に重複検索して確定する。

| 機能 | 目的 |
| --- | --- |
| PCR_GET_CAPS／PCR_EXTEND／PCR_READ | 第2.5節のPCR操作。Quoteと共通のbankを使用 |
| NV_COUNTER_GET_CAPS／READ／INCREMENT（案） | 第9.8節。PCR更新counterとは別の永続値 |
| PCR_MARK_FAILED（案） | 第2.7.4節のLinux異常申告をset-onlyでSMへ伝達。ABIは実装前に固定 |
| QUOTE_GET_CAPS | ABI/profile、アルゴリズム、最大長、provider準備状態 |
| QUOTE_GET_AK_PUBLIC | 公開鍵、Name/QN、登録用公開metadataを返す。秘密鍵は返さない |
| PCR_QUOTE | nonceとselectionからSM自身がQuoteを生成 |

PCR_QUOTEのRV64 ABI案：a0=requestのS-mode VA、a1=request bytes、
a2=responseのS-mode VA、a3=response capacity、a4/a5=0。
既存SMと同じ正のKeystone statusをa0、成功時の実response長をa1に返す。
Linux層でerrnoへ変換する。未対応・引数不正・copy失敗・provider不調・BUSYを区別する。

requestはversion、bytes、nonce_len=32、nonce、1 bankのselection、証明する固定NV index、予約値を持つ。
NV非対応profileは未対応指定だけを受け付け、値を捏造しない。検証者はselectionと同様に要求したNV indexも照合する。
C ABIは固定幅整数と明示padding、長さ上限を定義する。wire TPM整数とは別のABI。
応答は固定header＋長さで区切る以下のblobとし、各TPM blob内は第4節のwire形式とする。

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

## 8. 排他、copy、エラー、library

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

## 9. Jupiter2のAK・counterとpersistent storage管理

### 9.1 現時点で分かっていること

[既存ハードウェア調査](jupiter2-hardware-compatibility.md)と
[WTM/OP-TEE調査](jupiter2-security-engine-rng.md)で、K3向けWTM RNGのベンダーdriver、
OP-TEE platform、TE200等の関連定義を確認している。
公式K3 datasheetにはeFuseと256-bit NV Counterの記載がある（第9.11節）。
一方、**P-256の非exportable key slot、device-unique HUK API、secure storage、RPMB鍵、
AK signing RPCがJupiter2実機で利用可能であることは確認していない。**

WTM RNG、暗号エンジン、独立セキュアCPU、RT24管理コア、AP上のOP-TEE trusted domainを
同一物として扱わない。RNGがあることは、秘密鍵を永続保存できることの根拠にならない。
OpenSBI MPXYにはOP-TEE dispatcherがあるが、それだけでAK serviceが完成するわけではない。

### 9.2 管理方式の比較

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

### 9.3 推奨する段階とprovider境界

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

### 9.4 HUK wrap方式の具体案

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

### 9.5 実機で確認したい情報

- SoC security manualとベンダーAPI：key slot、P-256/ECDSA、HUK/KDF、署名、zeroize、権限設定。
- BootROM→firmwareの認証、debug無効化、anti-rollbackの実際の設定と検証済みimage。
- security processor/TE200/WTM/RT24/OP-TEEの正確な担当、firmware version、mailbox ABI。
- Linuxがkey engineや署名serviceへ直接アクセスできないことを示すfirewall/PMP/IOPMP設定。
- OP-TEEのHUK取得とsecure storage backend、eMMC RPMBのprovisioning・認証・書換耐性。
- key生成後のreboot、電源断、firmware更新、鍵rotation、失効、他boardへのblobコピーの結果。
- 暗号entropyのhealthと失敗時挙動、署名throughput、最大応答時間、timeout後のkey状態。

機密鍵そのものの提供は不要。API仕様・公開設定・公開鍵・試験結果で確信度を上げる。

### 9.6 TPMのcounterを区別する

今回の実装対象にcounterとその永続化providerを加える。次の値は用途と寿命が異なる。

| 値 | 幅 | 寿命・更新契機 | Quoteとの関係 |
| --- | --- | --- | --- |
| `pcr_update_counter` | 64 bit（本設計独自） | 同一SM起動内のextend成功ごとに増加。PCRとともにcold initで0 | 補足context。永続NV counterとは別 |
| `TPMS_CLOCK_INFO.resetCount` | 32 bit | TPM Reset相当のfirmware lifecycleで増加する永続状態 | TPMS_ATTESTに標準フィールドとして含まれる |
| `TPMS_CLOCK_INFO.restartCount` | 32 bit | 同一Reset期間内のTPM Restart等の規定イベントを数える。Resetで0 | 同上 |
| `TPMS_CLOCK_INFO.clock` | 64 bit、ms | 電源投入中の時間を基礎にするClock。永続checkpointとsafeの扱いが必要 | 同上。自由に書けるLinux時刻を使わない |
| NV counter | 64 bit、8 octets | `TPM_NT_COUNTER`相当。認可されたINCREMENTで更新し、電源断後も保持 | 通常のQuoteには標準フィールドがない。下記の補足contextで結合 |
| storage generation | backend依存 | 保存transactionごとに増加する内部rollback検出値 | TPMのNV counter値、RPMB write counterと同一視しない |

TPM2のNV counterはTPM2_NV_Incrementで更新する8-byte NV indexであり、
「TPMのすべてのcounterがNV counter」という意味ではない。
reset/restartは採用するTCG版のReset/Restart/ResumeとStartup/Shutdown状態遷移に照合する。
初期の本サービスはfull TPM command lifecycleを実装せず、対応するsubsetと未対応操作を明示する。

### 9.7 対応profileとlifecycle

| profile | 動作 | 受け入れpolicy |
| --- | --- | --- |
| `volatile-dev` | PCRと起動内counterのみ。reset/restart=0、safe=NO。永続NV APIは未対応 | 開発用。再起動をまたぐ単調性を保証しない |
| `file-dev` | 開発hostのstate fileで再起動・電源断復旧を試す。認証recordでもsnapshot巻き戻しは可能 | QEMU検証用。hardware-protected persistentと区別 |
| `protected-persistent` | 認証、durable commit、旧状態拒否を実証したproviderでreset/restart/NV状態を保存 | 実機の目標。必要条件を満たさない場合にこのprofileを名乗らない |

既存の「初期profileではcounter=0」はvolatile-devの仕様として残し、
永続counterの目標をprotected-persistentへ拡張する。profile、backend class、state epochを
署名対象contextとGET_CAPSで公開する。storage不調時に揮発値へ自動fallbackしない。

protected-persistentの初期lifecycle：

1. provision済みrecordとprotected anchorを検証する。通常bootでrecordが消えていても新規0状態を作らない。
2. cold/warmの完全firmware再初期化を、このsoftware profileのReset相当と定義する。
   resetCountを1増加しrestartCount=0にしたrecordをcommitしてから、Quote/NV APIをreadyにする。
3. secondary hart初期化、hart STOP/START、enclave操作ではどちらも更新しない。
4. Resetを伴わないRestart/Resume、kexec、suspendは初期対象外。
   将来対応時はTCGの遷移表に従いrestartCount等を更新し、単なるLinux再起動検出で代用しない。
5. 32-bit counterや世代のoverflowは検出してサービス停止とする。黙って0に戻さない。
6. TPM2_Clear相当やfactory resetを通常Linuxへ公開しない。必要なprovisioningは別の認可経路とし、
   検証者にも新しいownership/state epochを登録する。AK rotationだけでcounterを初期化しない。

cold initでPCRを0にすることと、永続counterを0にすることは別である。
Quoteにはその時点でRAMへ公開済みの、durableなcounter状態だけを入れる。

providerがOP-TEE等の起動後にしか利用できない場合、SMのcold initをI/O待ちで止めない。
PCRは独立して初期化し、protected Quote/NVはNOT_READYのままとする。
providerの認証・counter commit後に一度だけreadyへ遷移する。
boot transitionにはSM生成のboot_idを使い、応答喪失後の再loadで同じtransitionのcommit済みrecordを
確認できれば二重incrementしない。新たなfirmware bootでは新しいtransition IDを使う。

### 9.8 NV counter APIと署名への結合

初期実装はprovisioningで固定した少数の64-bit counter indexのみとし、任意NV領域は設けない。
動的なNV_DefineSpace/NV_UndefineSpace、TPM session/authorization protocol全体は対象外。
初期のindex数・識別子・認可policyはQ0で固定する。

| 新規symbolic SBI | 契約 |
| --- | --- |
| NV_COUNTER_GET_CAPS | index数、64-bit幅、初期化状態、durability/rollback保護class、rate limit |
| NV_COUNTER_READ | a0=index、a1〜a5=0。a0=status、成功時a1=現在値 |
| NV_COUNTER_INCREMENT | a0=index、a1〜a5=0。認可・overflow・storage確認後、永続commitしてからa0=成功、a1=新値 |

数値FIDはPCR/Quoteと一緒にhost範囲で割当てる。SM/enclave・Linux errnoの規約は第7節と同じ。
管理用Linux interfaceは通常Quote取得と別に認可し、無制限incrementで寿命を消費させない。
SBIは同一S-mode内の悪意あるkernel callerを識別できないため、認可をioctlだけで保証しない。
実機providerではSMだけに書込み権限を与え、kernelに対する可用性限界も明記する。

TPM referenceの初期化規則を踏まえ、未初期化indexのREADは未初期化error、最初のINCREMENTは
永続的なglobal high-water markを基準に初期化・増加する。以後の正常INCREMENTは現在値+1。
各indexの初期化状態とglobal high-water markも同じtransactionで保存する。
index再作成で過去に使った値へ戻れる設計にしない。最大値ではoverflowを返し、wrapさせない。
TPM2 command packetやTPM_RCをそのままSBI ABIに流用するわけではない。

INCREMENT成功はRAM更新完了ではなく、電源断後に新値を復旧できるcommitの完了を意味する。
timeoutや応答喪失時は結果不明になり得るため、無条件再送しない。READで状態を調べても
他callerの同時incrementがある場合は自要求の成功を一意には判定できない。
必要なら将来versionで永続request-idによる重複排除を追加するが、現ABIでexactly-onceを約束しない。
NV counterにはClockのcheckpoint方式を流用せず、成功incrementごとにcommitする。

TPMのQuoteはPCRの証明であり、NV counterを`TPMS_QUOTE_INFO.pcrDigest`へ混ぜない。
本profileでは補足contextに`persistent_state_epoch`、`durability_class`、`nv_index`、
`nv_initialized`、`nv_counter_value`を追加し、第5節のextraData内context hashを通して署名する。
これはKeystone固有の結合であり、標準TPM2_NV_Certify実装とは呼ばない。
標準の独立NV証明が必要なら、別段階でTPM_ST_ATTEST_NVとTPMS_NV_CERTIFY_INFOを実装する。

### 9.9 Clockの永続化とsafe

ClockはNV counterやRTCと別物。Linuxのwall-clockやRTCの電池保持だけでは単調性を保証できない。
TPMのClockはpowered-time、周期的NV反映、Reset/Restart、safeの規定を持つため、
「bootごとの経過msを保存した」だけでfull TPM Clock互換とはしない。

初期protected-persistent profileは、公開したClock値を再公開時に下回らせないため、
永続recordにClockの予約上限を持つ方式を採用する。

1. boot時は前回のdurable予約上限を新しいbaseとし、新しい範囲の上限をcommitしてからClockを返す。
2. Clockはbase＋SMの検証済みmonotonic timerの経過ms。範囲を越える前に次の予約上限をcommitする。
3. 電源断後は未使用分を捨てて、前回予約上限以上から開始する。時間が前へ跳ぶことはある。
4. commit失敗・timer逆行・加算overflow時は新たな安全なClock値の公開を停止する。

これは書込みを毎ms行わないsoftware monotonic-clock profileであり、
実際の通電時間と完全一致するTPM Clockの全規定を実装したという意味ではない。
予約幅、書込み頻度、timebaseの扱いはproviderの耐久性と合わせて固定する。
safeは初期実装ではNOを維持する。YESを返すのは、状態巻き戻し不可、複数hart、
電源断の全経路でTCGのsafe条件を満たすと検証したprofileだけに限定する。
nonceによるfreshness検査と、このcounter/Clockの検査は両方行う。

### 9.10 persistent storage providerと電源断transaction

SMがLinux filesystemへ直接書き込む設計にはしない。共通providerを以下の契約で分離する。

- `load_verified_state()`：recordのdevice binding、認証、版、世代とprotected anchorを検証。
- `commit_state(expected_generation, new_record)`：複数writerを直列化し、durabilityと旧状態拒否を満たして成功する。
- `get_storage_caps()`：媒体、認証、anti-replay、atomicity、耐久性/残量、最大応答時間を公開。
- provisioningだけが新規epochを作れる。通常処理からerase/reset/deleteはできない。

recordにはformat/version/length、device identity、state epoch、AK generation、storage generation、
resetCount、restartCount、Clock予約上限、NV counter値/初期化状態/global high-water mark、
lifecycle状態、直近のboot transition ID、認証tagを含める。公開counter値は必ずしも暗号化が必要ではないが、
認証とrollback防止は必要。AK blobは第9.4節の暗号化も必要とする。

protected storageがrecord単位のatomic replacementを保証するならそれを利用する。
そうでなければA/B journalとprotected anchorを組み合わせる。

1. old generationを検査し、新recordをinactive slotへ書く。
2. durable flush／readbackを完了し、そのrecordのdigestを確定する。
3. protected anchorのgenerationとrecord digestを一体としてcommitする。
4. その後RAM状態へ公開し、callerへ成功を返す。old slotの回収はさらに後に行う。

復旧時はanchorが指す正確なrecordだけを採用する。候補slotの最大sequenceを選ぶだけでは、
両slotを古い正規データへ戻す攻撃を防げない。anchor更新前のcandidateは未commitとして扱う。
anchorが進んだのに対応recordが無い場合、古いrecordへfallbackせずstorage errorにする。
MAC付きrecordであっても過去の正規recordはMAC検証に通るため、MAC単独をanti-rollbackとしない。

単なるbit消費型eFuse counterは、ここで必要なgeneration＋record digestのatomic anchor APIを
そのまま満たすとは限らない。二相commit・電源断復旧まで確認した専用設計が必要。
blobとAK generationも同じ保護stateへ結び付け、counterだけ新しく鍵だけ古い状態を受け入れない。

NV更新用mutexは署名workspace/PCR spinlockと分離する。storage I/O中はPCR lockを持たない。
commit後に短時間だけPCR snapshot lockを取得し、RAM上のdurable counter cacheを公開する。
QuoteはそのlockでPCR/counter/flagsを同時copyするため、署名中に後続更新があっても
snapshotは一つの合法な時点に対応する。storage世代とTPMの値が同じ数とは限らない。

### 9.11 Jupiter2でどの媒体を使うか

**「Jupiter2にはpersistent storage／不揮発counterが存在しない」とは言えない。**
確認できた根拠と、まだ確認できていない利用条件を分ける。

| 候補 | 確認できた根拠 | counter用途で残る問題 |
| --- | --- | --- |
| SPI NOR | 提供済み実機ログに`d420c000.spi`とbootinfo/fsbl/env/esos/opensbi/ubootのMTD partition。末尾境界0x800000 | 通常の書換可能flash。専用空き領域・耐久性・power-fail atomicity・Linux/外部書換保護は未確認。既存boot領域をcounter保存に流用しない |
| K3 eFuse/NV Counter | datasheet第2.8.3節とmanual第15.3節。K3 U-Bootではbank 5のARCN-NS 224 bit＋ARCN-Sec 32 bit。汎用eFuse書込みは既存値にORしてburn | 不可逆bitを消費する方式として扱う。1段＝1 bitなら最大224／32段だが、正式なdecoder・1段消費量・残量・bank 5更新API・電源断動作は未確認。TPMの高頻度counter保存には採用しない |
| eMMC RPMB | K3 datasheetはeMMC 5.1 controller対応を記載 | board/実機に搭載したeMMCの有無・型番・RPMB容量・key provisioning・secure経路は未確認。controller対応だけでRPMB利用可能とはしない |
| NVMe/通常file | 一般の永続媒体でstate fileを保存可能 | OSからの消去・過去snapshot復元を単独では防げない。開発用又はprotected anchorとの併用 |
| OP-TEE secure storage | K3 OP-TEE platformが存在し、eFuse/HUK関連設定がある | REE-FSとRPMBを区別。実際のbackend、正規HUK、認可、rollback保証を実機で確認する必要 |
| 外付けTPM/secure element | 現時点でこのboardへの実装確認なし | バス接続、driver、provisioningが必要。十分な内蔵providerが無い場合の選択肢 |

追加調査で、NV CounterはeFuse bank 5のfirmware image向けanti-rollback資源と確認できた。
詳細と根拠は[storage調査第5節](jupiter2-persistent-storage-investigation.md)に記録する。
bootごと・NV_INCREMENTごと・Clock checkpointごとに消費させる設計にはしない。
低頻度anchorとしても、正式な符号化・更新API・残量・既存boot chainとの所有権を確認してから採否を決める。
TPM counter用の書換可能な認証storageについては、利用可能なeMMC RPMB/secure serviceを優先して調査する。

RPMBの内部write counterは一般に認証書込みの順序管理用であり、TPMのNV counterそのものではない。
OP-TEE等の別利用者のwriteでも進むため、値を直接TPM counterへ対応付けない。
共有RPMBではproviderが全writerを調停し、RPMB keyをLinuxへ渡さない。

Linux userspaceによる保存代行は、認証frameの転送経路にはできるが、Linuxをdurable commitの
唯一の証人にはしない。応答抑止のDoSは残る。通常fileだけのbackendでは、同じAKのまま
古いcounterへ戻せないという保証はできない。外部検証者のhigh-water記録は補助策であり、
offlineで動くTPM相当のlocal monotonic storageの代用とはしない。

K3 OP-TEEの確認対象commitのconf.mkには`CFG_TE200_USE_DUMMY_OTP ?= y`、
`CFG_K3_HUK_ALLOW_UNFUSED ?= y`があり、後者にはblank fuse時のpublic development HUKという
説明がある。これらの存在を、実機のHUKがprovision済み・秘密保持済みである証拠にはしない。
本番profileはdevelopment fallbackを拒否し、設定・実際に選ばれたproviderを検証する。

追加で欲しい実機情報は、搭載storage一覧、`/proc/mtd`、eMMC型番とRPMBのread-onlyな能力情報、
OP-TEE build設定、公開API仕様、K3 NV Counterのprogramming manual、残り更新可能回数、
secure boot/debug lifecycleの設定である。秘密鍵の開示やeFuse/RPMB keyの不可逆programmingは不要。

### 9.12 counter/storageの実装・試験

counter実装とAK/Quoteの実装を段階分けし、以下をQEMUの実Linux→OpenSBI→SM経路で検証する。

- file-dev backendで、process再起動だけでなくQEMUの完全停止・再起動後の値を確認する。
- READ、初回INCREMENT、反復increment、同一/別index競合、権限拒否、overflow、rate limit。
- reset/restartの対応subset、secondary hart初期化で不変、AK rotationでNV値不変。
- journal書込み、flush、anchor commit、RAM公開、応答の各境界でguestを強制終了し、
  成功応答済みのNV値が下がらないことを確認する。応答喪失による結果不明も区別する。
- 正常bootでrecord欠落・改変・古い認証record・別board blob・旧鍵blobを拒否する。
- protected anchorを保持してjournalだけを巻き戻した場合の拒否、anchor/媒体の不整合でfail closed。
- Clock予約上限の更新と電源断、timebase逆行、safe=NO、counterとPCRのsnapshot一致。
- 永続counter/contextの改変、古いQuoteと同じnonceの再利用、署名済みの異常flagsを検証する。
- storage I/O対PCR_EXTEND/QUOTE/3002/3003、timeout、BUSY、書込み寿命/残量・key状態を記録する。

QEMU上のfileはhostに保護を委ねる。fileとmodel全体を一緒に戻せるhostに対してまで
anti-rollbackを証明したことにはならない。実storageの耐久性・RPMB/eFuseの不可逆性・
実機のsecure権限はJupiter2実機で別途検証する。現時点でcounter試験のPASS記録はない。

## 10. 検証者の手順

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

## 11. 実装順序と検証計画

| 段階 | 作業・完了条件 |
| --- | --- |
| Q0 | PCR/Quote/NV counterのFID、context/response ABI、profile/lifecycle、storage provider契約、enum、測定範囲、library versionを固定 |
| Q1 | 第2.10節に従いPCR24本/SHA-256、GET_CAPS/EXTEND/READ、Linux組み込みbackendとIMA連携、異常申告を実装。第2.11節のログreplay、atomic snapshot、update counter、latchを検証 |
| Q1-NV | counter record、READ/INCREMENT、reset/restart、Clock予約、file-dev/journal backend、storage capabilityと異常復旧を実装・検証 |
| Q2 | C marshal/unmarshal、AK public/Name/QN、Quote builder、qemu-dev ECDSA provider |
| Q3 | Linux ioctlとC client、SBI受付、外部検証者、独立parser/署名検証 |
| Q4 | 実QEMUで正常・異常・競合・障害注入と既存3002/3003等の回帰 |
| Q5 | Jupiter2のAKとpersistent storage provider選定、eFuse NV Counter/RPMBの仕様確認、provisioning/保管/rotation、実機の電源断・rollback・耐久性検証 |

想定配置：`sm/src/pcr/`、`sm/src/quote/`、`sdk/include/shared/quote_abi.h`、
Linux組み込みbackend、C client/verificationを`verifications/quote/`へ置く。
既存[異常系計画](keystone-abnormal-concurrency-verification-plan.md)にも機能対を追加する。
この文書で既存の未了試験や製品不具合を完了扱いにはしない。

PCR・IMA単体の詳細試験と受け入れ条件は第2.11節・第2.13節を適用する。以下はQuote追加分の必須試験：

- 全PCR／単一PCR／PCR0/23／複数PCR、独立SHA-256値、marshal長とbyte order。
- tpm2-tss MUでTPM2B_ATTEST、内部TPMS_ATTEST、TPMT_SIGNATURE、TPM2B_PUBLICを読む。
- challenge32＋context hash32、replay、誤AK、誤QN、誤selection、report/context/PCR改変を拒否。
- ECDSA R/S、DER変換、曲線点検査、サイズ境界、未知algorithm、二重hash・外側size混入の検出。
- PCR_EXTEND対QUOTE：単一時点のsnapshotに線形化され、PCR値とcounter/contextが一致。
- 同時QUOTE、3002/3003対QUOTE、RNG不調、workspace枯渇、secure RPC timeout、copy失敗。
- Quote失敗でPCR不変、部分出力は成功扱いにしない、keyや秘密の中間値をログへ出さない。
- Sstc on/off・複数hartの実Linux→OpenSBI→SM経路。模擬SM/PMPを合格根拠にしない。
- 実機の鍵非export性、Linuxからの署名権限、別機体blob、旧鍵rollback、更新/rotation。

現時点の結果：**設計レビューのみ。PCR・counter・IMA連携・Quoteの実装・試験のPASS記録は存在しない。**

## 12. 参照

- [TCG TPM 2.0 Library Specification](https://trustedcomputinggroup.org/resource/tpm-library-specification/)：Part 2 Structures（TPMS_ATTEST、TPMS_QUOTE_INFO、TPMT_SIGNATURE、Name）、Part 3 Commands（TPM2_Quote）。実装時に採用revisionを固定する。
- [tpm2-tss型定義](https://github.com/tpm2-software/tpm2-tss/blob/master/include/tss2/tss2_tpm2_types.h)：2026-11-10取得。規範はTCG仕様とし、検証時はtpm2-tssのcommitも固定する。
- [tpm2_checkquote](https://tpm2-tools.readthedocs.io/en/latest/man/tpm2_checkquote.1/)：file形式とalgorithm対応を採用versionで確認する。
- PCR/IMAの詳細は本書第2節。旧文書は[統合先案内](../com/keystone-pcr-ima-design.md)として保持。[既存crypto ABI](../keystone/sdk/include/shared/crypto_abi.h)。
- [Jupiter2 WTM/OP-TEE調査](jupiter2-security-engine-rng.md)、[SBI 3003 provider設計](keystone-sbi-3003-plan.md)。
- [K3 OP-TEE platform](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/platform_config.h)、[OpenSBI MPXY dispatcher](../opensbi/lib/utils/mpxy/fdt_mpxy_opteed.c)。

- [K3 datasheet](https://github.com/spacemit-com/docs-chip/blob/3ea42a6af47f2595428e2870122829dc5e8fc856/en/key_stone/k3/k3_docs/k3_ds.md)：2026-11-10取得、第2.8.3節の256-bit NV CounterとeFuse、第2.2.4節のeMMC controller。固定commitと内容hashは下記調査記録へ保存。
- [Jupiter2 persistent storage調査](jupiter2-persistent-storage-investigation.md)：実機NORログ、datasheet、未確認事項を区別。
- [K3 OP-TEE conf.mk](https://github.com/spacemit-com/optee_os/blob/2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59/core/arch/riscv/plat-k3/conf.mk)：開発HUK/dummy OTP設定と本番利用の区別。
- [TPM reference NV_Increment](https://github.com/microsoft/ms-tpm-20-ref/blob/main/TPMCmd/tpm/src/command/NVStorage/NV_Increment.c)：初回NvReadMaxCountとincrement。規範は採用版のTCG Part 3とする。
