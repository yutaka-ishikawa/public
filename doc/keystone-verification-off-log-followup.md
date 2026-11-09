# Sstc off失敗ログの調査と展開処理の修正

展開処理・診断の修正コミット：[bb84fa987](https://github.com/yutaka-ishikawa/public/commit/bb84fa987)。

2026-11-09 UTC。ユーザーから `run-qemu.py --all --sstc off` の失敗ログを受領した。
前回の構成未指定の全PASS報告と区別する。

## 受領したログ

| ケース | 結果 |
| --- | --- |
| qemu-dev boot1/boot2 | 3004の通常timer区間は43回の中断、取消し後は0回。exit=20。 |
| unavailable | 通常timer区間が0回。exit=18。 |
| RNG zkr | MD5確認後、gzip unexpected end of fileで展開失敗。SBI実行前。 |
| RNG zkr-absent | MD5不一致で展開失敗。SBI実行前。 |
| RNGの他5ケース | PASS。 |

失敗した3 bootでは後続のcrypto/edgecallは未到達である。
通常timer区間の43回はSMの独立期限が機能した証拠にはならない。
取消し後も中断することを検査する既存の条件は維持し、閾値を下げたりSKIPへ変更したりしない。

## 展開の原因と修正（KSB-029）

makeselfのMS_ddはpipeから `dd ibs=1024 count=N` で読む。
countは入力レコード数なので、短いreadも1回として数え、必要なバイト数より前に終了できる。
checksum確認と展開は別々のreadのため、前者が通っても後者が途中で切れる場合がある。
ゲストローカルへpackageをcopyするだけでは、このpipe内の短読取りを防げない。

`verifications/extract-package.sh`で次の順に処理するよう、4種類のguest runnerを変更した。

1. 共有上のpackageをローカルsnapshotへcopyする。
2. 単一payloadのheader行数・圧縮長・MD5を検査し、ファイル全体の長さを照合する。
3. `tail -c`でEOFまでpayloadをローカルファイルへ取り出す。pipeの固定read回数で切らない。
4. MD5、存在する場合のSHA256、gzip整合性を検査する。
5. 検査した同じファイルをtarで展開する。packageのshell本体は実行しない。

checksum・長さの不一致は失敗のままであり、壊れたpackageを成功扱いしない。
対象はこのリポジトリが生成する単一gzip payloadのmakeself形式。未対応形式は拒否する。

実ddの短いreadを入力／出力のhandshakeで再現し、32回のreadが32KiBにならないことを確認した。
archiveの正常抽出、破損・切断・SHA256不一致の拒否も試験した。
このtool regressionはSMの模擬試験ではなく、SBI機能のPASSにも数えない。

```bash
python3 scripts/tests/test-verification-package.py
```

## timer失敗の切り分け（KSB-031）

今回の添付にはboot全体・firmware hashがなく、使用artifactとの一致は未確定。
**提供された環境の取消し後0回の原因を、古いfirmwareと断定していない。**

新しいLinux環境を作成して実行したところ、現行のtimerコードでoffの失敗は再現しなかった。
SM timerの実装自体は今回変更していない。
暗号版の通常／取消し区間は約15～17回中断し、両区間の既存判定を通った。

自動runnerへ次の診断を追加した。

- boot前に選択したBINが現行timer共存の表示文字列を含むか検査する。
  不一致なら対象のpathとSHA256、build/installの案内を出して停止する。
  文字列検査だけでcommitの完全一致を保証するものではない。
- `[SM/TIMER]`、provider、boot ISA、Linuxのtimer情報を端末にも表示する。
- boot前に当該serial logを初期化し、事前検査失敗時に前回のPASSを再表示しない。
- install時のverification成果物検査にSTOP専用packageを追加する。

## 実Linuxでの確認

同梱linux-k3のK3 configからImageをbuild。対応するvmlinux.symversを
KBUILD_EXTRA_SYMBOLSに指定し、driverとSBI/PMP検証モジュールをlinkした。
未解決symbolの検査を無効化するオプションやforce insmodは使用していない。
Bianbu archiveからrootfsを作り、実guestで各moduleをロードした。
未使用の全kernel moduleのbuild完了とは区別する。

以下は実Linux→driver→OpenSBI→SM→Eyrie/enclaveでの結果である。

| Profile | crypto 2 boot + unavailable | RNG 7ケース | 通常firmwareの追加確認 |
| --- | --- | --- | --- |
| sstc off | 全suite PASS | 全7件PASS | trace=0で全suite PASS |
| sstc on | 全suite PASS | 全7件PASS | trace=0で全suite PASS |

全suiteにはSBI異常応答、11 OCALLのSTOP専用試験、host sleep/wakeup、crypto、
2/256 MiB構成のedgecallとPMP参照拒否が含まれる。
trace=1ではserial表示が割り込まれて中断数を読めない行もあるが、
終了値0とguestのassertion、trace=0の追加実行で確認した。

hashと結果は [Linux実行結果JSON](../verifications/sbi-3004/evidence/linux-on-off-results.json)。
これはクラウドの新構成での成功であり、提供された環境のartifact一致や実機の成功ではない。
元のtimer失敗はKSB-031で照合待ちとして残す。

## 再確認の手順

```bash
git pull
make build JOBS=4
# 起動中のQEMUを正常終了してから
make install
python3 verifications/crypto/run-qemu.py --all --sstc off \
    --logs verifications/crypto/results/sstc-off
python3 verifications/crypto/run-qemu.py --all --sstc on \
    --logs verifications/crypto/results/sstc-on
```

失敗が続く場合は、新表示の `[SM/TIMER]` 行と各manifestのfirmware SHA256で
今回の実行結果と照合する。test中にpackageやfirmwareをbuild/installして書き換えない。
