# SBI 3003（sealing key取得）の実装方針・計画

調査日：2026-11-09（日本時間）。対象は本リポジトリ `2c99fb066` 時点の
Keystone SMと、同梱OpenSBI 1.6系K3改造版です。ユーザーから既存verificationsの
全PASSを受領した状態を出発点とします。本変更では計画を作成し、3003の有効化は行いません。

## 同時実装に伴う更新

3002との同時実装は [共通先行計画](keystone-sbi-3002-3003-joint-plan.md) に従う。
本書の機能要件・ABIは維持し、着手順は共通計画C0～C4を先行してからA1/S1へ進む。
qemu-devでは固定するのはdevice rootで、SM identityはSM measurementに結び付ける。
同じimageの再起動では再現し、SM image変更時にはsealing keyも変わるpolicyとする。
本書の「3003だけを有効化」は当初の単独実装方針であり、現在は両SBIを独立capabilityで
管理しつつ、共通profileで同時に実装・検証する方針へ更新する。

## 1. 結論と優先順位

1. 同梱OpenSBIにsealingサービスがあるか確認したが、該当するAPI・実装は見つからなかった。
2. HKDF、HMAC、SHA3、Ed25519の実行時ライブラリも同梱OpenSBIには見つからなかった。
   したがって、3003の導出と署名は既存Keystone SMの実装を再利用する。
3. OpenSBIのecall dispatcher、trap対応アクセス、domain判定、spinlock、メモリ関数等は
   再利用する。Keystone固有のenclave識別・measurement・EPM所有権はSMで管理する。
4. 最初に明示的なQEMU開発用鍵providerで実経路を完成させる。
   Jupiter2の実機秘密鍵providerは同じインターフェイスの別実装として接続する。
5. 3002/3003は共通基盤の後に接続し、準備状態は個別capabilityで判定する。

これは同梱ツリーを調査した結果であり、将来版・他ベンダーのOpenSBI全体に
同種実装が存在しないという主張ではない。実装着手時に利用するツリーで再検索する。

## 2. Keystone SMの既存実装を基準にする

### 呼び出し経路とABI

```text
SDK get_sealing_key()
 → Eyrie syscall 1004
 → Eyrie sbi_get_sealing_key()
 → Keystone extension 0x08424b45 / FID 3003
 → OpenSBI Keystone adapter
 → sbi_sm_get_sealing_key()
 → get_sealing_key()
 → sm_derive_sealing_key() と sm_sign()
```

| 項目 | 維持する仕様 |
| --- | --- |
| a0引数 | 出力structのアドレス |
| a1引数 | key identifierのアドレス |
| a2引数 | identifierのバイト数 |
| a0戻り値 | 正のKeystone status。成功0 |
| a1戻り値 | データ返却なし。adapterでは0に統一 |
| 出力 | key 128 bytes + signature 64 bytes、計192 bytes |
| 呼び出し元 | enclave実行中のEyrieのみ。LinuxホストからはPROHIBITED |

アドレスに注意する。SMのspec文書はvirtual addressと書いているが、現行Eyrieの
3003経路はSMへ渡す前に作業バッファを `translate()` している。
**現行K3移植では、Eyrie→SMをEPM内の物理アドレスとして明確化する。**
U-modeアプリのVAはEyrieが扱い、SMがそのVAを直接dereferenceする仕様にはしない。

### 導出と署名

現行SMの計算を維持する。

```text
IKM  = sm_private_key（64 bytes）
info = enclave measurement（64 bytes） || key_identifier（任意のバイナリ列）
key  = HKDF-HMAC-SHA3-512(saltなし, IKM, info, output=128 bytes)
signature = KeystoneのEd25519系署名(key[128], SM public/private key)
```

- saltなしはHKDF内でゼロsaltとして処理される。呼び出すたびに乱数saltを追加しない。
- 同じroot/SM identity、measurement、identifierなら同じkeyを得る。
- identifierは文字列限定にせず、長さを持つバイナリ列とする。
- SMの署名実装はハッシュにSHA3-512を使う。SDK verifierもSHA3で検証する。
  **標準Ed25519/SHA-512のライブラリへ単純置換してはならない。**
- このSBIは鍵の取得であって、ユーザーデータの暗号化、保存、anti-rollbackまでは行わない。

上流specは再起動をまたぐ永続性を保証していない。再起動後に同じkeyを得られるかは
providerの秘密鍵が安定しているかで決まる。開発用固定鍵の試験と実機の永続保証を分ける。

