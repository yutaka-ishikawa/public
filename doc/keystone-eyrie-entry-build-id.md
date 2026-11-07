# Keystone Eyrieの入口でGNU build-idを実行して停止した問題

## 結論

K3向けKeystone環境で`run-keystone-hello`が終了しなかった原因の一つは、Eyrieの固定起動アドレスにGNU build-id情報が配置され、それをCPUが命令として実行していたことだった。

この問題はOpenSBIのSM移植処理ではなく、Keystoneランタイムの起動コードとリンク配置の問題である。起動側が固定アドレスを使う一方、リンク側がその位置に起動コードを置くことを保証していなかった。

修正コミットは[`345f9d2bd`](https://github.com/yutaka-ishikawa/public/commit/345f9d2bd)。修正したexampleパッケージへの更新後、ユーザー環境で`run-keystone-hello`の動作が確認された。

## 元のKeystoneの問題だったのか

確認した上流Keystoneのコミット`b8c2385355b2afb5b94a4e5dc503936f1502b972`にも、今回の修正前と同じ`runtime/runtime.ld.S`の配置指定と、固定アドレスを使う`runtime/loader-binary/loader.S`の起動処理がある。`runtime/sys/entry.S`の`_start`も、専用の`.text._start`ではなく通常の`.text`に置かれていた。

したがって、元のKeystoneにも存在するリンク配置への依存が、今回のビルド環境で表面化した問題と整理できる。ただし、上流の標準ビルド環境でも必ず発生するという意味ではない。GNU build-idの生成設定やリンカーの配置規則によって結果が変わる。

今回の生成物で入口にbuild-id情報が存在したことは、GDBのメモリ内容から確認できた。一方、それを引き起こしたツールチェーンのバージョン・設定差のすべてを特定したわけではない。上流の将来版や他のリリースについても、この比較だけから判断することはできない。

## 症状

ゲストで`run-keystone-hello`を実行すると、ドライバがロードされ、パッケージの検証・展開が終わった後に停止した。SMのトレースではCREATEの各段階が成功し、RUNでenclaveへ切り替わったところまで記録されていた。

```text
[SM-TRACE] ... CREATE hash end rc=0
[SM-TRACE] ... sbi.return a=2001 b=0x0
[SM-TRACE] ... RUN switch begin
[SM-TRACE] ... RUN switch end
[SM-TRACE] ... sbi.switch-return a=2003 b=...
```

その後LinuxがRCU stallを報告したが、この警告自体は原因を示すものではなかった。

## 起動処理の構造

起動は概ね次の順序で進む。

```text
Linux上のexample
  → Keystoneドライバ
  → SMによるenclaveの作成・実行
  → enclave内のloader
  → Eyrieランタイム
  → helloアプリケーション
```

SMから最初に実行されるloaderと、その先で起動するEyrieは別の段階である。今回の異常は、loaderがEyrieを仮想アドレスで起動する段階で発生した。

`keystone/runtime/loader-binary/loader.S`は、EyrieのELFを読み込む`load_runtime`の後で、次の処理を行う。

```asm
li t0, RUNTIME_VA_START
csrw stvec, t0
```

この環境の`RUNTIME_VA_START`は`0xffffffffc0000000`である。loaderはページテーブルを準備して`satp`を切り替え、その後の例外遷移を介して、この固定アドレスのEyrie起動コードへ処理を渡す。

この起動経路は、ELFヘッダーのエントリーポイントを読み取ってそこへ分岐する方式ではない。「固定アドレスに起動コードがある」という配置上の約束に依存する。

## GDBで確認した証拠

停止時、Thread 7（CPU#6）のレジスタは次の状態だった。

```text
pc      0x102025432
mepc    0xffffffffc0000000
mcause  0x2
mtval   0x4
satp    0x800000000010d206
mtvec   0x102000cb0
```

各値の意味は次のとおり。

| 値 | 意味 |
| --- | --- |
| `mepc = 0xffffffffc0000000` | 例外を起こした命令のアドレス。Eyrieの固定入口に一致する。 |
| `mcause = 2` | 不正命令例外。 |
| `mtval = 4` | 不正命令として取得された命令ビット。 |
| `pc = 0x102025432` | 停止時点ではファームウェア側を実行していた。正確な関数の特定には実行中のファームウェアと一致するELFが必要。 |

入口のメモリを読むと、次の内容だった。

```gdb
(gdb) x/8wx 0xffffffffc0000000
0xffffffffc0000000: 0x00000004 0x00000014 0x00000003 0x00554e47
```

これはGNU build-id noteのヘッダーに一致する。

| ワード | 意味 |
| --- | --- |
| `0x00000004` | 名前の長さ：4バイト。 |
| `0x00000014` | build-id本体の長さ：20バイト。 |
| `0x00000003` | GNU build-idのnote種別。 |
| `0x00554e47` | リトルエンディアンで`GNU\0`。 |

build-idはバイナリを識別するための情報であり、実行するコードではない。CPUは入口の先頭を命令として解釈し、無効な圧縮命令`0x0004`によって不正命令例外を起こした。メモリの内容と`mcause`・`mtval`が一致しており、入口で実行していたものを特定できた。

なお、GDBの`x/10i $pc`がファームウェア側アドレスの読み取りに失敗したことだけでは、そのファームウェア命令が不正だったとは判断できない。

## なぜリンク時に防げなかったのか

修正前のリンクスクリプトには、次の記述があった。

```ld
. = 0xffffffffc0000000;
PROVIDE(rt_base = .);
.text : {
  *(.text._start)
  *(.text.encl_trap_handler)
  *(.text)
}
```

ここには二つの問題があった。

1. `.text._start`を先頭に集める指定はあるが、実際の`_start`は通常の`.text`に置かれていた。起動コードを専用セクションで先頭へ固定する仕組みが機能していなかった。
2. `.note.gnu.build-id`の配置・除外が指定されていなかった。このような未指定のセクション（orphan section）の配置はリンカーが決める。今回の生成物では、固定の起動アドレスにbuild-id情報が配置された。

アドレスカウンターを固定値に設定するだけでは、未指定セクションも含めた最終的な配置を保証できない。起動コードの位置を検証するリンク時のアサーションもなかった。

## RCU警告とタイマーの関係

今回の観測を因果関係で整理すると、次の順序になる。

```text
Eyrieの入口にGNU build-id情報がある
  → CPUが情報を命令として実行する
  → 不正命令例外が発生する
  → 正常な起動処理・Linuxへの復帰が進まない
  → 時間が経過し、別のCPU上のLinuxがRCU stallを検出する
```

RCU警告は、LinuxがCPUの進行停止を検出した結果である。警告を出すタイマーを変更したり警告を抑止したりしても、入口にコードがない問題は解消しない。

ただし、不正命令例外の後にOpenSBI内部でどの処理を繰り返していたか、なぜ正常に復帰しなかったかという細かな経路までは、今回のレジスタとメモリの記録だけでは確定していない。ここは、確認できた入口の配置問題と区別する必要がある。

## 修正内容

### 起動コードを専用セクションへ移動

`keystone/runtime/sys/entry.S`で、`_start`を専用の実行可能セクションに置き、グローバルシンボルとして公開した。

```asm
.section .text._start,"ax",@progbits
.global _start
_start:
```

起動コードの後では通常の`.text`へ戻し、トラップ処理などと分離した。

### 起動アドレスをリンク時に保証

`keystone/runtime/runtime.ld.S`へ次の指定を加えた。

```ld
ENTRY(_start)
```

```ld
.text : {
  KEEP(*(.text._start))
  /* 以下、既存のコードセクション */
}
```

```ld
/DISCARD/ : { *(.note.gnu.build-id) }
ASSERT(_start == rt_base, "Eyrie entry must equal RUNTIME_VA_START")
```

それぞれの目的は次のとおり。

| 修正 | 目的 |
| --- | --- |
| 専用の`.text._start` | 起動コードをほかのコードから分け、先頭へ配置する。 |
| `KEEP` | セクションの削除対象になっても起動コードを保持する。 |
| `ENTRY(_start)` | ELFヘッダー上の入口も明示する。 |
| build-id noteの除外 | 実行時に不要な付加情報が入口へ入り込むのを防ぐ。 |
| `ASSERT` | `_start`が固定入口と一致しなければリンクを失敗させる。 |

`ENTRY(_start)`だけでは、固定アドレスを使うloaderの問題は解決しない。実際のコード配置を保証し、生成時に検査することが重要である。

## 検証結果と限界

次を確認した。

- helloとhello-nativeのSDK・ランタイム・パッケージを再ビルドできた。
- build-id生成を明示的に有効にし、通常の`.text`を先に含むテストでも、専用の`_start`が固定入口へ配置された。
- 両exampleのELFについて、入口と`_start`が`0xffffffffc0000000`に一致し、GNU build-id noteが存在しないことを確認した。
- loaderが読み込む実行可能な`PT_LOAD`領域の入口バイトが、build-idヘッダーではないことを確認した。
- 既存のraw loader入口テスト、SMの戻り処理テスト、タイマーポリシーテストが成功した。
- 生成した両exampleパッケージの整合性チェックが成功した。
- 更新後、ユーザー環境で`run-keystone-hello`が動作した。

入口の配置テストは、ゲスト内でenclave全体を実行するテストの代替ではない。今回の実行確認は、ユーザー環境での動作報告による。

テストは次のファイルにある。

- [`../scripts/tests/test-keystone-runtime-entry.py`](../scripts/tests/test-keystone-runtime-entry.py)
- [`../scripts/tests/test-keystone-loader-entry.py`](../scripts/tests/test-keystone-loader-entry.py)

CIにもランタイム入口の配置テストを追加している。

## 既存環境の更新手順

停止しているQEMUを終了してから、ホスト側のリポジトリで実行する。

```bash
git pull
bash scripts/build-install-keystone.sh --only sdk
bash run-k3.sh
```

起動したゲスト側で、共有ディレクトリ経由で両パッケージを更新する。

```bash
mount-hostshare
cp /mnt/host/.build-keystone/examples/hello/hello.ke \
   /usr/share/keystone/examples/hello.ke
cp /mnt/host/.build-keystone/examples/hello-native/hello-native.ke \
   /usr/share/keystone/examples/hello-native.ke
chmod +x /usr/share/keystone/examples/*.ke
run-keystone-hello
```

この修正はexampleパッケージ内のEyrieに含まれる。OpenSBI・カーネル・rootfsの再ビルドは不要である。`--build-dir`や`--hostfs`を変更している場合は、コピー元のパスを実際の構成に合わせる。

以前の強制停止でmakeselfの展開ディレクトリが残っている場合は、例えば`run-keystone-hello --target /tmp/hello-entry-fixed`のように未使用の展開先を指定する。

## 以前に修正した問題との区別

この調査では、今回の修正前にも別の問題を修正している。

| コミット | 対象 |
| --- | --- |
| `75f5c0377` | SMのトラップ復帰・戻り値ABI・K3のタイマーポリシーなど。 |
| `f1c0678e5` | raw loaderでGOT経由のスタック初期化が成立しない問題など。 |
| `345f9d2bd` | 本文のEyrie入口とGNU build-idの配置問題。 |

見かけ上はいずれも「RUNの後で停止する」症状になり得るが、失敗する段階と原因は異なる。これまでのすべての停止がbuild-idだけに起因していた、あるいは移植したOpenSBIに問題がなかったと結論付けることはできない。
