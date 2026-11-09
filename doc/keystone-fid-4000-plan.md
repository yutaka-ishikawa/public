# FID 4000（CALL_PLUGIN / multimem）実装計画

作成日：2026-11-09。状態：P0〜P2の登録・照会・解放を実装済み。P3（paging）とP4（hash/crypto）は未統合。
実装詳細と試験は[ソース検証](../verifications/sbi-4000/source-review.md)、
[結果](../verifications/sbi-4000/results.md)を参照。

## 目的と範囲

K3のLinux → driver/SDK → OpenSBI → Keystone SM → Eyrieの実経路で、
追加メモリ領域を安全に登録・照会・解放できるようにする。
FIDは十進4000。a0=plugin ID、a1=call ID、a2/a3=引数という既存ABIを維持する。
multimemのplugin ID=1、call ID=1はサイズ、2は物理開始アドレスの照会。
出力先ポインタはa2で渡す。成功・失敗は既存Keystone ABIに合わせる。

FID 4000の完成とページングの完成を分ける。4000は領域照会の入口であり、
それ自体でswap、暗号化、動的EPM拡張を実行するものではない。
本計画の初版はCREATE時の固定登録とし、RUN中の領域追加・resizeは対象外とする。

## 実装前に調査した既存実装

- `keystone/sm/src/sm-sbi-opensbi.c`：4000からSMへのdispatcherがある。
- `keystone/sm/src/sm-sbi.c`：現在のenclave IDをpluginへ渡す。
- `keystone/sm/src/plugins/plugins.c`：multimemはcompile-time有効化。
- `keystone/sm/src/plugins/multimem.c`：`REGION_OTHER`を検索してサイズ・物理アドレスをコピーする。
  未登録時の戻り値、未知call IDの成功扱い、出力ポインタの扱いは統合時に是正する。
- `keystone/sm/src/platform/sifive/fu540/fu540_internal.c`：scratchpadへEPMを移した際に
  元EPMを`REGION_OTHER`へ変更する構造。ただし`use_scratch`は現状0であり、
  K3の通常CREATEに使える汎用登録経路ではない。
- `keystone/runtime/call/sbi.c`、`mm/paging.c`、`mm/page_swap.c`：
  照会、マッピング、ページ退避の既存コードを再利用する。
  開始位置とサイズを別々に丸めた場合の実領域超過も確認する。

OpenSBIの既存ecall群にはKeystoneのmultimem登録・照会に相当する実装はない。
`include/sbi/sbi_heap.h`のheapはM-mode内部用であり、Linuxの大容量backing領域の
確保元としては使わない。`sbi_domain.h`の領域管理はOpenSBI domainの保護用であり、
enclaveごとの所有権・PMP切替の代替にはしない。
既存OpenSBIのecall dispatch、lock、hart/IPI機構を優先して再利用し、
Keystoneの既存PMP更新経路と組み合わせる。第二のIPI/PMP管理系は作らない。

## メモリとABIの設計方針

backing storeはhostが参照・改変できる非信頼メモリとする。EPMとは別に確保し、
平文の秘密を保存できる領域とは扱わない。hashのみでは秘密保持を提供しない。
本番の秘密データを退避するには、後続の暗号化・完全性・replay対策の検証を必要とする。

Linux driverが領域を確保・保持し、SMは登録範囲と所有enclaveを管理する。
SDK利用者から任意の物理アドレスを登録させない。
初版は物理連続領域とし、既存CMA確保処理の再利用可否を確認する。
確保限界はCMA容量・断片化に依存し、任意サイズの成功は保証しない。

CREATE情報にversion/sizeを持つ拡張を設計し、旧SDK・旧driver・旧SMの組合せを
黙って解釈しない。旧CREATEの無拡張動作は維持する。
必要な新規FID/IOCTL番号は既存定義との衝突を確認して確定する。
登録はRUN前に完了し、サイズ0、加算overflow、不整列、EPM/UTM/SMとの重複、
別enclaveへの重複割当を拒否する。既存UTMと同様の非信頼領域として扱えるか、
PMP優先順位・TOR/NAPOT・全hart反映を確認して確定する。

照会は実行中のenclaveだけに許可し、hostからの直接呼出しは拒否する。
未知plugin/call、未登録、無効出力ポインタを明示的なエラーにする。
出力は既存の検査付きコピー経路を再利用し、出力可能なenclave領域に限定する。
領域はSTOP/RESUMEを越えて保持し、DESTROY成功後にdriverが解放する。
CREATE失敗時は逆順にrollbackする。DESTROY/PMP解除失敗時は再利用せず、
保持状態と再試行経路を明確にする。実行中の解放を禁止する。

## 実装順序と完了条件

### P0：ABIと保護条件を確定

