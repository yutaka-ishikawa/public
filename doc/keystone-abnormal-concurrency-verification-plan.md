# Keystone実装済み機能の異常系・競合検証コード作成計画

作成：2026-11-09（日本時間）。**計画全体は未完了。Cの中間実装と実行結果は[robustness](../verifications/robustness/README.md)を参照。**
対象ソースの基準commitは`6ac052bc1`。以後の実装では対象commitと成果物hashを更新する。
対象はK3 QEMU上の実LinuxのC検証client → driver → OpenSBI → SM → Eyrie/enclave。
既存SDK経由の回帰結果は基礎資料として使うが、新規clientはC++ SDK classを直接呼ばない。
SM/PMP/CSR/暗号処理を置き換える模擬試験や、製品C関数をPC上で直接呼ぶ試験は追加しない。

## 1. 対象機能と既存試験の位置付け

| 機能 | 識別 | 今回拡充する責務 |
| --- | --- | --- |
| CREATE | FID 2001 | 引数・layout・測定・EPM/UTM確保と保護・rollback |
| DESTROY | FID 2002 | 状態遷移、全hart PMP解除、消去、回収 |
| RUN | FID 2003 | FRESHからの単一実行、PMP/context/timer切替 |
| RESUME | FID 2005 | STOPPEDからの再開、二重再開拒否、context保存 |
| 拡張CREATE | FID 2006（本リポジトリ独自） | version/size・backing登録・所有権・rollback |
| RANDOM | FID 3001 | backend選択、WAIT/DEAD/timeout、エラー伝達、共有provider排他 |
| ATTEST | FID 3002 | 測定値・nonce・report生成・署名・入力出力の検査 |
| SEALING | FID 3003 | 識別子・測定値による鍵導出、providerとworkspaceの扱い |
| STOP | FID 3004 | 有効/無効request、timer/OCALLによる中断 |
| EXIT | FID 3006 | 終了値、host復帰、再開不可、終了後の回収 |
| CALL_PLUGIN / multimem | FID 4000、plugin 1、call 1/2 | 文脈・ID・出力先・登録サイズ/PAの照会 |
| edgecall / UTM | SDK/Eyrie、内部で3004→2005 | offset/length・データ・callback・共有領域の寿命 |
| timer / IPI / shutdown | OpenSBI標準SBI・Sstc/MTIMER・SRST/HSM経路 | 上記FIDを支えるhart間保護・割込み・終了 |
| Linux driver | ioctl/mmap/open/close、EPM/UTM/backing、CMA | file所有権・mutex・in-flight操作の寿命・解放 |

2004、3005等は未定義番号の拒否試験であり、新たな実装済み機能には数えない。
標準SBIのTIME/IPI/SRST等はKeystone EID `0x08424b45`のFIDとは別に識別する。

既存の`sbi-errors`、`edgecall`、`random`、`crypto`、`sbi-3004`、`sbi-4000`を基礎にする。
正規report検証、代表的な不正引数、RNGの7ケース、timer取消し後の中断、反復回収等は
実行記録があるため再利用する。**以下の試験が既に全て存在・PASSしているとはしない。**
特に「2 enclaveを保持して交互にRUN」「CPU affinity切替」と「別hartの同時実行」は区別する。
既存結果は各evidence、[移植計画](keystone-porting-plan.md)、
[4000結果](../verifications/sbi-4000/results.md)に紐付け、READMEの古い未確認表記だけで判定しない。

## 2. カテゴリと試験ID

| カテゴリ | ID形式 | 分類する対象 | 作るコード |
| --- | --- | --- | --- |
| 単一機能の異常 | A-FID-番号 | 例：A-2001-01はCREATE自身の異常 | guest host制御、固定raw-SBI probe、専用eapp/runtime |
| 同じ機能同士の競合 | R-FID-番号 | 例：R-2003-01はRUN対RUN | barrier付き複数workerと結果照合 |
| 複数機能間の競合 | X-番号 | 対象FID集合を必須記載 | lifecycle/crypto/UTM/timer等を組合せたscenario |
| 実障害の注入 | F-層-番号 | PMP、IPI、allocation、copy、provider等 | 専用QEMU/firmware/driverのone-shot fault制御 |
| 長時間・資源再利用 | S-番号 | 上記組合せの反復・枯渇・回復 | seed付きscenario scheduler、資源台帳 |

