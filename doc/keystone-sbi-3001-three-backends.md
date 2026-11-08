# SBI 3001の3方式設計とQEMU統合試験

設計日：2026-11-08。ユーザー指定の3方式を実装するための設計。
この文書のオプション・追加ファイル・試験スクリプトは実装予定であり、まだ使用できない。
従来の共通DRBG中心の案に優先する。

## 1. 採用する3方式

| backend名 | 供給・生成方法 | 位置づけ |
| --- | --- | --- |
| `zkr` | seed CSRをCSRRWで取得 | Zkrが利用可能な機種で優先する。 |
| `jupiter2` | OP-TEEのK3 WTM RNGドライバを参考に、SMからMMIOで読み出す | Jupiter 2向け。OP-TEE全体の起動は要求しない。 |
| `counter` | OpenSBIのタイマー等の値と呼び出し連番を混合して疑似乱数を作る | 他機種用の初期実装・動作確認用。暗号学的な乱数ではない。 |

アプリ・EyrieからのSBI 3001呼び出しは全方式で共通にする。
③はユーザー指定により、初期実装として成功値を返せるようにする。
ログと記録には `quality=development` と出し、①②のハードウェア供給元と区別する。
③の出力を理由にattestation/sealing等を自動で有効化しない。

## 2. 共通の処理構造とABI

```text
enclaveアプリ / getrandom / AT_RANDOM
  → Eyrie checked API
  → Keystone拡張 0x08424b45 / FID 3001
  → OpenSBI dispatcher
  → SMの共通rng_read64()
       ├─ zkr backend
       ├─ jupiter2 backend
       └─ counter backend / 他機種で差し替えるplatform hook
```

成功時は `a0=0`、`a1=64 bit値`。失敗時はa0にstatusを返し、a1を0にする。
0や全bitが1のデータ値をエラーと解釈しない。
未対応、供給待ち、故障を区別し、内部エラーを既存Keystoneの正のstatusへ明示変換する。
乱数源や実装方式を、a1の一部に埋め込んでデータ幅を変えない。

handlerのenclave context検査は維持し、通常Linuxからの3001直接呼び出しは拒否する。
Eyrieのasmはa0/a1を同じecallの出力として宣言し、a0を確認してからa1を使用する。
getrandomの長さ・flags・部分取得・コピー失敗、AT_RANDOMの失敗も処理する。

### SM内部インターフェイス案

```c
struct keystone_rng_ops {
    const char *name;
    const char *quality;
    int (*init)(void);
    int (*read64)(uint64_t *value);
};

int keystone_rng_init(void);
int keystone_rng_read64(uint64_t *value);
int platform_rng_read_counter(uint64_t *value);
const struct keystone_rng_ops *platform_rng_get_ops(void);
```

これらはM-mode内のAPIで、新しいSBI番号は追加しない。
`read64()` の内部statusとSBI ABIのstatusは共通層で変換する。
platform hookを通して、他機種で独自TRNG・管理firmware等へ差し替えられるようにする。

## 3. 選択方法

ビルド設定を `KEYSTONE_RNG_BACKEND=auto|zkr|jupiter2|counter` とする案。
`auto`は選択方針であって、4個目のbackendではない。

- `auto`：利用可能なZkrを優先。なければJupiter 2でWTM RNGを選ぶ。
  どちらも使えない機種ではcounterを選び、開発用であることを表示する。
- autoで次候補へ進むのは非搭載・非対応の場合。選択できるハードウェアの故障や
  timeoutはエラーとして扱い、counterへの変更で隠さない。
- 明示指定：指定した方式だけを初期化する。利用不能時は別方式へ黙って切り替えない。
- 機種情報なしにJupiter 2のMMIOアドレスへアクセスしない。
- 起動時に選び、enclave実行中の一時エラーを理由にbackendを変更しない。

例となる起動ログ：

```text
[SM/RNG] requested=counter selected=counter quality=development
[SM/RNG] source=opensbi-timer
```

manifestには要求した方式、起動ログには実際に選択した方式を記録する。
QEMU試験ではautoだけを使わず、3方式をそれぞれ明示指定して検査する。

## 4. ① Zkr backend

OpenSBIの `sbi_hart_has_extension(..., SBI_HART_EXT_ZKR)` とCSR helperを使う。
seedは `csr_swap(CSR_SEED, 0)` 等のCSRRWで読み、ES16の値だけを受け取る。
RV64では必要数の16 bit値を収集する。WAIT/BISTには上限、DEADには故障statusを設ける。
ZkrなしのhartではCSRへアクセスしない。

