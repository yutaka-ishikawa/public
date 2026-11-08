# 実QEMUでのSBI 3002/3003連携verification

同じeapp/runtime/loaderでattestationとsealingを呼び、実OpenSBI/SMから返されたreportと
鍵を検査します。SMのC関数をホストで模擬実行する試験ではありません。
鍵そのものはOCALLで渡さず、独立計算したSHA3ダイジェストとenclave内で比較します。

## ビルドとインストール（ホスト）

既存のLinux/rootfsが動作している環境で、リポジトリ直下から実行します。

```bash
git pull
make build JOBS=4
# QEMUを正常shutdownしてから
make install
```

`make crypto` だけでも開発用firmware・SDK/Eyrie・専用パッケージをbuildできます。
対応する配置は `make install-crypto install-eyrie` です。
通常firmware（providerなし）も今回のソースで `make opensbi` / `make install-opensbi` してください。

既存のLinux/driverは再利用可能です。カーネル自体を変更しない場合、最小更新は次です。

```bash
make opensbi crypto JOBS=4
make -C verifications guest kernel-modules
make install-opensbi install-crypto install-eyrie
```

検証用.koは今回ioctl構造体が変わったので、対応する既存カーネルの完全なbuild出力で
再buildします。古い検証moduleは取り外すか、ゲストを再起動してください。
`make -C verifications kernel-modules` にはModule.symversが必要です。
SDKを別のbuild領域で作る場合は、各makeに同じ `KEYSTONE_BUILD_DIR=/absolute/path` を指定します。

## 起動と手動試験

ホスト:

```bash
bash scripts/run-k3.sh --crypto qemu-dev
```

ゲストのrootシェル:

```bash
mkdir -p /proc /sys /mnt/host
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host
bash /mnt/host/verifications/crypto/run-guest.sh qemu-dev
```

専用試験はAを2回（それぞれ明示DESTROY）、異なるmeasurementのBを1回実行します。
全既存suiteも同じ起動で実行するなら:

```bash
KEYSTONE_CRYPTO_EXPECT=qemu-dev bash /mnt/host/verifications/run-all.sh
```

通常の `--crypto` なし起動ではproviderがunavailableです。
その状態の `run-all.sh` は両SBIがNOT_IMPLEMENTEDを返し出力を変更しないことを検査します。
**unavailable試験のPASSは、attestation/sealingの正常系PASSではありません。**

## 自動起動・再起動試験（ホスト）

他のQEMUを終了してから:

```bash
python3 verifications/crypto/run-qemu.py
# 各bootの既存suiteと、最後に3001の7ケースも実行
python3 verifications/crypto/run-qemu.py --all
```

同じ開発identityで2回cold bootし、続いて通常unavailable版を起動します。
`--all` は3 bootの後に既存の3001乱数matrix（7ケース）も実行します。
raw rootfsに一時qcow2を重ね、終了後にoverlayを破棄します。
`results/manifest.json` と各serialログへ結果を保存します。
失敗時は理由とログ末尾を表示します。実行が遅い環境は `--timeout 600` を指定できます。
この自動runnerは標準の `install/keystone` を使用します。
別prefixの場合は手動起動し、run-guest.shの第2引数にゲストから参照可能なpublic metadataディレクトリを渡してください。

## 検査内容

- 3002: 0/1/7/8/32/1023/1024 bytes、binary data、nonceの一致、正確な1352-byte出力、canary保持。
- 外部pinによるdevice→SM証明書、独立SM hash、SDKがファイルから測定するenclave hash、enclave署名。
- report hash/signature/SM signature/device pin/SM期待hashの改変拒否。
- 短いreport、不正data_len、JSON負数のverifier拒否。
- full nonceを含む既に受理したreportの再送を、検証者のprotocol側で拒否。
  短いdataの境界試験を、完全なfreshness protocolの試験とは扱いません。
- 3003: binary/NUL identifier、長さ0/1/7/8/4095/4096、同一identifierの再現、異なるidentifierの分離。
- 独立HKDF-SHA3-512既知値への一致。秘密鍵の拡張形式・clampもKeystone方式に合わせる。
- 3002のSM公開鍵による3003署名検証、1 bit改変の署名拒否、192-byte出力とcapacity 191/193の処理。
- Eyrie検査: 不正VA、入力長上限、capacity不足。
- 専用runtimeからの実SBI PA probe: NULL、overflow、UTM、SM領域、SMでの上限超過の拒否と、入出力重複の成功。
  test syscall 1099は `CRYPTO_VERIFY` 専用runtimeだけに含み、通常Eyrieには含みません。
- hostからの3002/3003の拒否は `../sbi-errors` の実ecall moduleで検査。
- 各eappが終了したら明示DESTROY、Aの再作成とBの異なるmeasurementに対して各々の既知値を確認。

## 秘密鍵とmetadata

開発device rootの既定値はホストの
`${XDG_STATE_HOME:-$HOME/.local/state}/keystone-k3/qemu-dev.seed`。
32 bytes・0600で作成し、既存ファイルは自動再生成しません。
`KEYSTONE_CRYPTO_SEED_FILE=/private/path/seed` で変更できます。
元のseedを通常の9p共有へ置かないため、既定ではリポジトリ外のprivate stateへ保存します。

生成した開発用secret include/object/firmwareはbuildディレクトリ内に存在します。
開発firmwareから秘密を抽出できるため、実機のroot of trustやdevice endorsementではありません。
SM起動ログに出すmeasurement/公開鍵/証明書は公開データです。秘密seedやsealing keyは表示しません。

public metadataは `install/keystone/share/keystone/crypto/` に配置します。
SM測定manifest、device public pin、各eappの期待鍵digest、package SHA256を含みます。
パッケージを再buildした場合は `make crypto` / `make install-crypto` も再実行してください。
古い期待値なら、ゲストのSHA256事前検査が不一致を報告します。

## 確認済みと未確認

[initialization-results.json](initialization-results.json) は実QEMU cold bootの記録です。
2回のSM measurement一致・device→SM署名検証・identity一致、証明書改変の拒否と
unavailable版の初期化は確認済みです。これは3002/3003をenclaveから呼んだPASSではありません。
この作業環境には完全な対応Linux/rootfsがないため、上記ゲストsuiteの統合実行は未確認です。
hardware provider、実機boot/DMA隔離、部分的capability故障注入、全hart競合の網羅、
別root/SM imageへの自動rotation試験は未実装または未検証です。