Fは別の機能名ではなく横断属性。例えばX-01にF-PMP-02を加えた場合、
結果には「2003 RUN対2002 DESTROY、PMP復旧失敗」を同時に記録する。
各caseは「対象機能」「参加者」「同じ/別enclave」「同じ/別file」「hart配置」「注入点」を持つ。
単なるファイル分類だけで対象を推測させない。

## 3. 単一機能の異常系コード

以下は代表case群。P0工程で入力ごとの具体的なcase番号と期待status/errnoを確定する。
既存caseを再利用するものには既存caseの参照を付け、追加分だけ実装する。

| case群 | 対象 | 入力・状態・故障 | 必須の確認 |
| --- | --- | --- | --- |
| A-2001-* | CREATE | NULL/不正copy元、0/overflow/不整列、EPM/UTM/SM重複、順序不正、測定途中失敗 | 所定status、拒否段階、未公開EID、rollback、次の正常CREATE成功 |
| A-2006-* | 拡張CREATE | version/bytes/予約引数、backingの不正範囲・他所有者との重複、割当後の登録失敗 | 旧2001動作維持、追加領域の未登録/回収、二重解放なし |
| A-2002-* | DESTROY | 無効ID、RUNNING、二重破棄、PMP解除途中失敗 | 状態ごとの拒否、成功時だけ再利用、失敗時quarantine |
| A-2003-* | RUN | 無効ID、非FRESH、PMP/context/timer準備失敗 | host frame/timerを壊さない、失敗後の状態と再試行可否 |
| A-2005-* | RESUME | 無効ID、FRESH/RUNNING/終了済み、保存context不整合の専用注入 | 所定拒否、二重実行なし、既存STOPPED contextの保持 |
| A-3001-* | RANDOM | host呼出し、Zkr不在/WAIT/DEAD/連続待機、WTM不在/not-ready/zero/アクセス失敗 | bounded poll、所定statusと出力値、正常backendの回復。乱数値の重複だけを不具合扱いしない |
| A-3002-* | ATTEST | host呼出し、長さ0/1/1024/1025/overflow、不正PA/UTM/copy、provider unavailable/片側ready/署名準備失敗 | reportのstatus・正規長・nonce/pins・canary、失敗を成功として送らない |
| A-3003-* | SEALING | host呼出し、識別子の境界/不正範囲、provider unavailable/初期化失敗、copy失敗 | 所定拒否、識別子/測定の分離、workspace消去。鍵はenclave外へ出さない |
| A-3004-* | STOP | 不正request、host文脈、timer取消し、連続yield | 拒否前にcontextを壊さない、正常RESUME、SM sliceとhost timer復旧 |
| A-3006-* | EXIT | host文脈、0/非0/最大幅の終了値、EXIT後のRUN/RESUME | 終了値の幅、host復帰、再開不可、DESTROYで回収 |
| A-4000-* | plugin/multimem | plugin無効/未知ID、予約引数、未登録、無効VA、UTM、read-only、非canonical、異常page table | 所定status、EPM外へ書かない、host/caller所有者違いを拒否 |
| A-UTM-* | edgecall | offset+length overflow、境界外、return長の不一致、callback失敗、共有データ途中変更 | 範囲検査とenclave側再検証、別enclaveのデータ混入なし |
| A-DRV-* | driver | 他fileのUID、二重初期化、copy_to/from_user失敗、mmap境界、alloc失敗後close、dup/fork/exit | errnoとSBI statusを区別、所有権保持、IDRの解放済み参照なし |

入力チェックでの早期拒否と、途中copy/保護更新の失敗を別caseにする。
「失敗なら全出力が必ず未変更」は一律の期待値にしない。入口拒否で未変更を保証するcaseと、
途中copyで部分変更があり得るcaseを契約に従い分ける。後者でも成功を返さず、
別要求/別enclaveの秘密が混入せず、範囲外canaryが保たれることを検査する。

