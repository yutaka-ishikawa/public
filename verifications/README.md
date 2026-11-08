# Keystone SBI 2001–2004のソース検証

初回検証：`c6598a7bc`。修正後の検証対象：`cc885e6ad`。検証日：2026-11-08（日本時間）。

| function ID | 対象 | ソース検証 | 実行結果 |
| --- | --- | --- | --- |
| 2001 | CREATE | [内容](sbi-2001/source-review.md) | [結果](sbi-2001/results.md) |
| 2002 | DESTROY | [内容](sbi-2002/source-review.md) | [結果](sbi-2002/results.md) |
| 2003 | RUN | [内容](sbi-2003/source-review.md) | [結果](sbi-2003/results.md) |
| 2004 | 未定義 | [内容](sbi-2004/source-review.md) | [結果](sbi-2004/results.md) |

これらはextension ID `0x08424b45`のfunction IDであり、独立したSBI extension IDではありません。2004は上流・現在のコードともに未定義です。RESUMEは2005なので、2004をRESUMEへ読み替えていません。

## 実行方法

64ビットLinuxホストにCコンパイラ、GNU Make、Python 3、Bashが必要です。クロスコンパイラ・SDKの事前ビルド・root権限・QEMUは不要です。

```bash
bash verifications/run-all.sh
```

個別に実行する場合：

```bash
bash verifications/sbi-2001/run-tests.sh
bash verifications/sbi-2002/run-tests.sh
bash verifications/sbi-2003/run-tests.sh
bash verifications/sbi-2004/run-tests.sh
```

Makefileからの実行も可能です。

```bash
make -C verifications test
make -C verifications/sbi-2001 test
```

既定は`-O2 -g`とAddressSanitizer／UndefinedBehaviorSanitizerです。フラグを変更する場合は先にcleanしてください。

```bash
make -C verifications clean
bash verifications/run-all.sh CFLAGS='-O0 -g' SANITIZERS=undefined
```

`assert`が判定を行うため、`-DNDEBUG`を指定しないでください。失敗はassert、sanitizer、makeまたはスクリプトの非ゼロ終了として検出します。

## 何を実行しているか

各`test.c`は[共通ハーネス](common/harness.c)と、ビルド時に生成する`production.inc`をコンパイルします。[抽出スクリプト](common/extract_sources.py)は、現在のリポジトリの実際の関数本文をそのまま取り込みます。関数のコピーを手動で書き直したモデルではありません。

実際に取り込むのは、OpenSBIの`sbi_ecall_handler()`、Keystone adapter、対象のSM helper、CREATE/DESTROY/RUN本体と関連関数、RUN/RESUME/STOP/EXITのcontext切替・threadのレジスタ交換処理です。`enclave.h`、`thread.h`、`pmp.h`、共有ABIヘッダーは実ファイルを使用し、OpenSBIのtrap register構造も実定義から生成します。

各SBIディレクトリの`build/`には以下を生成します。

```text
production.inc          実関数の抽出結果（#lineで元ファイル・行に対応）
include/sbi/            ホストビルド用の最小型定義と実trap構造
source-manifest.json    入力ソースのSHA-256と抽出した関数一覧
test                    実行バイナリ
run.log                 run-tests.shで実行したログ
```

再実行時には現在のソースを検証するため、掲載した結果と異なる場合があります。初回の入力ハッシュとログは各`baseline/`に、修正後の再検証記録は各`after-fix/`に保存しています。

## 模擬している境界と未確認範囲

| 対象 | このテストの扱い |
| --- | --- |
| 状態遷移・引数検証・後始末・戻り値・PC補正 | 実ソース関数を実行。 |
| EPM／UTM | ホスト上のページ整列済み配列。実際のゼロクリアを検査。 |
| threadレジスタ交換 | 実C関数。CSRアクセスだけ配列へ置換。 |
| PMP確保／設定／解除／IPI | 呼び出し順序と成功・失敗を模擬。実CSR・遠隔hartの動作は未検証。 |
| argument copy | コピーの成功／失敗を注入。実MPRV、Linuxアドレス変換、例外回復は未検証。 |
| enclave測定 | 呼び出し順序と成功／失敗を模擬。SHA3と測定対象の正しさは別検証。 |
| lock | 単一スレッドでlock/unlockの整合を検査。実際の同時実行は未検証。 |
| `sm_is_ready`・extension lookup | 準備状態とextensionの存在を模擬。実際の全hart準備判定・登録は未検証。 |
| platform hooks／CPU context | hookの順序とcontextフラグを模擬。 |
| ecall／mret | dispatcherのC処理を実行するが、CPU命令としてのecall/mretは未実行。 |
| Linux driver／SDK | 呼び出し経路をソースで確認。今回のホストテストには含めない。 |

したがって、QEMUゲストや実機で全機能が確認できたという結果ではありません。ホスト側ソース試験で再現できる範囲を明示して実行しています。

## 判定の読み方

- `PASS`：記載した正常系・異常系の期待結果を確認。
- `OBSERVED ... [not an acceptance PASS]`：現在の不整合・要修正動作の再現に成功。正常性の合格ではありません。
- テスト全体のexit 0：期待した観測と一致したことを意味します。`OBSERVED`が解決したことは意味しません。

初回は計36グループ（正常系・異常系31、要修正動作の再現5）でした。修正後は2001が12、2002が15、2003が16、2004が5の**計48グループがPASS**し、OBSERVEDは0です。1グループ内で複数の入力や100回の繰り返しを検査しているため、assert数や入力数とは異なります。

初回に検出したDESTROYの状態条件、PMP APIのエラー未反映、EIDの32ビットへの切り詰めは`cc885e6ad`で修正しました。各SBIの`source-review.md`と`results.md`に修正内容・再検証結果・限界を記載しています。2001のrollback等の未検証事項まで解決したという意味ではありません。

既知の動作を修正した場合は、対応する`OBSERVED`の再現テストを、修正後の期待を確認するテストへ更新してください。修正前の不整合を将来も維持するための仕様ではありません。

2002/2003の修正設計・更新手順は[修正と再検証](../doc/keystone-sbi-2002-2003-fixes.md)を参照してください。
