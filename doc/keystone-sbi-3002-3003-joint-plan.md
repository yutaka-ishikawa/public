# SBI 3002/3003同時実装計画：共通基盤を先行

共通基盤とQEMU開発用の初期実装を追加済み。確定仕様・実施済み範囲と未確認事項は
[実装記録](keystone-sbi-3002-3003-implementation.md) を参照。以下は計画時点の記録。

作成日：2026-11-09（日本時間）。調査対象は本リポジトリ `425b4f32c` 時点の
Keystone SM/SDK/Eyrieと同梱OpenSBI 1.6系K3版。
本書は [3003個別計画](keystone-sbi-3003-plan.md) を拡張し、同時実装の着手順を定める。
既存verification全PASSはユーザー報告として受領済み。本変更は調査・計画のみ。

## 1. 方針とOpenSBI再調査

- 同梱OpenSBIのSBI定義、lib/sbi、lib/utils、platform/genericを再確認したが、
  Keystone互換attestation/report生成・device→SM証明書生成のサービスは見つからなかった。
- 同梱OpenSBIにHKDF/HMAC/SHA3/Ed25519の実行時ライブラリも見つからなかった。
- よって3002はKeystoneのattest_enclave/sm_sign、3003は既存HKDF/署名を再利用する。
  新しい署名・KDFアルゴリズムを設計しない。
- OpenSBIのdispatcher、trap対応アクセス、domain補助判定、spinlock、通常のメモリ操作は
  優先して使う。PA/VAとPMPの意味が合わない関数を無条件に流用しない。
- OP-TEE MPXYはdomain間の転送機能であってattestationライブラリではない。
  初期実装では必須にせず、hardware providerが必要とする場合に再検討する。

調査範囲は同梱ツリー。将来版・他社OpenSBI全体の機能不存在は主張しない。
OpenSBIとKeystoneの再利用判断の詳細は [3003計画の第3節](keystone-sbi-3003-plan.md) を共通前提とする。

## 2. Keystoneの3002を基準にする

```text
SDK attest_enclave(report, data, length)
 → Eyrie syscall 1003
 → sbi_attest_enclave()
 → extension 0x08424b45 / FID 3002
 → OpenSBI Keystone adapter
 → sbi_sm_attest_enclave()
 → attest_enclave() / sm_sign()
```

SBI引数はa0=report出力PA、a1=入力dataのPA、a2=入力長。
正のKeystone statusをa0で返し、a1の返り値は0に統一する。
現行Eyrieは3002に作業バッファVAを渡しているが、3003と同じEPM内PA契約へ統一する。
旧経路はMPRVでVAを扱える場合があり、VAを渡すことだけを単独の確定バグとはしない。
PA統一後のsatp/MPRV/PMP適合性を確認する。
アプリのVAを扱う層はEyrie、enclaveの識別とEPM所有権を扱う層はSMとする。
Linuxホストから両FIDへの直接呼び出しはPROHIBITED。

### Reportの固定形式

既存SM/SDKが定義するRISC-V64の形式を維持する。実装時にはsizeof/offsetofを双方でassertする。

| フィールド | サイズ | report内offset |
| --- | ---: | ---: |
| enclave.hash | 64 | 0 |
| enclave.data_len（uint64） | 8 | 64 |
| enclave.data | 1024 | 72 |
| enclave.signature | 64 | 1096 |
| sm.hash | 64 | 1160 |
| sm.public_key | 32 | 1224 |
| sm.signature | 64 | 1256 |
| dev_public_key | 32 | 1320 |
| 合計 | **1352** | — |

入力dataは0～1024 bytesのバイナリ。署名対象は次のとおり。

```text
SM証明書:
    Sign_device(sm.hash[64] || sm.public_key[32])

enclave report:
    Sign_SM(enclave.hash[64] || data_len[8] || data[0:data_len])
```

enclave.signatureは可変長dataの直後へ詰めるのではなく固定offsetに置く。
dataの未使用末尾は署名対象ではないため、report全体を初期化して0にし、残留データを出さない。
署名はKeystoneのSHA3-512版Ed25519系実装。標準Ed25519/SHA-512へ単純に置換しない。

### 既存measurementの意味

[attest.c](../keystone/sm/src/attest.c) はCREATE時にloader、runtime、eappの領域サイズと
ページ内容をSHA3-512へ入れてenclave.hashを作る。eapp単独のELFファイルhashではない。
Eyrie/loader/SDKの再buildでもmeasurementが変わる場合がある。
期待値は同じ配置・padding・サイズ列を再現して独立計算し、report中の値をそのまま期待値にしない。

### 検証者の信頼の起点

SDKの [Report::verify](../keystone/sdk/src/verifier/Report.cpp) を再利用し、以下をすべて確認する。