## 4. 同一機能同士の競合コード

| case群 | 機能対 | 参加者と同時実行点 | 確認する不変条件 |
| --- | --- | --- | --- |
| R-2001/2006-* | CREATE対CREATE（同FIDごと） | 別file/別enclave、別hartでEID/PMP/メモリ資源を要求 | 二重所有なし、確保競合の敗者をrollback、上限後に回復 |
| R-2002-* | DESTROY対DESTROY | 同一enclaveへ2要求 | 破棄は高々1回成功、二重消去/解放なし。driver直列化とSM直接試験を分ける |
| R-2003-* | RUN対RUN | 同じFRESH enclaveへ別hartから要求 | 単一runnerのみ入る。両方の成功/同時実行を許さない |
| R-2005-* | RESUME対RESUME | 同じSTOPPED enclaveへ要求 | 保存contextを1回だけ使用、敗者は状態に対応した拒否 |
| R-3001-* | RANDOM対RANDOM | 別enclave/別hart、WTM共有FIFOやrng_lockで競合 | status/wordの取り違えなし、pollとlockの進行性。統計的乱数品質の合格とは別 |
| R-3002-* | ATTEST対ATTEST | 別enclaveから異なるnonce、security_lock共有 | 各reportが自分のmeasurement/nonceで検証成功、workspace混線なし |
| R-3003-* | SEALING対SEALING | 同じ/異なるmeasurementと識別子を別enclaveから要求 | 現導出契約どおりのcommitment。同measurement+同IDの鍵一致は正常で、EID違いだけで別鍵を要求しない |
| R-3004-* | STOP対STOP | 別enclaveでtimer/OCALL yieldを同時発生 | host frame・停止理由・再開先が混線しない |
| R-3006-* | EXIT対EXIT | 別enclaveが異なる終了値で同時終了 | 値/IDの混線なし、各host timer復旧、後続回収可能 |
| R-4000-* | QUERY対QUERY | 別enclaveがSIZE/ADDRを反復照会 | 自分の登録値のみ返り、同時DESTROYなしでも情報が混線しない |

同一enclaveのSTOP/EXIT/cryptoを同時に2スレッドから呼ぶ「正常系」は作らない。
現SMはMAX_ENCL_THREADS=1。同一enclaveの二重進入を拒否する試験と、
別enclaveから同じFIDを並行呼出しする試験を混同しない。

## 5. 複数機能間の競合マトリクス

A/Bは独立enclave。`same`は同一enclave。対応するF注入を後から重ねる。
各組合せは、同時性を作る入口と、合法な状態順序を先に定義してから実装する。