### 主な参照ソース

- [FID定義](../keystone/sdk/include/shared/sm_call.h)、[SM spec](../keystone/sm/spec/v1.0.md)
- [SDK API](../keystone/sdk/src/app/syscall.c)、[出力struct](../keystone/sdk/include/app/sealing.h)
- [Eyrie syscall](../keystone/runtime/call/syscall.c)、[ecallラッパー](../keystone/runtime/call/sbi.c)
- [adapter](../keystone/sm/src/sm-sbi-opensbi.c)、[SM helper](../keystone/sm/src/sm-sbi.c)
- [鍵導出と署名呼び出し](../keystone/sm/src/enclave.c)、[SMの導出入力](../keystone/sm/src/sm.c)
- [暗号ラッパー](../keystone/sm/src/crypto.c)、[HKDF](../keystone/sm/src/hkdf_sha3_512/hkdf_sha3_512.c)
- [SHA3版の署名](../keystone/sm/src/ed25519/sign.c)、[SDKの署名検証](../keystone/sdk/src/verifier/ed25519/verify.c)
- [既存sealingサンプル](../keystone/examples/tests/data-sealing/data-sealing.c)

既存のwith_outputサンプルは鍵そのものを表示するため、新しいverificationの既定動作には採用しない。

## 3. OpenSBIで確認した機能と採否

以下の表は `opensbi/lib/`、`include/`、`platform/generic/`、`docs/` を
sealing、key derivation、HKDF、HMAC、SHA3、Ed25519、crypto等で検索して確認した結果。

| 調査対象 | 結果と実装での扱い |
| --- | --- |
| 同等のsealing SBI | [SBI定義](../opensbi/include/sbi/sbi_ecall_interface.h)・dispatcher群に該当サービスなし。Keystone 3003を接続する |
| HKDF/HMAC/SHA3/署名 | 該当する実行時ライブラリなし。既存Keystoneの実装をそのまま使う |
| fw_dynamic_sign.itsのSHA256/RSA | ホスト側FIT生成・署名の指定。SMから呼ぶ暗号ライブラリではない |
| platform内のuboot_keyファイル | FIT用の鍵ファイルであり、デバイス固有のsealing rootとしては使用しない |
| [OP-TEE MPXY dispatcher](../opensbi/lib/utils/mpxy/fdt_mpxy_opteed.c) | OP-TEE domainへのメッセージ転送。sealing/KDFやKeystone measurementの実装ではない。初期実装では使わない |
| [domain範囲判定](../opensbi/include/sbi/sbi_domain.h) | 補助検査として再利用可能。ただしdomain内＝呼び出しenclaveのEPMではない。EPM所有権検査の代わりにはしない |
| [sbi_load/store](../opensbi/include/sbi/sbi_unpriv.h)・[実装](../opensbi/lib/sbi/sbi_unpriv.c) | trapを回収する既存アクセスを優先検討。後述のPA/VA条件を満たすラッパーで使う |
| OpenSBIのspin_lock等 | provider状態・共有作業領域の排他に再利用 |
| sbi_memcpy/sbi_memset | 通常コピー・初期化に再利用。秘密値の消去は最適化で消えないことを別途保証する |

MPXYを使ってもOP-TEE側にprovider実装が必要で、さらにOpenSBI domain/PMPと
Keystone PMPの共存設計が必要になる。同梱dispatcherだけで3003の代替にはならない。
将来OP-TEEが必要なhardware key APIを提供すると確認できた場合は、provider内部での利用を再評価する。

### OpenSBIのアクセス関数の再利用条件

`sbi_load/store` はMPRVによる呼び出し元のアドレス空間でアクセスする。
Eyrieのsatpが有効なまま、3003のPAをそのまま渡すと誤った変換になる。
Keystoneの既存copy_to_sm/copy_from_smも、名前やコメントだけでEPM所有権・PA対応を保証できない。

実装では、まず3003用のPAコピー契約を固定し、OpenSBIの既存trap処理を優先して使う。
必要なら局所的なBareアドレス空間への切替ラッパーを設けるが、satp、mstatus、mtvec等の
保存・復帰、割り込み条件、sfence.vma、PMPの作用を確認してから採用する。
適合性を確認せずVA用関数へPAを渡したり、新しい独自trap handlerを増設したりしない。
この適合性確認は後述P2の実装前提であり、異常PAを使う実QEMU試験で判定する。

## 4. 必須の鍵provider