QEMUの既存seedモデルは `qemu_guest_getrandom()` に接続されるので再利用する。
QEMU起動時に `zkr=true` を明示し、DT/検出結果を確認する。
実機でのconditioning・取得データの扱いはZkrの仕様と供給品質に従う。
QEMUのホストcrypto backendの性質を、実機の全供給元にそのまま仮定しない。

混成hartでZkr対応が異なる場合の初期版は、非対応hartで明示的に未対応を返す。
別hart経由の代理取得や再seed配信は別途設計し、誤って非対応CSRを実行しない。

## 5. ② Jupiter 2 WTM RNG backend

参考実装はベンダーOP-TEEの `core/arch/riscv/plat-k3/k3_prng.c` とclock制御。
確認済みソースとアドレスは [調査記録](jupiter2-security-engine-rng.md) に固定する。

初期実装では8 bytesをCPUで読む方式だけを採用し、DMAは使わない。
clock確認、reset/初期化、ready待ち、FIFO、32 bitデータ読み出し、排他、timeoutを移植する。
OP-TEEのmutexやMMIO wrapperはOpenSBI/SMの仕組みに置き換える。
既存OP-TEEと同時に同じエンジンを独立管理しないよう、所有方針も定義する。

「セキュアプロセッサからのデータ」は、まず確認できているWTM系RNGを指す。
RT24上のESOS、AP上のOP-TEE trusted-domain、TE200暗号エンジンは同一物として扱わない。
WTMドライバが選ぶhardware PRNGのseed/reseed・品質仕様は実機段階で確認する。

### QEMUで同じSMドライバを実行する

QEMUにWTM RNGのMMIOデバイスモデルを追加する。
SMのRNG関数を別関数へ置き換えるのではなく、実際のMMIOアクセスをモデルが処理する。

| 要素 | モデル化するもの |
| --- | --- |
| RNG window | `0xf0703000`から4 KiB。RNGレジスタはその中の`+0x800`。 |
| RNG制御 | FIFO clear、RNG_EN、PRNG_VALID、32 bit data読み出しと更新。 |
| clock/reset | ドライバが参照するPMUMAIN/PMUAPのbitとreadback。clockなしではreadyにしない。 |
| データ供給元 | QEMUの既存 `qemu_guest_getrandom()` / crypto backend。 |
| 異常状態 | 有限ready遅延、readyにならない状態、供給失敗等を明示的に選べる。 |
| 範囲 | RNG取得に必要な機能だけ。TE200暗号処理、DMA、SoC全体は実装しない。 |

PMUMAINは `0xd4050000`、PMUAPは `0xd4282000`。
触るレジスタとmaskは参考ドライバから確定する。
QEMU `jupiter2` machineへデバイス有無を切り替えるpropertyを追加する。
搭載時だけfirmware DTへ専用nodeを生成し、SMはそのnodeとstatusから存在を判定する。
新しいcompatible文字列は設計時に定めるもので、既存の標準bindingとは主張しない。
実機はOpenSBIへ渡すDTに同じ情報を追加するか、確認済みplatform descriptorで指定する。
機種名だけを見て、モデル無効時や未知の機種のMMIOへアクセスしない。
QEMUでの成功はSM/MMIO/エラー処理の動作確認であり、実機の乱数品質や電気特性の証明ではない。

## 6. ③ 他機種向けinterface＋counter backend

他機種で `platform_rng_get_ops()` を差し替えられるようにし、初期実装としてcounterを用意する。
counter取得だけを差し替える場合は `platform_rng_read_counter()` を使う。

### 初期の生成方法

1. OpenSBIの `sbi_timer_value()` でtimestamp相当の値を取得する。
2. hart IDと、lockまたはatomicで管理する呼び出し連番を加える。
3. 既存SHA3で `backend tag || counter || hart ID || sequence` を混合し、64 bitを返す。
4. 同一tick中の連続要求にも対応する。出力の偶然の一致はそれだけで失敗としない。

SHA3は値を混合するだけで、予測可能なcounterを本物のentropyに変えるものではない。
この方式は明示的な開発用生成器であり、共通DRBG・安全なentropy源として説明しない。
既存SHA3を再利用し、専用の暗号学的乱数アルゴリズムを新しく設計しない。

### 「RISC-Vなら必ず読める」ことは仮定しない

`time/cycle`を、すべてのRISC-V構成・実行権限で無条件に読めるわけではない。
OpenSBIはZicntrがあればrdtime、なければtimer deviceのcallbackを利用できる。
どちらの経路もないと `sbi_timer_value()` は0を返し得るため、初期化で利用可否を調べる。
利用不能な機種ではplatform hookを実装するか、未対応を返す。
カウンタ値0自体をエラーとする判定は使わない。

