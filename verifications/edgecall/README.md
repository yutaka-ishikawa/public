# 実enclaveのデータ転送・STOP/RESUME・DESTROY試験

この試験はゲストの実SDK・Linuxドライバ・OpenSBI・Keystone SM・Eyrieを使う。
製品関数を取り込んだ模擬テストは削除した。

## ビルドと実行

ホストのリポジトリルートで：

```bash
bash scripts/build-install-keystone.sh --only sdk
make -C verifications guest
```

ゲストで正常経路だけを実行する場合：

```bash
mount-hostshare
cd /mnt/host/verifications/edgecall
bash run-guest.sh --data-only
```

省略なしの `bash run-guest.sh` は対応する `kernel/edgecall_pmp_probe.ko` を必要とする。
全体のビルド・モジュール準備は [統合試験手順](../README.md) を参照。

`run-guest.sh` はproc/sysを準備し、未ロードのKeystoneドライバを自動ロードする。
既存イメージのドライバが古い場合は、事前に修正版へ更新する。

## 確認内容

- 実CREATE/RUNでenclaveへ入る。
- 0、1、7、8、31、255、256、4096 bytesのNULを含むデータをOCALLへ渡す。
- runnerが各バイトを検査しXOR変換してUTMへ返す。
- 実STOP/RESUMEを経てenclaveが内容、元のprivate入力、canaryを照合する。
- 不正なreturn offsetを返すとruntimeが拒否する。
- 実EXITで終了し、runnerは終了値とcallback数を確認する。
- 明示的DESTROYの成功を検査する。SDKは解放後のIDを無効化し、
  destructorでDESTROYを重複発行しない。

省略なしの場合、OCALL中にカーネルprobeが次を検査する。

- 通常のカーネルRAMを読める。
- UTMの同じ共有データを読める。
- EPM内の元の物理ページをRAM direct mapから読むと `-EFAULT` になる。

privateポインタをデータとして渡すことと、private内容をUTMへコピーすることは別。
後者の内容をrunnerが読めるのは意図した動作で、前者の元EPMが読めないことは
実PMP probeで確認する。`--data-only` では実アクセス拒否は判定しない。

## 256 MiB設定

ルートの `make -C verifications guest` は、別出力先に2種類を作る。

| 出力 | free memory |
| --- | ---: |
| `build/guest/edgecall.ke` | 2 MiB |
| `build/guest-256/edgecall.ke` | 256 MiB |

256 MiB版だけをゲストで実行する場合：

```bash
EDGE_VERIFY_PACKAGE=build/guest-256/edgecall.ke bash run-guest.sh --data-only
```

単一出力先でビルド設定を切り替える際はcleanして作り直す。
大容量の前提は [EPM・CMAの設定](../../doc/keystone-large-epm.md) を参照。
256 MiB設定はその容量のEPMでSM実行する試験であり、全256 MiBをenclaveから
読み書きするメモリ診断ではない。

## 結果

旧版の2 MiB設定でユーザーからOCALL往復PASSの報告がある。
今回追加した明示的DESTROYと256 MiB版、実PMP拒否の新しい結果は未確認。
ビルド成功とゲスト実行成功を分けて記録する。