現在のK3 sm_initはSM秘密鍵をロードせず、3003はadapterとSM helperの両方で無効化されている。
既存の開発用鍵コピー機能とB1 handoffの入口はあるが、通常の鍵供給は完成していない。
ゼロ初期化された `sm_private_key` のまま3003だけ有効にしない。

| provider | 初期実装方針 |
| --- | --- |
| unavailable（既定） | 3003はNOT_IMPLEMENTED。CREATE/RUNと3001は動作を継続 |
| qemu-dev（明示指定） | 保存された開発device rootとSM measurementからSM identityを導出し、既存keypair生成で鍵を作る。device rootを無断再生成しない |
| hardware（将来の接続先） | 実機の認証済み起動・保護された鍵供給からSM identityを得る。同じAPIを実装。未接続時は失敗を返す |

提案APIはchecked initialize/get状態であり、具体的な関数名は実装時に既存provider APIへ寄せる。
公開鍵は公開可能、秘密鍵はSM内だけで保持する。providerの失敗は3003で返す。

QEMU開発用device root seedはgitに含めず、権限0600・buildディレクトリ配下で管理する。
既存のdev-embedded-key経路を再利用・整備する。組み込んだ開発秘密鍵はfirmwareから取り出せるため、
`provider=qemu-dev` と表示し、Jupiter2実機用の秘密鍵・attestationの証明とは扱わない。
DTや9p共有へ秘密鍵を載せる渡し方は採用しない。

3001のcounter出力は秘密鍵の材料に使わない。Zkr/WTMの乱数だけでも、再起動後の同一identityや
永続鍵は保証されない。実機ではHUK/secure processor/boot provisioningの実際のAPIと
鍵の可読性・署名方法を確認する。非exportableなhardware keyしか使えない場合は、
既存HKDFが受け取るIKMをどう得るか、またはhardware KDFへどう委譲するかを別途確定する。

3003の署名は「そのSM公開鍵で検証できる」という意味であり、device endorsementや
SM証明書チェーンを3002なしに提供するものではない。
同時実装ではSM measurement変更時にSM identityとsealing keyを変更するpolicyとする。
同じdevice root・同じSM imageの再起動では再現する。実機providerの移行・鍵rotationも明示する。

## 5. SM/Eyrieで行う移植作業

### SM

- enclave contextを検査してから3003へdispatch。3002の拒否は維持する。
- 現在のeid、Running状態、measurement、EPM範囲をSMメタデータから取得する。
  呼び出し側からmeasurementやeidを指定させない。
- identifier長は現行Eyrieバッファに合わせ最大4096 bytes。0 bytesを許可し、NUL終端は要求しない。
- `[PA, PA+length)` のoverflowと、入力・出力が当該EPM内に完全に含まれることを確認する。
  UTM、他enclave、SM/OpenSBI領域、MMIOは拒否。出力は常に192 bytes。
- identifierをSM側へコピーしてから計算し、結果をSM内の一時structに作って最後にコピーする。
  入出力が重なっていてもidentifierのコピーが完了してから出力を書き込む。
- 既存の無制限VLA `info[MDSIZE + key_ident_size]` は置換する。
  長さを固定上限で制限し、8 KiB級のSMスタックを圧迫しない保護された作業領域を用いる。
  初期版は有界の共有作業領域＋専用lockを候補とし、encl_lockを長い署名計算中ずっと保持しない。
- HKDF/HMAC/署名後のkey、PRK、作業領域の消去を成功・失敗両方で実施する。
  OpenSBIの通常memsetだけで消去保証があるとは扱わず、必要なcompiler barrier等を追加する。
- 失敗はstatusで返す。VM停止やtrapの無限反復にしない。
  コピー途中の障害では部分書き込みがあり得るため、非0status時の出力は無効と定義する。

### Eyrie/SDK

- `get_sealing_key()` の既存APIを維持する。
- 出力capacityは最低192 bytes。小さければ拒否、大きくても192 bytesだけコピーし、後続領域は触らない。
- identifier上限、NULL/0長、VA overflow、全ページの読み書き可能性を確認する。
- copy_from_user/copy_to_userの戻り値を確認し、不正VAはEFAULTへ。
- 3003が失敗したときはkeyをユーザーへコピーしない。正のSM statusとruntimeの負errnoを明確に扱う。
- runtimeのコピー用作業バッファを両方消去する。不要な前回データを余分にコピーしない。
- 192 bytes、field offset、SM/SDK双方の定義が一致することをbuild時に検査する。

## 6. 実装順と成果物

