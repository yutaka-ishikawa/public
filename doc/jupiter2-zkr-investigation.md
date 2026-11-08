# Milk-V Jupiter 2のZkr対応調査

調査日：2026-11-08。

## 結論

**現時点でJupiter 2の実機でZkrが使用できる根拠は確認できない。
SBI 3001の実機移植では、Zkrを非対応として扱い、供給元を別途確認する。**

これは「シリコンにseed CSRが絶対に存在しない」と断定するものではない。
提供済み実機情報、K3 CPU定義、公式製品資料のいずれにも対応宣言がなく、
実機M-modeでのseed CSRの実行結果もないため、使用可能とは判断できないという結論。

QEMUで `zkr=true` を指定すれば使えることや、OpenSBIがZkrのコードを持つことは、
実機K3のハードウェア対応を示さない。同じOpenSBIのK3ブランチでも同様である。

## 照合した証拠

| 資料 | 確認した事実 | 判断できる範囲 |
| --- | --- | --- |
| ユーザー提供の実機情報 | X100がhart 0–7、A100が8–15。Linux共通ISAにzkrなし。 | 提供された実行環境ではZkrが宣言されていない。 |
| リポジトリの `linux-k3/.../k3-cpus.dtsi` | 全16 CPUのriscv,isa-extensionsを個別に確認。X100/A100ともzkrなし。zktやvector cryptoはある。 | このK3 CPU定義はZkrを広告していない。 |
| Milk-V公式サイトのGitHubソース | Jupiter2のK3/RVA23表記を確認。確認したJupiter2ページ・docsにZkr/seed/TRNGの対応宣言なし。 | 製品ページだけからZkr対応とは言えない。 |
| SpacemiT公式OpenSBI K3ブランチ | 汎用Zkr検出と、検出時のmseccfg.SSEED設定あり。K3固有のZkr有効化・乱数/TRNG providerは見つからない。 | ソフトウェア側の条件付き対応はあるが、実機対応の証明ではない。 |
| RISC-V公式RVA23仕様 | ZkrはRVA23S64のOptional Extensions / Expansion Options。 | RVA23準拠はZkr必須を意味しない。 |
| 第三者のK3/X100/A100実機ログ | 別ボードK3-CoM260の各hart ISAにもzkrなし。 | 補助資料。Jupiter2の実測値の代用にはしない。 |

提供済み情報の要約・元ファイルhashは
[実機inventory](jupiter2-hardware-inventory.json) を参照。
CPU定義は [k3-cpus.dtsi](../linux-k3/arch/riscv/boot/dts/spacemit/k3-cpus.dtsi)。

`Zkt` は実行時間に関する拡張で、乱数を提供する `Zkr` とは別。
暗号命令やvector cryptoが存在してもseed CSRの存在は推論できない。

## 公開資料の参照先と調査版

Milk-Vサイト本体への接続はこの環境でHTTP 403となったため、
公開されている公式GitHubのサイトソースを調査した。

- [Milk-V公式Jupiter2製品ページのソース](https://github.com/milk-v/milkv.io/blob/0b9a83599c02047731e48e725a40ee941010265d/src/pages/jupiter2/index.js)
- [Milk-V公式Jupiter2概要](https://github.com/milk-v/milkv.io/blob/0b9a83599c02047731e48e725a40ee941010265d/docs/jupiter2/overview.md)
- [SpacemiT公式OpenSBI・K3ブランチ](https://github.com/spacemit-com/opensbi/tree/ad6091fd16c4d4a445168f6a479e1b8aadb4931c)
- [上記OpenSBIのZkr検出・seedアクセス制御](https://github.com/spacemit-com/opensbi/blob/ad6091fd16c4d4a445168f6a479e1b8aadb4931c/lib/sbi/sbi_hart.c)
- [RISC-V公式RVA23仕様](https://github.com/riscv/riscv-profiles/blob/e030b70992447784ad9081045d47ce6e278f381e/src/rva23-profile.adoc)
- [補助資料：第三者のK3各hart cpuinfo](https://github.com/sanderjo/SpacemiT-K3-X100-A100/blob/7a75d9d634ff6cd6602ba6c296bbe821bf188e10/proc-cpuinfo.md)

## なぜLinuxの表示だけでは完全に断定できないか

CPUのISA情報は、DT・ファームウェア・カーネルの認識や公開方針にも依存する。
未記載の拡張が物理的に存在する可能性は残る。
また、Linuxアプリからseedを読んでillegal instructionになっても、
U-modeへのアクセス権がないだけかもしれず、ハードウェア非対応の証明にはならない。
rootでLinuxコマンドを実行してもM-modeにはならない。

OpenSBIはZkrを検出した場合だけSSEEDを設定する。
DTでZkrが宣言されない状態では、この条件付き処理を根拠に利用可とは言えない。

## 実機で確定する方法

まずベンダーのX100/A100のISA・TRNG/seed仕様で対応を確認する。
未記載でも実機診断をする場合は、OpenSBIのM-mode診断として実施する。

1. illegal instructionから復帰できる診断経路を用意する。
2. X100とA100の両群で、CSR 0x015へCSRRWでアクセスする。
   単純なCSRRS読み取りは、Zkr実装があっても仕様上illegalになり得るため使わない。
3. CSRアクセス成功と、OPSTのES16/WAIT/BIST/DEADを区別して記録する。
4. ES16が得られればそのhartでseed経路が動いた証拠になる。
   WAIT/BISTは準備待ち、DEADは故障状態で、利用可能な乱数が得られた証拠ではない。
5. 3001を全hartで利用するなら、最終的に全対象hartの結果を確認する。

診断はDTへzkrを追加するだけで代用しない。
この調査では実機M-mode命令は実行しておらず、上記の結果は未取得。

## SBI 3001計画への反映

- QEMU：既存Zkr/seed/crypto backendを使う案を継続する。
- 実機：Zkr必須構成にはせず、ベンダーのTRNG/Security Engine、
  M-modeサービスやRPMI側の乱数機能を調べ、checked providerへ接続する。
- ベンダーの仕様や実測でZkrが確認できた場合だけ、実機のZkr backendを有効にする。
- ISA名を追加するだけ、固定値、タイマー値、hash単独等で供給元を代用しない。

詳細は [SBI 3001移植計画](keystone-sbi-3001-plan.md) を参照。

Zkrのない機種に対する候補と選択方針は
[乱数供給元のfallback設計](keystone-random-fallback-design.md)を参照。

追加調査でZkrとは別のWTM RNGドライバをベンダーOP-TEEに確認した。
[Security Engine/RNGの利用案](jupiter2-security-engine-rng.md)を参照。
Zkr非対応という前提でも、実機向けの具体的な供給元候補が見つかっている。
