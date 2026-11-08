# 256 MiB EPMのCREATEがPMP_FAILUREになる原因と修正

## 実ゲストでの観測

ユーザーのQEMUゲストで2 MiB設定のenclave試験後、256 MiB設定で次を観測した。

```text
EPM allocated: 268656640 bytes at 104f00000 (DMA/CMA)
pmp_set_global:PMP global set unavailable or incomplete
keystone_create_enclave: SBI call failed with error code 100003
```

LinuxによるCMA確保は成功している。100003はSMのPMP_FAILURE。
SM CREATEが全hartへEPMのPMP設定を行う段階で失敗し、enclaveのRUNには達していない。
CMA予約やrootfsの不足ではない。

## ソースから確認した原因

今回のサイズ268656640は256 MiBにプログラム等を加えた値で、2の冪ではない。
Keystoneの `pmp_region_init()` は、サイズが2の冪で開始アドレスもそのサイズに
整列していればNAPOT、それ以外はTORを選ぶ。

- 既に成功した4 MiB buddy領域はNAPOTで表せる。
- 今回のCMA領域は要求ページ数どおりなので、2個のPMPエントリでTORを表す。

`k3_pmp_apply_verified()` は設定後のCSRを読み戻してACKする関数だが、
旧コードには次の無条件拒否があった。

```c
if (regions[region].addrmode == PMP_A_TOR) return -1;
```

理由はOpenSBIの `pmp_get()` がTORの下限を読み出して区間全体を検証できないこと。
同APIはNAPOT以外でpmpaddr単体と固定orderを返すため、任意のTOR区間を
NAPOTのサイズ比較で代用することもできない。

全hart設定の途中でこの拒否に入り、送信側がPMP更新失敗として扱った。
IPIの故障や空きメモリ不足を示すログではない。
失敗後の同期状態にはpoisonや停止したhartが残り得るため、
同じゲストで再実行を続けず、新しいファームウェアで起動し直す。

## 修正

TORも実際のpmpaddr/pmpcfg CSRから読み戻して検証する。
検証を省略して成功扱いにする変更ではない。

1. 上側エントリのmodeがTOR、権限が要求値、lock等の余計なbitがないこと。
2. 下側エントリがOFF、権限・lockなしであること。
3. 下側pmpaddrが開始アドレス/4、上側が終了アドレス/4に一致すること。
4. エントリ0のTORでは下限が暗黙の0であること。
5. UNSET後は上下のエントリがOFFになったこと。
6. CSR更新後のfence/sfenceと、全対象hartの世代ACKを維持する。

ページ境界の開始・終了アドレスを用いるので、現在のPMP granularity=16の
WARL low bitsとも一致する。NAPOTと全アドレスNAPOTの検証は維持する。

`run-all.sh` は2 MiB版と256 MiB版を2周実行し、DESTROY後に再作成する。
TORのSETだけでなくUNSET後の資源再利用も実enclave経路で確認する。
模擬試験は追加していない。

## 更新と再実行

ホストのリポジトリルートで：

```bash
git pull
bash scripts/build-install-k3.sh --only firmware --prefix "$PWD/install/keystone"
```

今回の製品修正はSMだけで、SDK・Linuxドライバ・カーネル・rootfsの再ビルドは不要。
実行スクリプトは共有リポジトリの更新で反映される。
稼働中のQEMUにはファームウェアは反映されない。
古いQEMUを終了し、修正版を指定して新しく起動する。

```bash
bash scripts/run-k3.sh
```

ゲストで、既に統合試験のビルド・検証モジュールを用意している場合：

```bash
mount-hostshare
cd /mnt/host/verifications
bash run-all.sh
echo "exit=$?"
```

PMP直接参照試験を省く場合だけ `bash run-all.sh --data-only` とする。
どちらもTORの設定・全hart同期、実enclave実行、DESTROYを通る。
手順全体は [統合試験](../verifications/README.md) を参照。

## 確認した範囲

SM/OpenSBIファームウェアのコンパイル・リンク成功を確認する。
この環境には起動用Linux Imageと稼働中K3ゲストがないため、
修正後の実ゲストの256 MiB試験は未確認。
上記のエラー原因をソースから特定したことと、修正後の実行成功を区別する。