1. **外部から信頼して設定した**device公開鍵でSM証明書を検証する。
2. 期待するSM measurementと一致することを確認する。
3. 認証されたSM公開鍵でenclave reportの署名を検証する。
4. 期待するenclave measurementと一致することを確認する。
5. report.data内のnonce等が検証者の未使用challengeと一致することを確認する。

report内のdev_public_keyを新しい信頼の起点として採用しない。外部のpinと一致を確認する。
署名検証だけではfreshnessは証明できない。nonceは検証者が発行し、使用済みchallengeを
再受理しない。これは検証プロトコル側の状態で、SMへreplay台帳を追加する計画ではない。

## 3. 先に実装する共通基盤

### C1: 鍵providerと準備状態

3002と3003で別々にSM秘密鍵を生成しない。同じSM identityを一度だけ初期化し、
同じ公開鍵・秘密鍵をreport署名とsealingに使う（Keystone既存の構成を維持）。

共通providerが供給する情報:

- SM公開鍵・秘密鍵、SM measurement
- device公開鍵、deviceによるSM証明書
- provider名/quality、初期化結果、sealing_readyとattestation_ready

`sm_is_ready()`（基本実行可能）とは別にcapabilityを管理する。
SM鍵があっても証明書がなければ3002は未準備。3003も鍵未準備なら失敗する。
初期化失敗でCREATE/RUNや3001まで止めない。全hartへ公開する前に初期化を完了し、
実行中に鍵を変更しない。切替・rotationは再起動を単位とする。

既存 [platform.c](../keystone/sm/src/platform/spacemit_k3/platform.c) の開発鍵ロードは
SM hash/certificate/device公開鍵を0にしているため、そのまま3002を有効にはできない。
既存 [B1 handoff](../keystone/sm/src/platform/spacemit_k3/k3_b1_handoff.c) は鍵取得未実装であり、
descriptor検査成功だけを鍵供給成功とは扱わない。既存providerの入口を拡張し重複frameworkを作らない。

### C2: 共通measurement・鍵の生成policy

| mode | 鍵と証明書の供給 | 位置づけ |
| --- | --- | --- |
| unavailable（既定） | 供給しない。3002/3003は未準備として返す | 通常起動の既存PMP機能を維持 |
| qemu-dev（明示指定） | 保存された開発device root、SM measurement、SM identity、開発証明書を一貫して生成 | 実QEMUでの機能試験 |
| hardware（将来） | 認証済みboot measurementと保護された実機鍵供給 | 実機API確認・実装後に接続 |

qemu-devではdevice root seedをbuild領域に0600で保存し、再build時に無断再生成しない。
SM identityはdevice rootとSM measurementに結び付けて導出する方針とし、3003の
「固定開発鍵」を次のように具体化する。

- 同じdevice root・同じSM measurementなら同じSM identity。
- SM measurementが変わればSM identityとsealing keyも変わる。
- provider内のSM seed導出は既存HKDFに専用のversion付きcontextを与え、
  3003のenclave向けinfoとは分離する。3003の外部ABI・鍵導出式は変えない。

SM measurementはOpenSBIとSMがリンクされた実行コードを対象にする。
測定するimmutable範囲、load address/relocationの扱い、可変data・秘密鍵・証明書領域の除外を
manifestで固定し、ホスト期待値生成と一致させる。
鍵や証明書を埋めてからその値までhashへ含める循環定義は避ける。
正確なlinker範囲と測定時点の確定をC2の完了条件とし、ゼロhashや未検証のbuild文字列で代替しない。

QEMU開発用device秘密鍵はfirmwareから抽出可能で、開発署名は実機endorsementではない。
ログと結果にはqemu-devを明記し、信頼する開発device公開鍵はverifierへ別途pinする。
hardwareではdevice秘密鍵をSMに渡さず前段やsecure processorで証明書を生成する方式も許容する。
ただし実際のhardware APIは未確認で、QEMU成功から実機対応済みとは判断しない。
3001のcounter出力からroot/SM鍵を作らない。

### C3: アドレス・入力コピー・一時領域

3002/3003共通で次を先に実装する。

- enclave context、Running状態、当該EPMのPA範囲、overflow、全範囲の所有権を確認する。
- OpenSBI domainチェックは補助。domain内の他メモリをEPMとして許可しない。
- data/identifierをSM内へ取り込んでから署名・KDFする。呼び出し元の入力を繰り返し参照しない。
- OpenSBIのtrap対応load/storeを優先するPAコピー契約。satp/MPRVの適合性と復帰を実QEMUで検証する。
- 4096 bytesまでのidentifier、1352 bytesのreport等を収容するbounded workspace。
  大きなVLAを避け、共通の排他・消去処理を用いる。