QEMUの既存timer/CLINTを使う初期版では、この取得経路を確認してから利用する。
他のマシンへこのRNG backendを移せることと、Keystone SM全体のPMP/HSM等の移植が
完了することは別。今回作るのは乱数部分の差し替え点。

## 7. 3方式のQEMU試験構成

全方式で `jupiter2` machine、実Linuxドライバ、OpenSBI、SM、Eyrieを使う。
③を試すために、SM全体を別machineへ移植する必要はない。

| 試験 | firmwareのbackend指定 | QEMU CPU | WTM model | 必須経路 |
| --- | --- | --- | --- | --- |
| ① | zkr | zkr=true | 無効 | 実seed CSR → 実SM → 3001 |
| ② | jupiter2 | zkr=false | 有効 | 実MMIO → 実SM → 3001 |
| ③ | counter | zkr=false | 無効 | 実timer → 実SM → 3001 |

backendを明示指定し、host側の起動スクリプトでOpenSBIログのselected値を照合する。
たまたま別方式へfallbackしてPASSした結果を認めない。
QEMU仮想デバイスによるハードウェアのエミュレーションと、以前削除した
ホスト上のC関数抽出・模擬SBI/PMP/CSR試験は区別する。後者は再導入しない。

### 共通の検査項目

- enclaveから実ecallし、a0/statusとa1/dataを正しく取得する。
- getrandomの0、1、7、8、9、15、16、17、32、4096 bytesと非整列バッファ。
- 前後canary、コピー失敗、成功バイト数、flagsの扱い。
- OCALLでデータを受け渡し、RESUME後にも同じ内容を確認する。
- ENV_SETUPのAT_RANDOM、enclave終了値、DESTROYを検査する。
- 複数回実行、複数enclave、複数hartで実行する。
- 通常Linuxから3001を直接呼ぶとSBI_PROHIBITEDになること。

値の変化は機能診断として記録し、乱数品質の証明にはしない。
③でも有効な出力値0を拒否しない。

### エラーと選択の試験

- zkr指定でzkr=false：未対応を返す。counterへ自動変更しない。
- jupiter2指定でWTMなし：事前検出で未対応とする。存在しないMMIOを無条件に読まない。
- WTMがreadyにならない：timeoutで戻る。SMが無限待機しない。
- WTMが供給失敗：エラーをEyrieへ伝播する。
- counterの取得経路なし：初期化エラーを返す。
- auto：①→②→③の機種別選択と起動ログを確認する。

既存QEMUが実行で再現しないZkrのWAIT/DEAD分岐は、再現用CPUモデルの追加がなければ
未検証と記録する。②の異常状態は追加するWTMデバイスモデルで再現する。

## 8. ビルド・実行用スクリプト案

以下は将来追加するコマンドの案で、現行スクリプトの実装済みオプションではない。

```bash
# 開発用ホスト：3方式のQEMU/firmwareと試験パッケージを準備
bash scripts/build-verification-rng.sh --backend all

# 開発用ホスト：方式ごとに別のQEMUゲストを起動して実行
bash verifications/random/run-qemu-matrix.sh --backend all
```

3方式で同じrootfsを同時に開かない。
共通ベースに方式ごとのqcow2 overlayを作り、各QEMUプロセスを終了してから結果を集約する。
ゲストアプリはvirtio-9pで共有する。
プロファイルごとにQEMU/firmwareのhash、選択backend、QEMU引数、kernel release、
guest試験ログ、終了コードを保存する。

予定出力先：`.build-keystone/rng/{zkr,jupiter2,counter}`。
firmware出力・manifest・ビルドキャッシュはbackendごとに分ける。
既存の `install/keystone` とrootfsを上書きしない。

## 9. 変更予定ファイルと実装順序

1. `keystone/sm/src/rng/`：共通API、Zkr、counter、backend選択。
2. `platform/spacemit_k3/`：WTM RNGとclock処理、機種別hook。
3. `sm-sbi.c` / `sm-sbi-opensbi.c`：既存3001へ接続。
4. `runtime/call/` / `runtime/util/` / `sys/env.c`等：checked APIと失敗伝播。
5. `qemu/hw/misc/`等：WTMモデルとclock/reset部分、Jupiter2への接続。
6. `scripts/`：backend指定をSM compiler、firmware link、manifestまで伝える。
7. `verifications/random/`：実enclave試験と3起動のmatrix。

Zkr、counter、WTMの順に動作させ、最後に3起動を連続実行する。
共通DRBGやboot seed方式は今回の3方式の必須要件から外し、将来の追加候補として残す。
完了判定は3方式それぞれで実3001/getrandom/AT_RANDOM/OCALL/DESTROYが完走し、
結果がbackend別に保存されること。