| ID | 機能同士（FIDまたは層） | 参加者・競合点 | 合否の中心 | 優先 |
| --- | --- | --- | --- | --- |
| X-01 | RUN 2003 × DESTROY 2002 | same、RUNの状態予約/PMP切替前後 | RUNNING破棄を拒否、解放済みEPMを実行しない | P1 |
| X-02 | RESUME 2005 × DESTROY 2002 | same、STOPPEDからの競争 | RUNNING化とDESTROYING化のどちらか一方、再開contextの解放なし | P1 |
| X-03 | RUN 2003 × RESUME 2005 | same、FRESH/STOPPEDを区別 | 状態に適合した要求だけ受理、二重runnerなし | P1 |
| X-04 | STOP 3004 × RESUME 2005 × DESTROY 2002 | same、停止通知とhostの再開/破棄 | 停止の公開順序、適切な勝者/敗者、破棄後の再開なし | P1 |
| X-05 | EXIT 3006 × DESTROY 2002 / RESUME 2005 | same、host復帰前後 | EXIT後は実行再開不可、終了値保持、回収は一度 | P1 |
| X-06 | CREATE 2001 × CREATE_V2 2006 × DESTROY 2002 | A/B、EID/PMP枯渇と再利用 | 世代・所有者混線なし、EID再利用時もin-flight参照を壊さない | P1 |
| X-07 | CREATE_V2 2006 × QUERY 4000 × DESTROY 2002 | Aの照会中にBを登録/破棄。same破棄拒否も別case | descriptor/PA/sizeの取り違え、backingの早期解放なし | P1 |
| X-08 | RUN/RESUME 2003/2005 × PMP/IPIによるCREATE/DESTROY | A実行中、Bの全hart保護更新 | Aの許可を壊さずBを保護、ACKとPMP stateが整合 | P1 |
| X-09 | ATTEST 3002 × SEALING 3003 | A/B、同じsecurity_lock/workspace | reportと鍵処理の交差汚染なし、lock解放/消去、進行性 | P2 |
| X-10 | ATTEST/SEALING 3002/3003 × RUN/RESUME/DESTROY | Aでcrypto、Bの生成/破棄/切替。same破棄は拒否試験 | measurement snapshot・EPM寿命・共有workspaceの整合 | P2 |
| X-11 | RANDOM 3001 × ATTEST/SEALING 3002/3003 | A/B、RNG stall中とcrypto同時要求 | 不要なlock依存・無限待機なし。現在のcryptoがRNGを内部利用すると決めつけない | P2 |
| X-12 | RANDOM 3001 × STOP/RESUME 3004/2005 | A/B、RNG bounded pollと別enclaveのtimer | 他hartの進行、戻り値維持、再開後のエラー伝達 | P2 |
| X-13 | QUERY 4000 × ATTEST/SEALING/RANDOM 3002/3003/3001 | A/B、照会とserviceを併走 | 自分の出力領域・PA/size・nonce/commitmentの分離 | P3 |
| X-14 | edgecall/UTM × STOP/RESUME 3004/2005 × host応答遅延 | same、異なるsequenceのデータ往復 | 順序・長さ・canary・返信の対応、二重配送なし | P1 |
| X-15 | Linux Sstc / 標準TIME × SM M_TIMER × 3004/2005 | 各hartでLinux待機、A/Bでtimer設定/取消しとyield | host timerの保存復旧、SM slice独立、deadlineの取り違えなし | P1 |
| X-16 | driver close/dup/SIGKILL × RUN/RESUME/DESTROY | 同一fileのin-flight ioctlと最後の参照解放 | Linux file参照寿命、mutex、IDR、module参照を保持し早期freeしない | P1 |
| X-17 | mmap/UTM初期化/backing初期化 × FINALIZE 2001/2006 | same fileと他file | 初期化の直列化、他所有者拒否、登録後の増設/再初期化拒否 | P1 |
| X-18 | report/keyの不正copy × 別enclaveの正常3002/3003 | Aが異常要求、Bが正常要求 | Aの失敗がBを汚染せず、workspaceの残骸を再利用しない | P2 |
| X-19 | 標準SRST/HSM shutdown × RUN/RESUME/crypto/照会 | QEMU全体、guest shutdown時にworker稼働 | 方針どおり終了し、不正hart state/無期限待機を避ける | P3 |
| X-20 | CREATE/DESTROY/backing割当 × guest Linux負荷 | A/Bと通常process、CMA/buddy/PMP圧迫 | OOM等の環境事象を区別し、失敗後に所有資源を再利用 | P2 |

**この表の全てを単純な2スレッドテストで再現できるわけではない。**
同一fileのdriverはioctl全体をmutexで保護する。RUN ioctlもその対象なので、
同じfdへのDESTROYを並列発行しただけならSMへ到達する前に待つ。
その結果は「driver直列化確認」であり、X-01のSM内部競合を確認したとはしない。

SM内部のsame-enclave競合には専用kernel probeを設計する。probeはCAP_SYS_RAWIO限定、
固定case列挙、所有者と寿命を保持したテストobjectを使い、検証用に取得したSM IDで実ecallする。
通常driverの保護を回避する製品APIや、任意のCSR/PMP操作APIは追加しない。
probeで回避した層と未回避の層をmanifestに記録する。

## 6. 障害注入の設計