- enclaveメタデータのsnapshotと現在実行中enclaveの寿命を保証する。
  グローバルencl_lockを長い署名/KDF処理全体で保持しない。
- 成功・失敗時とも秘密値を消去する。通常memsetに消去保証があるとは仮定しない。
- 異常はstatusで戻す。失敗時の出力は無効とし、成功としてデータを返さない。

### C4: 共通wire定義・runtime・SDK

SMとSDKでreport/sealing structのサイズ・offsetを共有または相互assertする。
Eyrie共通ヘルパーで入力長、VA overflow、ページの読み書き可否、copyの戻り値、
PA変換、出力の必要バイト数だけのコピー、作業バッファ消去を扱う。

3002の既存経路で修正が必要な点:

- 入力長検査より前にcopy_from_userしているため、先に1024上限とVAを検査する。
- 作業バッファVAをそのままSMへ渡しているため、3003同様PAへ変換する。
- statusに関係なく2048 bytesコピーしているため、**成功時のみ正確に1352 bytes**へ修正する。
- 失敗時に古いreportをコピーしない。入出力作業バッファを初期化・消去する。

既存attest_enclave APIは出力capacity引数がないため、SDK契約上1352 bytes以上の領域が必要。
PTE検査だけでは同じページ内のCオブジェクトの実capacityは判別できない。
必要ならcapacity付きchecked SDKラッパーを追加し、既存のSBI/既存APIは維持する。

verifierも生バイト長とdata_lenを先に検査する。既存fromBytesは長さ引数を持たないため、
length付きchecked入口を追加またはその前段で検査し、data_len>1024、短いreport、
JSON負数・過大値等を署名処理や表示前に拒否する。

## 4. 共通基盤の後に接続する個別処理

### A1: SBI 3002

既存attest_enclaveを基準に、current enclaveのmeasurementと入力dataを共通snapshotへ取り込み、
reportをゼロ初期化し、providerのSM証明書を入れ、既存sm_signでenclave部分へ署名する。
1352 bytesを検査済みEPM出力へコピーする。3002の二重無効化をcapability判定に置き換える。
既存の「eidを参照した後でlockを外し再度グローバル配列を読む」経路はsnapshot契約に整理する。

### S1: SBI 3003

[3003個別計画](keystone-sbi-3003-plan.md) に従い、同じproviderのSM秘密鍵と
**同じenclave measurement**を使ってHKDFし、同じSM鍵で128 bytesのkeyへ署名する。
192 bytes出力、identifier上限4096、binary identifier、capacity検査は個別計画を維持する。
3002のSM公開鍵で3003の署名を検証できることも統合試験に加える。
連携試験では同一eapp/runtime/loaderで両SBIを呼ぶ。別パッケージのmeasurementの
差を、同じenclave identityとして誤って比較しない。

3002/3003は別capabilityのため、片方の処理失敗をもう片方の成功に見せない。
同じ開発profileで両方を使えるようにするが、片方だけの準備状態も試験する。

## 5. 実装順・完了条件

```text
C0 OpenSBI再確認・共通ABI/型の固定
 → C1 鍵provider/ready状態
 → C2 measurement・SM identity・device署名チェーン
 → C3 EPM/PAコピー・workspace・排他/消去
 → C4 runtime/SDK/verifierの共通入出力
 → A1 3002接続 ＋ S1 3003接続
 → V1 単体機能の実QEMU試験 ＋ V2 連携/再起動試験
 → R1 既存全verificationの回帰
```

| 段階 | 完了条件 |
| --- | --- |
| C0 | 1352/192 bytesのwire形式、PA契約、再利用ソースを固定 |
| C1 | 鍵未供給でも既存試験がPASSし、ready状態を明示できる |
| C2 | pinned開発device鍵でSM証明書を検証でき、実測値と独立期待値が一致 |
| C3 | 不正PA/overflow/UTM/他EPMを拒否し、後続正常処理へ復帰できる |
| C4 | 正確なサイズだけコピーし、失敗・不正長がruntime/verifierで伝播する |
| A1/S1 | 3002/3003が同じidentityを使い、既存ABIで成功する |
| V1/V2 | 以下の専用QEMU試験がPASSし、profile・成果物hash・実ログが残る |
| R1 | 既存SBI、edgecall、PMP、3001各方式の試験が全PASS |

A1/S1は共通基盤完成後の独立した作業単位とし、先に共通APIを確定してから実装する。
鍵providerやPA検査を3002/3003ごとに二重実装しない。

## 6. QEMU verification計画

新設候補は `verifications/attestation/`、`verifications/sealing/` と共通support。
実SMをホストC関数の模擬呼び出しで置き換えない。