CREATE/IOCTL、SDK、SMの構造体と戻り値、PMP・copy helper、Eyrieの
backing領域境界を追跡する。ABI表・所有権の状態遷移・失敗時rollback表を作る。
上記の疑義を確認した際は`keystone-bug-report.md`へ登録する。
完了条件は旧構成との互換条件、登録API、解放責任をコード変更前に確定すること。

### P1：SM dispatcherと照会の整備

明示的なmultimem build profileを追加する。enclave文脈、plugin/call ID、
出力範囲、未登録を検査する。既存の1/2の照会ABIは維持する。
完了条件は未登録・不正呼出しがguestで所定のエラーになり、hostが継続動作すること。

### P2：driver・SDK・CREATE登録・解放

別領域の確保、CREATE拡張、SM検査、所有権記録を実装する。
失敗rollback、DESTROY、プロセス異常終了、二重解放防止までまとめて実装する。
完了条件は実enclaveから登録値を照会でき、複数enclaveが混線せず、
繰り返し生成・破棄でdriverの割当数とCMA使用量が回復すること。

### P3：Eyrieとの接続と基本paging

まずPAGINGを無効のまま照会を検証する。次に専用profileでのみPAGINGを有効にし、
検証データによるevict/swap-inを試す。丸め後の範囲、空き領域枯渇、
PMPアクセス許可、ページテーブル領域・metadataの必要量を確認する。
完了条件は利用可能EPMを超えるデータの往復一致と、枯渇時の明示的な終了・回収。
この段階で本番向け暗号化pagingが完成したとはしない。

### P4：PAGE_HASH・PAGE_CRYPTO

既存Merkle/AES実装とOpenSBIで利用可能な暗号機能を再調査し、重複実装を避ける。
SBI 3001の乱数失敗、鍵の生成・破棄、nonce/counter再利用、改変・replayを検査する。
完了条件は秘密保持と完全性の保証範囲を文書化し、改変・再利用の拒否を実証すること。
N3に対応する後続工程として扱い、FID 4000単体の完了条件と分ける。

## 実QEMUでのverification

`verifications/sbi-4000/`へenclave/runtime/hostのCコード、Makefile、
実行スクリプト、READMEを置く。模擬SM試験は追加しない。
全体の`make`、guest成果物のinstall、`run-all.sh`とQEMU自動実行へ組み込む。
PASS/FAILにはFID 4000、plugin/call、試験名、profileを表示する。

| 試験 | 確認すること |
|---|---|
| plugin無効・未知ID・host直接呼出し | 拒否とhostの継続動作 |
| 未登録・不正出力先 | 明示的エラー、EPM外への書込みなし |
| 正常照会 | 登録したサイズ・PAとの一致、領域端の一致 |
| 不正登録 | overflow・重複・不整列・所有者違いを拒否 |
| STOP/RESUME・timer | 登録値維持、Sstc on/off双方で継続 |
| 複数enclave・hart移動 | 他enclaveの登録値・メモリを利用できない |
| 失敗・終了・反復 | CREATE途中失敗、DESTROY、異常終了後に回収 |
| paging専用profile | EPMを超えるデータの退避・復元一致と枯渇処理 |
| hash/crypto専用profile | 改変・replay拒否、乱数失敗時に安全に終了 |

全既存verificationも再実行する。各結果へcommit、firmware/kernel/module/packageの
hash、profile、QEMUコマンドを保存する。build成功とguest PASSを区別する。
QEMUの結果でJupiter2実機のPMP・CMA・cache整合性まで確認済みとはしない。

## 完了の記録

P0〜P2と照会・解放のguest試験が完了した時点でFID 4000 / multimem登録を完了とする。
P3とP4は別に進捗を記録する。各工程の実装commitと試験証跡を本書および
[移植計画](keystone-porting-plan.md)へ追記する。P0〜P2の実装commit：[2f87a9d57](https://github.com/yutaka-ishikawa/public/commit/2f87a9d5736d7653c512e050e944f17b787098e5)。

## P0〜P2で確定した仕様

- 新規K3拡張CREATEはFID 2006、構造体version=1。旧FID 2001は変更しない。
- driverのBACKING_INIT IOCTLはnr 0x08。実長とPAを返す。SDKに任意PA指定APIを追加しない。
- `REGION_BACKING`を新設し、FU540固有の`REGION_OTHER`転用とは区別する。
- 4000の既存の仮想出力ポインタABIを維持し、private EPMの書込み可能範囲だけを許可する。
- host-visibleなbackingのPMPはhost復帰時にOFFへ戻し、DESTROYでも全hartの解除を確認する。
- K3は既定で有効。`KEYSTONE_MULTIMEM=0`と別firmware出力で無効profileも試験する。

この完了はbackingへのページ退避・復元、暗号化、実機動作を意味しない。