| ID | 実際に失敗させる段階 | 対象case | 観測すること |
| --- | --- | --- | --- |
| F-ALLOC-* | EPM/UTM/backing/metadata確保の前後、N回目の確保 | A-2001/2006/DRV、X-06/16/17/20 | 割当済み資源の正確なrollback、一度だけfree |
| F-COPY-* | CREATE引数copy、enclave入出力copy、Linux user copy | A-2001/2006/3002/3003/4000、X-18 | 途中失敗のstatus、canary、部分変更の契約、次要求の独立性 |
| F-PMP-* | private保護、backing RW/OFF、host復元、全hart解除 | A-2002/2003/2005/2006、X-01/02/07/08 | 実PMP状態、host/enclave frame、timer、再利用の可否 |
| F-IPI-* | 送信前失敗、remote適用前遅延、適用後ACK欠落/遅延、対象hart不一致 | X-06/08、DESTROY | 送信・受信・適用・ACKを区別、未完了のままdescriptor/freeを許さない |
| F-RNG-* | Zkr WAIT/DEAD、WTM valid遅延/欠落/zero/MMIO失敗 | A-3001、R-3001、X-11/12 | 実backendのpoll上限とerror、lockを解放して進行可能か |
| F-CRYPTO-* | provider unavailable/片側ready/初期化失敗、特定のservice段階での失敗 | A-3002/3003、X-09/10/18 | 許可と失敗伝達、workspace消去、次要求の正当性 |

注入点はP0でソース位置へ対応付ける。実PMP処理やIPI transport、MMIO、allocatorの
境界にone-shotの失敗/遅延を入れ、成功したことにする偽実装や偽CSRには置き換えない。
不足するZkr状態は既存QEMU CPU実装への検証専用制御、WTMは既存deviceのfault機構を拡張する。
標準QEMU/通常firmware/通常driverには無効で、専用profile・別成果物に分離する。

制御にはcase ID、target hart/enclave、stage、発火回数、上限時間を持たせる。
「armed」と実際に「fired」を区別し、未発火はPASSにしない。boot時失敗はDT等の
起動前設定、実行途中は専用制御経路を使う。秘密鍵やメモリ全体をdebug interfaceで公開しない。

## 7. 競合を作る方法と合否判定

### 7.1 制御と観測

- coordinatorが独立Linux worker processをforkし、pipe/eventfd/shared controlで準備完了を待つ。
  別hartにaffinityを固定して同時解放する。SDK edgecallの共有領域設定はprocess-globalなので、
  複数enclaveのSDKを同じprocessのthreadで雑に併走させてharness自身の混線を作らない。
- 同一fdのmutex試験は共有fdを持つworkerで行い、別file試験は各workerがopenする。
  same-enclaveのraw-SBI試験は前節の専用probeで別に実装する。
- 開始barrierだけではSM内部の重なりを証明しない。検証用の小さいper-hart event ringで
  entry/state-reserve/lock-attempt/lock-acquire/PMP-apply/ACK/returnを記録する。
  console printfの順序を同期や同時性の証拠にしない。event ringのoverflowも検出し、
  必須eventが欠けた結果を成功扱いしない。
- deterministic gateは対象処理の入口/安全な境界に置く。SM spinlock保持中に、同じlockを
  必要とする別workerやLinuxからの解除を無期限に待つgateは作らない。
  有限遅延と外部watchdogを併用し、harnessが作ったdeadlockを製品不具合と誤認しない。
- 同時開始後にlockで直列化されたなら、到達と待機・完了を確認して「lock競合を検証」と記録する。
  2サービスのcritical sectionが並行することを要求しない。

### 7.2 oracle（正しさの判定）

P0でSMの正のstatus、標準SBIの負のerror、Linux errno、enclave終了値を別欄に定義する。
単なる「非0ならOK」にはしない。競争には合法な直列順序を列挙し、勝者/敗者の組を検査する。
例：同一FRESHへのRUN対RUNは成功が高々1、敗者はNOT_FRESH等の契約に対応する。
RUN対DESTROYはRUNNING破棄拒否または破棄先行後のRUN拒否を、観測した状態順序と照合する。

