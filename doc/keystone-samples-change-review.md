# samplesとKeystone元ソースの差分評価

2026-11-09（日本時間）。比較元はこのリポジトリ内の`keystone/examples/`であり、
上流全バージョンに共通する不具合・優劣を断定する文書ではない。
今回の修正前commitは`3054460c4`、元examplesのGit treeは
`8052282e231f8cbc9bba04c10ed4ee9f9248275f`。元examplesは変更しない。

## 評価方法

- **改善**：失敗検出、正規ABI、未初期化値など、正当性が具体的に改善したもの。
- **適合**：現在のK3・SDK・toolchainまたはユーザー指定のビルド方法に合わせたもの。
  元の対応環境でも劣っているという意味ではない。
- **劣化／機能削減**：設定、インターフェイス、計測の意味、実行経路・負荷条件を失ったもの。
- **中立**：書式、prototype整理など、単独では実行機能の優劣を示さないもの。
- 1つの変更に利点と不利益がある場合は両方を書く。QEMUのPASSだけで性能向上とは判定しない。

## ソースごとの比較

| 元 | 統合先 | 理由・判定の詳細 |
| --- | --- | --- |
| keystone/examples/hello | samples/helloworld | [helloworld/CHANGES.md](../samples/helloworld/CHANGES.md) |
| keystone/examples/hello-native | samples/hello-native | [hello-native/CHANGES.md](../samples/hello-native/CHANGES.md) |
| keystone/examples/attestation | samples/attestation | [attestation/CHANGES.md](../samples/attestation/CHANGES.md) |
| keystone/examples/tests（9例） | samples/tests | [tests/CHANGES.md](../samples/tests/CHANGES.md) |

## attestorのループは復元した

復元と本評価のcommit：[715228dcf](https://github.com/yutaka-ishikawa/public/commit/715228dcf7ca4aeb3b1694fb4e69f58dde4cab74)。

初回統合のループ削除は、report ABIや鍵の適合に必要な変更ではなかった。
1〜10000のループ、5000/10000での2回のOCALL、続く共有nonceコピーを復元した。
これにより正常経路の追加2回のhost復帰/RESUMEも戻す。
OCALL等の失敗を検査して非0を返す改善は維持する。

このループは負荷・往復動作の確認には意味があるが、時計の取得や集計はない。
一定の実時間や最適化後の命令数を保証するベンチマークではなく、attestationを
10000回呼ぶ試験でもない。元の作者の意図はコードだけでは断定しない。

## 共通runner・ビルドの評価

| 変更 | 理由 | 判定・残る課題 |
| --- | --- | --- |
| SDK status・終了値・report検証・DESTROYの失敗を非0へ | 自動実行で失敗を検出 | 改善。誤retval・誤公開鍵・無効providerの拒否を実QEMUで確認済み |
| 外部公開metadataの使用 | 現qemu-dev identityとSM測定契約へ適合 | 適合・運用性改善。外部pinの真正性と保護が必要。元の固定pinより無条件に安全とは言えない |
| 共通runner.hppへ実装とmainを集約 | 重複削減 | 重複は減るが、責務分離・独立性・拡張性は劣化。通常の.cppとAPI分離が望ましい |
| 実行ファイル名でattestor等の動作を選択 | 共通化の簡略化 | 設計上の劣化。明示的モード指定の方がよい。現コードはリネームで挙動が変わる |
| 全runnerにverifier依存を追加 | 単一runnerの共有 | 非暗号例には余分な依存。依存面は劣化。サイズ・性能の定量比較は未実施 |
| 旧メモリ指定・計測・load-only等のCLI削減 | 共通化の簡略化 | 機能劣化。統合に不可欠ではない。今回は未復元として明示 |
| 固定メモリ値の統一 | 小規模例を揃える | 下表のとおり増減が混在。全例で資源効率が改善したとは言えない |
| 文字列表示とOCALL戻り値を共通化 | byte長に基づく出力へ | 境界の扱いは改善するが、旧表示・printf戻り値との互換性が低下。仕様維持または明示optionが望ましい |
| CMakeのアプリ定義→GNU Make | ユーザー指定の開発方式へ | 要求への適合。SDK/EyrieにはCMakeを継続使用。方式単独の優劣は判定しない |
| 独立SDK/runtimeの再利用 | アプリだけ反復ビルド | 利便性の利点。profile切替・別SDK時の再build条件に注意が必要で、機能の上位互換ではない |
| snapshot/checksumによる展開、引数と依存関係の整理 | 既知のmakeself pipe問題回避と再ビルド | 手順の改善。checksum検査はSM実行の証明ではない |
| K&R、braces、main(void)、tab配置 | 指定スタイルへ | 主に中立。性能改善・脆弱性解消を一律に主張しない |

### 明示的に変更したメモリ要求

| 例 | 元のfree / UTM | 現在のfree / UTM | 評価 |
| --- | --- | --- | --- |
| hello | 256 KiB / 256 KiB | 2 MiB / 256 KiB | free要求増加。helloに必須と確認した増量ではない |
| hello-native | 1 MiB / 1 MiB | 2 MiB / 256 KiB | free増、共有容量減。改善とは一括判定できない |
| attestation | 48 MiB / 2 MiB | 2 MiB / 256 KiB | 要求量減、拡張余地減 |
| tests | 48 MiB / 2 MiB | 2 MiB / 256 KiB | 小規模例はPASS。元の負荷条件を再現する設定ではない |

これらはSDKへの要求値であり、allocatorの丸めを含む実EPM確保サイズではない。
ELF・CRT・heap配置・コンパイラの違いでも測定値は変わる。旧enclave hashや
測定依存のsealing keyとの互換性を保証するものではない。

## 今回の対応と未対応

今回実際に戻したのはattestorのループ・値OCALL・nonceコピーとの処理順。
CLI、print戻り値、共通runnerの分割、ファイル名依存、メモリ設定、fib-benchのcycles測定、
long-nopの総ELF条件等は、評価対象として記載したもので今回すべてを復元したわけではない。
特にrdtimeへの変更は実行可能性の改善とcycles計測機能の削減を伴い、元のbenchmark結果と比較しない。

残すべき正当性修正は正規report長、status検査、未初期化値修正、失敗の非0伝達、
現SDKへの起動ABI適合。機能削減は別の判断事項として扱い、すべてを「改善」とは呼ばない。

既存[統合説明](keystone-examples-integration.md)、[通常の検証結果](../samples/results.md)、
[ループ復元の証跡](../samples/evidence/loop-restored.json)を参照。