| 段階 | 作業 | 完了条件 |
| --- | --- | --- |
| P1 | provider状態を独立させ、qemu-dev鍵生成・保存・公開鍵取得を整備 | 鍵未供給時は3003未対応のまま、既存試験がPASS。開発鍵はbuild間で安定 |
| P2 | EPM範囲・長さ検査、PAコピー、bounded workspaceを整備 | 実QEMUの無効PA/overflow/別領域入力でerrorになり、復帰・後続正常呼び出しが可能 |
| P3 | Keystone HKDF＋SHA3版署名に接続し3003を有効化 | 実enclaveから192 bytes取得、既知入力との一致、SDK互換verifierで署名検証成功 |
| P4 | Eyrie/SDK経路の引数・コピー・消去処理を完成 | U-mode API経由の正常・異常系、出力capacity検査がPASS |
| P5 | verifications/sealingとMakefile・起動設定を追加 | 複数enclave、再生成、再起動、provider未供給まで自動実行できる |
| P6 | 既存全verificationを再実行し記録 | SBI 3001・edgecall・PMP・lifecycleを含め回帰なし |

新設候補は `verifications/sealing/` のeapp/runner、専用test runtime、Makefile、
run-guest.sh、再起動matrix、README。SM側providerは既存spacemit_k3配下の入口を拡張し、
同じ役割の別provider frameworkを重複して増設しない。
root `make opensbi`、`make eyrie`、`make verifications` と対応installターゲットへ統合する。
開発用firmwareの指定は例として `--sealing-provider qemu-dev` を想定するが、
**このオプションは本計画時点では未実装**。通常firmwareと専用build領域・出力を分ける。

## 7. QEMU verification計画

すべて実QEMUゲストのOpenSBI/SMとEyrieを通す。ホストでSMのC関数を模擬実行しない。
ログに鍵そのものは出さず、比較・署名検証の成否、provider、firmware/test packageのhashを記録する。

| 試験 | 期待結果 |
| --- | --- |
| 同一enclave・同一identifierの反復 | keyと署名が一致 |
| STOP/OCALL/RESUMEの前後 | 同じkeyを取得でき、元データも保持 |
| 同じmeasurementでDESTROY/再CREATE | eid/PAが変わっても同じkey |
| 異なるバイナリmeasurement | 同じidentifierでも異なるkey |
| 同じenclaveで異なるbinary identifier | keyが異なる。途中のNULも識別に含まれる |
| identifier長0、1、7、8、4095、4096 | 仕様どおり成功、境界外書き込みなし |
| 4097、overflow、不正VA/PA、EPM境界跨ぎ | error。VM継続、次の正常要求成功 |
| output capacity 0/191 | runtimeが拒否 |
| output capacity 192/193以上 | 192 bytesだけ出力。前後canaryと未使用末尾を保持 |
| 同じ入力と出力領域の重複 | SMでidentifierを先にコピーし、定義どおり完了 |
| UTM・他EPM・SM・MMIO宛て | SMで拒否。LinuxやSMのメモリを破損しない |
| Linux hostから3003 | PROHIBITED、a1=0 |
| qemu-dev known-answer | 独立計算のHKDF-SHA3-512期待値と一致、Keystone互換署名のverify成功 |
| 署名またはkeyを1 bit変更 | verify失敗 |
| 同じdevice root・SM imageでQEMU再起動 | 同じenclave measurement/identifierなら同じkey |
| 開発鍵を明示変更して再build | keyが変化、旧データが同じkeyで復元できるとは扱わない |
| providerなし/初期化失敗 | NOT_IMPLEMENTEDまたは明示error。ゼロ鍵やcounter由来鍵で成功させない |
| 連続要求・複数hart | 作業領域混線なし、boundedな復帰、既存timing/lifecycleと共存 |

known-answerの期待値生成にホストPython等の独立暗号実装を使うことは可能だが、
これは期待値の生成であり、SMの代用ではない。署名の期待値にはSHA3版の差異を反映する。
無効PA試験に専用Eyrieが必要ならverification専用buildに限定し、production APIへ
ホスト制御の秘密鍵取得経路を追加しない。

## 8. 現時点の完了範囲

ソース調査と実装・verification計画を完了。3003を実装した／実機の鍵が利用できる／
新しい試験がPASSした、とはまだ報告しない。
既存全verificationのPASSはユーザー報告として記録する。
次の作業はP1から開始し、鍵providerのない通常起動を維持したままQEMU専用経路を完成させる。
