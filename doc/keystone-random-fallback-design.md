# SBI 3001：Zkr非対応機の乱数供給案

作成日：2026-11-08。設計案であり、以下のproviderやDRBGを実装済みという意味ではない。

## 後続の採用設計

ユーザー指定の [3方式設計](keystone-sbi-3001-three-backends.md) に更新した。
③としてcounterを使う開発用生成器を明示的に実装する。
本書のDRBG/boot seed案は将来の候補で、今回の実装要件ではない。
本書の「供給元なしではエラー」は③を選ばない場合の暗号学的乱数の方針を指し、
③の機能試験を禁止するものではない。

## 先行提案

SBI 3001のABIは全機種で共通にし、乱数の供給元だけをplatform providerで切り替える。
Zkrが利用可能なら優先し、非対応機ではTRNGまたは信頼できるファームウェアサービスを使う。
連続して使える供給元がなくても、信頼できる起動時seedがある機種にはDRBG経路を用意する。

供給元と生成器を分ける。

```text
Zkr / MMIO TRNG / 信頼できるfirmware乱数サービス / SM専用boot seed
  → 機種別entropy provider
  → SM内の共通DRBG（暗号学的な疑似乱数生成器）
  → SBI 3001：a0=status、a1=乱数値
  → Eyrie → getrandom / AT_RANDOM
```

この共通DRBG構成を最終形の推奨案とする。
先行するQEMU移植計画の「ES16を4回連結して64 bit値にする」案は、
QEMUの既存crypto backendを確認する最小経路として残せるが、
実機と共通化する段階では上記のentropy/生成器分離へ整理する。
生のentropy出力に必要なconditioningは供給元の仕様に従う。

## 供給元の優先順位

| 優先 | 供給元 | 適用条件 | 追加作業 |
| --- | --- | --- | --- |
| 1 | Zkr / seed CSR | 検出だけでなく、実際に有効データを取得できる。 | CSRRW、OPST、有限待機。 |
| 2 | SoCのMMIO TRNG / Security Engine | M-modeから利用でき、品質・初期化・エラー処理の仕様がある。 | ベンダーの既存実装を参考に、小さいSM/OpenSBI providerへ接続。 |
| 3 | 信頼できる管理firmwareの乱数サービス | 既存OpenSBI/RPMI/管理プロセッサ側が乱数を供給できる。 | 既存transportを使う。サービスの存在と共有bufferの扱いを確認。 |
| 4 | 信頼できるbootloaderのSM専用seed | 起動ごとに新しく、Linuxへ公開されず、十分なentropyがある。 | SM初期化時に取り込み、DRBGを初期化。元seedを消去。 |
| なし | 上記の供給元がない | 暗号学的な乱数の初期化ができない。 | 未対応/供給不能を返す。 |

2と3の順序は、機種で既に動いている経路を優先して入れ替えてよい。
既存firmwareに利用可能なサービスがあれば、MMIOドライバを新しく書く必要はない。
OpenSBIのtimer/IPI/hashを組み合わせるだけでは、新しいentropy供給元にはならない。

## A. TRNG / Security Engineを使う案

Jupiter 2ではベンダーOP-TEEのK3専用WTM RNGドライバを確認した。
[具体的な実装と利用案](jupiter2-security-engine-rng.md)を第一候補とする。
ベンダーのTRNG/RNG実装、MMIOアドレス、clock/reset、ready/error処理を探す。
Linuxに既存ドライバがあればレジスタ処理は参考にできるが、
M-modeからLinuxのgetrandom関数やLinuxデバイスドライバを直接呼ぶ方式にはしない。

ハードウェア側でconditioning済みか、生entropyかを確認し、DRBGの初期化と再seedへ渡す。
Linuxと共有するデバイスなら、同時アクセス・reset・初期化の所有を決める。
供給元の故障と、既に初期化済みのDRBGが規定内で生成できるかを別々に管理する。

## B. 既存管理firmwareサービスを使う案

M-modeから呼べる既存乱数サービスがあれば、そのABIとtransportを再利用する。
RPMI等の名前があることだけで乱数サービスがあるとは判断しない。
現時点でJupiter 2に利用可能な乱数サービスを確認できたわけではない。

サービスが管理するTRNGや暗号学的乱数を取得し、SM内のDRBGへ投入する。
応答の成功/供給待ち/故障、要求サイズ、timeout、共有bufferを誰が変更・参照できるかを
確認する。SMの秘密状態をLinuxの共有bufferへ置かない。