| 分類 | 試験と期待値 |
| --- | --- |
| 3002正常 | data長0/1/7/8/1023/1024、binary/NULを含むdata、非整列・ページ跨ぎの有効領域で署名とデータが一致 |
| 証明書チェーン | pinned device鍵→SM証明書→enclave署名の両段が成功。SM/enclave measurementは独立期待値と一致 |
| 改変 | 署名対象data/hash/長さ/SM公開鍵/各signatureの改変、別device pin、別expected hashで検証失敗 |
| 非署名領域 | 未使用data末尾は0で、残留データなし。ここを暗号署名の対象だとは扱わない |
| サイズ | 出力1352 bytes前後のcanary保持。2048 bytes書き込みがない。checked wrapperの不足capacity拒否 |
| 異常入力 | 1025、短いreport、data_len>1024、不正VA/PA、overflow、UTM/他EPM/SM宛てを拒否。VM継続 |
| Freshness | 正しい未使用nonceだけ受理。古いnonceや既受理reportはverifierのchallenge管理で拒否 |
| lifecycle | STOP/RESUME、DESTROY/再CREATE後も同じ測定値・identity。同じdataの署名が再検証可能 |
| measurement変更 | 別eapp/Eyrie/loaderでenclave hashが変わり、旧expected hashで拒否 |
| 同時動作 | 複数enclave・複数hartでreport、identifier、workspaceが混線しない |
| host呼び出し | Linuxから3002/3003はPROHIBITED、a1=0 |
| provider異常 | unavailable/鍵失敗/証明書失敗で該当capabilityだけ失敗。ゼロ鍵/ゼロhashで成功しない |
| 3003個別 | 同じidentifierの再現性、異なるidentifier/measurementでの分離、最大長・署名検証等は個別計画の全項目 |
| 両SBI連携 | 3002が証明するSM公開鍵で、同じSMの3003署名を検証可能 |
| 同一imageで再起動 | 同じ開発device root/SM image/enclave imageならidentityとsealing keyが再現 |
| SM image変更 | canonical測定対象を変更するとSM hash、SM identity、sealing keyが変わり旧SM pinでは拒否 |
| root変更 | device pin/証明書チェーンが変わり旧pinでは拒否 |
| 回帰 | 全既存verificationと3001の3方式・異常ケースが引き続きPASS |

署名されたreportを受け取るだけで通信相手が同じenclaveだと認証したとは扱わない。
実際の通信相手の認証が必要なサンプルでは、nonceだけでなくenclave内のセッション公開鍵等を
report.dataへ束縛し、その鍵の所有証明まで行う。これは3002単体の基本試験と区別する。

## 7. build/install・文書・記録

- rootのOpenSBI/SM buildに共通provider/measurementと両FIDを統合する。
- Eyrie/SDK buildに共通wire定義・checkedコピー・verifier入力検査を統合する。
- `make verifications` / `install-verifications` とrun-allへ両専用試験を追加する。
- qemu-dev専用firmware、鍵生成物、測定manifest、verifier公開鍵pinを別build領域で管理する。
- 起動オプションは共通providerを一度選ぶ設計にし、attestation/sealingで別の鍵を選ばせない。
  具体的な新オプション名は実装時に確定し、本計画時点では使用できない。
- 既存のbuild/install分離を維持し、installで鍵を再生成しない。
- ログにprovider/capability、成果物hash、verifier期待値の出所、PASS/FAIL理由を残す。
  開発秘密鍵・sealing keyは表示しない。reportの公開可能dataにも秘密を含めない。
- 実施済み試験と未実施の実機確認を区別してdocと累積変更記録に残す。

## 8. 主な参照ソース

- [SM reportと型](../keystone/sm/src/enclave.h)、[attest_enclave](../keystone/sm/src/enclave.c)
- [SM key/hash/sign](../keystone/sm/src/sm.c)、[enclave measurement](../keystone/sm/src/attest.c)
- [SDK report型](../keystone/sdk/include/verifier/Report.hpp)、[SDK verifier](../keystone/sdk/src/verifier/Report.cpp)
- [Eyrie syscall](../keystone/runtime/call/syscall.c)、[SM helper](../keystone/sm/src/sm-sbi.c)
- [K3 provider](../keystone/sm/src/platform/spacemit_k3/platform.c)、[B1入口](../keystone/sm/src/platform/spacemit_k3/k3_b1_handoff.c)
- [OpenSBI SBI定義](../opensbi/include/sbi/sbi_ecall_interface.h)、[unprivアクセス](../opensbi/lib/sbi/sbi_unpriv.c)
- [OpenSBI OP-TEE MPXY](../opensbi/lib/utils/mpxy/fdt_mpxy_opteed.c)

現在は計画作成までで、3002/3003の有効化と新verificationの実行結果はまだない。