SMのslot IDやdriver UIDは再利用され得る。古い数値IDが将来も必ず無効という前提は置かない。
test側でallocation generationを付け、in-flight参照・所有者・物理範囲の取り違えを検査する。
強い世代付きhandleが製品ABIとして必要かは、現契約と別にP0で判断する。

データはworker/enclave/generation/sequenceごとのpatternとcanaryで追跡する。
ATTESTは独立nonceとpinsで署名検証、SEALINGはenclave内で期待commitmentを比較する。
鍵そのものをOCALL/ログ/ダンプに出さない。RNG出力の一意性や統計だけでentropy品質を保証しない。
早期拒否、途中失敗、成功を混ぜず、それぞれの出力・状態・回収契約を照合する。

SMのM-mode service内ではtimerが任意の位置で処理を中断すると仮定しない。
service実行時間・lock待機・S-mode復帰後の中断を分け、boundedな処理と他hartの進行を観測する。

PMP隔離は、既存edgecallのtrap対応probeを拡張して並行時にも確認する。
hostや別enclaveから他所有者のEPMへのload/storeを実際に試み、許可されないことを検査する。
VAを意図したPAへ正しくmapしたうえでaccess fault等の原因を照合し、単なる未mappingの
page faultをPMP成功の証拠にしない。probeの例外は捕捉してLinuxをpanicさせない。
対照としてhost-visibleなUTM/backingは設計どおり参照でき、host復帰後にbackingの
NO_PERMが残っていないことも確認する。検査中のpage参照を保持し、free済みPAを
無差別に読むharnessにはしない。秘密でなく専用の既知patternだけを使用する。

### 7.3 回収とfail-stop

資源台帳はalloc/freeの件数・bytes、EID/PMP descriptor数、driver IDR/file参照、
未完了IPI/ACK、timerのowner/deadline、quarantine対象を観測する。SDKのhost資源問題
KSB-035は別に追跡し、backing_bytesだけで全資源の解放を主張しない。
CmaFreeはLinux他処理でも変動するため補助指標とし、厳密な差分だけを合否にしない。

成功/可逆失敗後は正常enclaveを再作成して実データ往復・終了・破棄まで確認する。
保護解除不能時は無理な回収を要求せず、対象ページが再利用されないことを確認する。
現在コードには保護状態不明時のsbi_hart_hangやDESTROYING保持があるため、
期待する動作をP0でfail-stopと回復可能エラーに分類する。

**timeoutだけではfail-stopのPASSではない。** 注入発火、最後の到達段階、S-modeへ
戻っていないこと、未回収/再利用禁止の観測をQEMU monitor/GDB等で確認する。
他hartのheartbeatが継続できるかも記録する。観測不能ならINCONCLUSIVE、
意図した注入のない停止や誤った再利用はFAIL。fail-stop caseは別guestで実行して終了させる。

### 7.4 有限stressのcase

| ID | 対象機能の組合せ | 実行と判定 |
| --- | --- | --- |
| S-01 | 2001/2006→2003→3004→2005→3006→2002 | 複数worker・大小領域を有限回反復。各周期に資源台帳と正常再作成を照合 |
| S-02 | 3001/3002/3003/4000 × 2003/3004/2005/3006 | A/B/Cで別serviceを併走。nonce/commitment/sequenceの所有者と各workerの進行回数を検査 |
| S-03 | S-01/02 × F-ALLOC/PMP/IPI等 | 最初は1故障だけ発火させ、注入解除後の回復または隔離を確認。複合故障は単発caseの完了後 |
| S-04 | C clientのinit/destroy × driver open/close | 同一processでC client自身のFD/資源とdriverの回収を反復観測する。C++ SDK固有のKSB-035の直接検証には数えない |

SM/driverを測るS-01等はraw driver workerや短命workerと、C clientの長寿命workerを区別する。
反復数・最大資源量・wall-clock上限を指定し、無期限の負荷コードにはしない。
既存C++ SDKの資源問題KSB-035をC clientの成功で解消扱いしない。
SDK固有の反復init/destroy検証には既存SDKを呼ぶ経路が必要で、C-onlyの新規clientとは別範囲に残す。
既存C++実行ファイルをC coordinatorから回帰実行することは可能だが、それだけでSDK内部の
長寿命反復や新しい競合を網羅したとはしない。既知不具合が観測されたら既知FAILとして記録する。