## C. 起動時seed＋DRBGを使う案

連続TRNGアクセスがなくても、信頼できる前段firmwareが十分なentropyを供給できる機種で使う。
seedの供給元には、前段で利用できるTRNG、TPM、Secure Element等が考えられる。

1. Linuxへ入る前に、SM専用のfresh seedを受け取る。
2. SMの保護領域へ取り込み、既存DRBG実装を初期化する。
3. 引き渡しbufferやDT内などに残るseed本体・コピーを消去する。
4. 以後の3001にはDRBGから出力する。
5. 規定の生成量・回数に達したら再seedする。供給元がなく再seedできなければエラーにする。

起動ごとのseedやnonceが再利用されると出力も繰り返し得るため、
固定seed、時刻やhart IDだけから作ったseed、全機種共通seedは使わない。
checkpointやsnapshotの復元時も、同じDRBG状態をそのまま再利用する条件を整理する。

このリポジトリの一部K3 DTSには固定 `rng-seed` が記載されている。
これをそのままSMのseedに使う案は採用しない。
実際のbootloaderが上書きしたfresh seedの有無とは区別して調査する。
Linuxへ渡すseedとSM専用seedも分離する。

## DRBGの実装方針

- 新規の暗号方式を自作せず、既存のHMAC_DRBG/SHA-256等の実装を選定する。
  例としてMbed TLS系が候補だが、M-modeでの静的メモリ構成・依存・ライセンスを確認して決める。
- 現リポジトリのOpenSBI/SMに汎用DRBG APIは見当たらないため、
  この経路には既存ライブラリの取り込み・接続という追加作業がある。
- 既存SHA3/HKDFがあることと、DRBGが実装済みであることは別。
- 必要なentropy量、nonce、personalization、再seed間隔は選定実装の仕様に従う。
  単に32 bytesを得たというだけで256 bitのentropyがあると決めつけない。
- 最初はSM内の共有状態と短いlockで全hartから利用する。
  待ち続けるentropy取得とDRBG状態更新を分け、無限待機しない。
- X100/A100等でZkrの有無が異なる場合も、非対応hartでseed CSRを実行しない。
  再seedを行う対応hart/共有TRNGを選び、生成済みDRBGは全対象hartから使えるようにする。

## Linuxの乱数を利用する案の位置づけ

Linuxのgetrandomやvirtio-rngは、Linuxを信頼するシステムでは利用候補になる。
ただし、KeystoneでLinuxから隔離した乱数を必要とする場合、Linuxが与えた値だけを
元にしても、Linuxから未知の乱数になる保証はない。

そのため、通常プロファイルの自動fallbackにはしない。
OSを信頼する開発専用プロファイルとして採用する場合は、seed投入やOCALLの経路を
別途明示し、ログ・試験結果にもOS由来と記録する。
M-modeの3001処理からLinux関数を直接呼ぶことはしない。

## 供給元の選択と故障時の動作

起動時に候補の能力と状態を確認し、選択したproviderを記録する。
「Zkr命令がある」だけで選択を完了せず、初期seedが得られることを確認する。

動作中のZkr DEAD等で、タイマーや固定値へ黙って切り替える処理は入れない。
別の設定済みの信頼できる供給元が正常なら再seed先を切り替え、なければ規定の再seed限界で失敗する。
DRBG初期化前は3001を成功にしない。再seed時の一時待ちと永久故障も区別する。

Eyrieはa0のstatusを必ず確認し、getrandom/AT_RANDOMへ伝播させる。
供給元が変わってもSBI 3001のABIとアプリ側の呼び方は変えない。

## 段階的な実装案

1. QEMUのZkr経路とEyrieのchecked APIを先に完成する。
2. 実機用のTRNG/管理firmwareサービスを調査し、既存機能があれば接続する。
3. 共通DRBGとboot seedの取り込みを追加し、Zkrのない機種にも同じAPIで対応する。
4. Zkrあり、Zkrなし＋別供給元、全供給元なしの構成を実QEMU/実機で試験する。

QEMUでZkrを無効にして代替経路を検証する場合は、実装したRNGデバイスや
実際のboot seed受け渡し経路を使う。模擬SBI/CSRの試験には戻さない。

Jupiter 2ではAのWTM RNGドライバの存在を確認した。実機動作・初期seed/reseed仕様、
Bの呼び出し可否、Cのfresh seedはまだ未確認。
Zkrが見つからないことを理由に、未確認のTRNG/RPMIサービスを存在するものとして扱わない。
