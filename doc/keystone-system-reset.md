# Keystone構成のshutdownでhart停止エラーが出る問題

## 症状と原因

ゲストの`shutdown -h now`でrootfsを読み取り専用に再マウントした後、次の表示になりQEMUが終了しない問題があった。Keystoneアプリを実行していない場合にも発生する。

```text
reboot: Power down
sbi_hsm_exit: ERR: The hart is in invalid state [0]
ERR: Failed stop hart [7]
```

ここで`Power down`はLinuxが電源断処理へ進んだ表示で、QEMUの終了完了ではない。hart番号は実行時に変わる。

原因は次の組み合わせだった。

1. K3 Keystone構成は、PMP更新の応答対象を維持するため、通常のHSM hart停止を`SBI_EDENIED`で拒否する。アプリの実行履歴とは関係ない。
2. 通常のOpenSBIシステム終了経路は、HALT IPIと自hartの停止要求の後でプラットフォームの電源断処理を呼ぶ。停止要求が拒否されても、最後のフォールバックで`sbi_exit()`へ進む。
3. `sbi_hsm_exit()`は`STOP_PENDING → STOPPED`を期待する。しかし停止が拒否されているので状態は`STARTED`（値0）のままであり、エラーになる。
4. QEMUのjupiter2は`syscon-poweroff`／`syscon-reboot`のデバイスツリーとSiFive testデバイスを提供するが、Keystone用OpenSBIのdefconfigでsyscon resetドライバが有効になっていなかった。そのため、QEMUを終了・再起動するデバイスが登録されなかった。

根本的には、全hartをオンラインに保つKeystoneのポリシーと、通常のOpenSBI終了経路が一致していなかった。

## 修正

`opensbi/platform/generic/configs/k3_keystone_audit_defconfig`で以下を有効にした。

```text
CONFIG_FDT_REGMAP=y
CONFIG_FDT_REGMAP_SYSCON=y
CONFIG_FDT_RESET_SYSCON=y
```

`opensbi/lib/sbi/sbi_system.c`のKeystone構成では、システム終了時にHSM STOPやHALT IPIを行わず、ドメインの`system_reset_allowed`を確認して、選択したプラットフォームの電源断・再起動デバイスを直接呼ぶようにした。通常運転中のHSM hart停止禁止は変更していない。

これはCPUの個別停止を許可する変更ではない。全マシンを終了・リセットするデバイスへ処理を渡す経路を分けたものである。電源断要求までほかのhartを停止させないため、PMP更新が途中だった場合でも、その応答対象を終了処理の途中で失わない。

電源断デバイスが存在しない、呼び出しが戻る、またはドメインがリセットを許可していない場合は、エラーを表示して`sbi_hart_hang()`で待機する。`STARTED`のまま`sbi_exit()`へ進まず、呼び出し元にも戻らない。このフォールバックは、デバイス自体が電源断できない問題を解決するものではない。

非Keystone構成の従来の終了順序は維持している。

## 既存ビルドでドライバが追加されない問題も修正

検証中、defconfigを更新しても生成済みの`fdt_reset_drivers.carray.c`が再生成されず、ドライバ一覧にsysconが追加されない問題も確認した。

`opensbi/Makefile`の3種類の`%.carray.c`生成規則に`$(KCONFIG_AUTOHEADER)`への依存を追加した。設定ヘッダーの更新時にドライバ一覧も再生成するため、既存のビルドディレクトリを削除せずに設定変更を反映できる。

## 更新方法

以前のQEMUプロセスを終了してから、ホスト側のリポジトリルートで実行する。

```bash
git pull
bash scripts/build-install-k3.sh --only firmware --prefix "$PWD/install/keystone"
bash scripts/run-k3.sh
```

別のインストール先を使う場合は、ビルドと起動で同じ`--prefix`を指定する。今回はファームウェアの修正なので、SDK・example・カーネル・rootfsの再ビルドは不要。

起動したゲストで確認する。

```bash
shutdown -h now
```

QEMUが終了してホスト側のシェルに戻ることを確認する。再起動は、別途ゲストを起動して`shutdown -r now`で確認する。

## 現在の検証方法

旧reset関数の模擬試験は削除した。現在の動作確認は上記の実ゲストでの
shutdown/reboot手順を使い、ファームウェアのビルド成功とは区別する。
過去のビルドではSM/OpenSBIのリンクとsyscon poweroff/rebootドライバの収録を
確認したが、それだけで実ゲストの電源断が成功したとは扱わない。