## 8. 作成するコードと配置案

以下は当初の配置案。現在の実装済みファイル・利用可能なCLI・未了部分は[中間実装README](../verifications/robustness/README.md)に記載する。全ファイルを作成済みという意味ではない。

```text
verifications/robustness/
  README.md                  対象、実行、必要profile、既存caseとの対応
  Makefile                   guest/kernel-module、C用SDK資産/runtimeを再利用
  cases.yaml                 A/R/X/F/Sのcase台帳と期待status・資源条件
  coordinator.c              process起動、barrier、affinity、deadline、結果照合
  workers/lifecycle.c         driver経由のCREATE/RUN/RESUME/DESTROY
  workers/services.c         別enclaveのRNG/crypto/multimem/edgecall
  common/client.c            ioctl、file配置、mmap、RUN/RESUME、明示cleanup
  common/report_check.c      共通report型と既存C暗号primitiveで検査
  enclave/workload.c         pattern、nonce、commitment、yield、bounded computation
  runtime/probes.c           raw引数・不正文脈・page table境界の専用hook
  kernel/probe.c             制御されたSM直接競合、所有権/寿命観測
  kernel/abi.h               version付きの固定case制御とevent取得
  run-guest.sh               guest前提、個別/有限stress/破壊試験の選択
  run-qemu.py                disposable overlay、profiles、host watchdog、manifest
  source-review.md           各caseと製品関数・lock・状態遷移の対応
  evidence/                  入力hash、結果、秘匿情報を含まないevent/ログ
```

one-shot hookは必要最小限をSM/driver/QEMUに置き、case台帳から位置を参照する。
既存testプログラムを丸ごと複製せず、4000等の正常packageや共通extractorを再利用する。
新規のcoordinator、worker、client、report検査、enclave/runtime hook、kernel moduleは全てC。
Cは既存K&R・4-column/tab8・単文でもbracesの規約を踏襲する。

通常の有限・回復可能caseは将来`make -C verifications guest kernel-modules`と
`run-all.sh`へ組み込む。fail-stop、長時間stress、fault-profile必須caseは別実行とし、
標準suiteが勝手にguestを停止する構成にはしない。buildとinstallを分け、
通常成果物へfault profileを上書きしない。

### 8.1 Cで実装する際の境界

初案の.cppは既存host SDKのEnclave/Params APIをそのまま使う便宜上の選択であり、
process起動、barrier、affinity、deadline、競合制御にC++が必要だったわけではない。
ユーザー指定に従い新規検証コードはCとし、C++ wrapperを隠して追加する方式にはしない。
PythonはQEMU起動・ログ集計、shellはbuild/guest操作に使い、機能を検証するclient本体はCとする。

- coordinatorはfork/waitpid、pipe/eventfd、sched_setaffinity、clock_gettime等のC APIを使う。
- lifecycle clientはshared/keystone_user.h、sm_call.hの既存ABIからopen/ioctl/mmap/closeを呼ぶ。
  libkeystone-host.aやEnclave.hppには依存しない。version/size、予約引数、errno/SM statusを検査する。
- C clientにはloader/runtime/eappのfile読込み、page丸め、領域配置、FINALIZE、RUN/RESUME、
  OCALL配送、cleanupが必要。既存SDKの実配置契約を参照して最小限を実装する。
  Eyrie側の本物のELF loaderを置き換えず、正常測定・データ往復・破棄を先に実QEMUで照合する。
  新clientの配置不具合をSMの不具合として誤認しない。
- edgecallは既存Cライブラリと共通structを再利用する。process-globalな共有領域設定は
  C実装でも残るため、独立enclaveのworkerをprocess分離する設計は維持する。
- report検査はshared/crypto_abi.hの正規型、既存CのSHA3/Ed25519実装を再利用する。
  C++のReport.hpp/Report.cpp/json11.cppを新clientへリンクしない。C++ verifierと同じ
  署名対象・chain・pins・nonce・長さ条件をsource-reviewで照合し、正規/改変reportを試験する。
  純C archiveの利用または同じCソースからのC専用library作成をP0で確定し、暗号方式は新設しない。
