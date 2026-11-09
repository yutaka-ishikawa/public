# FID 4000 検証結果

2026-11-09（日本時間）、クラウド環境の実K3 system QEMUで実行。
[入力hash・実行manifest](evidence/linux-results.json)と同ディレクトリのassertion logを保存した。
実装前のユーザー全PASS報告とは別の、今回追加した実装に対する試験である。

| 設定 | 実行 | 結果 |
| --- | --- | --- |
| multimem有効・Sstc on | qemu-dev 2起動＋通常unavailable 1起動、各起動で全suite | PASS |
| multimem有効・Sstc off | 同上 | PASS |
| RNG回帰・Sstc on/off | 各7ケース：Zkr、WTM、counter、Zkr不在、WTM不在/not-ready/zero | 14/14 PASS |
| multimem無効・Sstc on/off | 各1起動、4000/2006拒否と旧CREATEの継続 | 2/2 PASS |

## 新規4000で確認したこと

- hostからの4000拒否、未知plugin/call、予約引数の拒否。
- 2006のversion/bytes、0・不整列・overflow・EPM/UTM重複の拒否。
- 別enclaveが所有する実backing PAを2006へ渡した際のSMの重複登録拒否。
- SDK/driverから得たPA・実長と4000の照会結果の完全一致。
- private EPM外のUTM、NULL、不整列、overflow、書込み不可app rodataへの出力拒否。
- 2 enclaveを同時に保持し交互にRUN、3 OCALL/RESUMEとtimer中断、CPU affinity切替。
  生成・破棄を3反復してdriverのbacking保持量が初期値へ戻ること。
- 256 MiB + 4 KiB（268439552 bytes）のCMA/TOR backingも登録・照会・解除・回収。
- 未登録、file所有権違反、不正IOCTL、二重割当の拒否。
- FINALIZEのSM CREATE失敗後close、SDK destructorを呼ばない子プロセス終了後の回収。
- 既存の実EPM/PMP隔離・edgecall・SBI error・crypto・STOP/timer・大容量EPM試験もPASS。

## 環境と証拠の扱い

Linux Imageと一致するビルドの`vmlinux.symvers`をKBUILD_EXTRA_SYMBOLSとして使い、
driver/probeをビルドした。未解決symbolの許容、force insmod、検査の無効化は使わない。
更新driverを別の試験用rootfsへ配置し、各QEMUは一時qcow2 overlayから起動した。
標準rootfsにも同じdriverを反映済み。

manifestは試験時の作業ツリー基準commitと対象ソースのhash、firmware/kernel/QEMU/
package等のhash、実行コマンド、guest statusを記録する。試験は実装commit作成前に行い、
対応する確定commitは移植計画に記録する。生serial logはクラウドの`.build-k3/`に保持し、
公開証跡にはそのhashと抽出したassertion logを置いた。秘密鍵・seedは含めない。

再ビルド途中のcrypto packageと旧package-sha256.txtの組合せでは検査が失敗したため、
公開metadataを生成し直した。表は整合した成果物での最終再実行だけを示す。
開発中のPMP解除不具合と修正はKSB-034に記録した。

## 限界

paging、実backingへのevict/swap-in、PAGE_HASH/PAGE_CRYPTOは未統合。
実機、全hart固定の網羅、同時実行競合、PMP/IPI失敗注入、DMA隔離も未確認。
driverのbacking保持量回復はSDK全体のhost資源解放の証明ではなく、KSB-035は未修正。