- Makefileは新clientを$(CROSS_COMPILE)gccでbuild/linkする。GNU C11と必要なLinux拡張を
  明示し、g++やlibstdc++を新clientの必須依存にしない。symbol/link結果も確認する。

既存のC++ SDK・samples・verification実装は、この言語指定だけを理由に全てCへ書き直さない。
新規Cコードが直接試験するdriver/SM/Eyrieと、既存C++ SDK自身の挙動を明確に区別する。

## 9. 実装順序と完了条件

| 段階 | 作業 | 成果物・完了条件 |
| --- | --- | --- |
| P0：契約/観測設計 | C clientの正常配置/測定・C verifierの契約確認、全11 FID、driver、標準SBIの状態/lock順序/資源/期待statusを調査。既存caseと未実装分を分離 | cases台帳、source-review、合法順序、観測方法、hook位置が確定。未確定契約を既存実装に合わせて黙認しない |
| P1：lifecycle/driver | A-2001/2002/2003/2005/2006/3004/3006/4000の各群、Rのlifecycle、X-01〜08/14〜17を優先。まずdriver直列化、次にSM競合 | 一度だけ実行/回収、所有者拒否、通常再作成、実際の重なりの証跡。新たな不具合はKSBへ記録して修正後再試験 |
| P2：障害注入とservice競合 | F-ALLOC/COPY/PMP/IPI、R-3001/02/03、X-09〜12/18/20、RNG/provider faultを実装 | 実際の発火、bounded failure、workspace分離、回復/隔離。別guestのfail-stopも観測可能 |
| P3：混合・拡充 | X-13/19、全hart組合せ、有限stressと複合faultを段階追加 | 再現seed・反復回数・coverage、資源の傾向、最終通常suite回帰を保存 |

P1の同時実行制御にはevent観測等P0の基盤が先に必要。
PMP失敗の詳細試験はP2のfault hook導入後に完了させる。
criticalなPMP/所有権/rollback不具合はstress拡大より先に修正する。
発見した不具合は[バグ台帳](keystone-bug-report.md)へ登録し、試験ID、由来、修正commit、
再実行結果を同じentryへ追記する。計画上のリスクだけを確定バグとして登録しない。

## 10. 実行matrixと記録

最初は2 hartで再現可能な競合を作り、その後K3既定16 hartへ拡張する。
Sstc on/off、通常crypto unavailable/qemu-dev、multimem on/offは対象caseだけ組合せる。
RNG 3 backend・障害状態も関係するcaseへ限定し、全caseの無意味な直積を作らない。
EPM/backingは小容量、TOR非NAPOT境界、256 MiB級、上限/枯渇を分ける。
2 hart構成とCMA容量・kernel command lineが試験条件を満たすことを起動前に確認する。
実機固有のWTM所有権、DMA/IOPMP、未統合paging/hash/crypto、hotplug/suspend、
単一enclaveの多スレッド、FP/Vector完全対応はこの「既存機能」の合格対象に含めない。

各結果の必須欄：case/category、FID集合と標準SBI EID、A/B・sameの区別、file/UID/SM ID/generation、
hart配置、要求sequence、合法順序/実際のevent、stageと発火回数、全status、データ検証、
回収/隔離、timeout観測、seed/反復数、全成果物hashと対応commit/profile。
正常終了を返しただけで競合caseをPASSにしない。競合未到達・注入未発火は別記する。

結果はPASS / FAIL / SKIP / INCONCLUSIVE / SETUP_ERRORを区別する。
SKIPにはunsupported profile等の理由を必須とし、missing image/moduleや版不一致は
SETUP_ERRORとしてsuite成功に含めない。予定caseはPLANNEDのままでPASSと数えない。

完了判定は実装機能ごとに異常case、同FID競合、Xの機能対、fault段階、hart/profileのcoverageで示す。
「全PASS」には選択case数と未実施範囲を添え、実機や全ての組合せの証明へ一般化しない。
