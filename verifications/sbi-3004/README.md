# SBI 3004 STOPの実QEMU verification

`eapp.c`はenclave、`host.cpp`はゲストLinuxのrunner。
`Makefile`は専用Eyrie（STOP_VERIFY=ON）、native eapp、runnerを作成する。
SM・PMP・SBIの模擬処理は使わない。ソースレベルの確認は [source-review.md](source-review.md)。

## ビルドと配置（ホスト）

```bash
git pull
make build JOBS=4
# 起動中のQEMUを終了してから
make install
```

個別のパッケージ作成は `make -C verifications/sbi-3004 guest`。
SDKには新しいobserver APIを追加したため、古いSDKヘッダ／archiveとは組み合わせない。
通常firmwareとcrypto/RNG用firmwareも今回のSMから再ビルドする。
`make build`はこれらと新しい検証モジュールをまとめて再ビルドする。

S-modeからの3004拒否を調べるsbi-errorsのioctl構造体は8件から9件へ拡張した。
古い検証モジュールと新しいtest.cは組み合わせない。QEMUを再起動して試験する。

## 実行

ホストから全suiteを自動実行：

```bash
python3 verifications/crypto/run-qemu.py --all
```

STOP専用を手動実行する場合、ホストで `bash scripts/run-k3.sh` を起動し、
QEMUゲストのrootシェルで：

```bash
mount-hostshare
cd /mnt/host/verifications/sbi-3004
bash run-guest.sh
echo "exit=$?"
```

ホスト権限の拒否試験も個別に実行する場合は、ゲスト内で
`bash /mnt/host/verifications/sbi-errors/run-guest.sh`。

## 判定する条件

- request=2、3、UINT64_MAXはILLEGAL_ARGUMENT=100008を返し、同じenclaveで実行を継続。
  2は旧名STOP_EXIT_ENCLAVEのdefineがあるが、STOPの有効な要求にはしない。終了には3006を使う。
- request=0を専用syscallから3回発行し、LinuxがINTERRUPTED=100002を受けてRESUMEする。
- 9回の通常OCALLはrequest=1でEDGE_CALL_HOST=100011となり、応答データも一致する。
  不要な追加OCALLやUNKNOWN_ERRORを成功として扱わない。
- timerを設定し、明示STOPもOCALLも発行しないU-mode待機区間で、少なくとも3回の
  非同期INTERRUPTED→RESUMEをLinux runnerのobserverで検出する。
- 再開後のstack／global canary、順序番号、終了値0、明示DESTROYを確認する。

非同期statusだけではM_TIMERとM_SOFTを区別できない。M_TIMER原因まで確認するには、
ホストでtrace版もビルドして起動する：

```bash
KEYSTONE_SM_TRACE=1 make opensbi
make install-opensbi
bash scripts/run-k3.sh
```

trace `STOP asynchronous cause a=7` がM_TIMER、`a=3`がM_SOFT。
同じtrace設定で再ビルドしていない別crypto/RNG版のfirmwareを選ばないこと。
専用試験だけのPASSで全hartのtimer／IPI原因を網羅したとはしない。
待機時間はtimebaseの4,000,000 ticksで、QEMU K3のtimebaseを想定する。

## RuntimeとSDKの変更範囲

専用runtimeのsyscall 1100：operation=0はtimerを停止して指定requestを実SMへ発行、
operation=1は実SBI timerを設定する。通常runtimeにはこのsyscallを入れない。
SMの割込み経路をEyrie側で模擬しない。

SDKの `registerStopDispatch` はEdgeCallHost／EnclaveInterruptedを観測する任意callback。
従来のrun()によるOCALL dispatchと自動RESUMEは維持する。
callbackは短い観測処理に使い、enclaveのdestroy／run／resumeを再入呼出ししない。

## 実施済みと未確認

クラウドでSDK／通常runtime／専用runtime／全verificationパッケージと
SM/OpenSBI firmwareをビルドした。実Linuxゲストからの本試験の実行は、対応Image/rootfsと
完全なModule.symversがないため未確認。過去の全PASSはこの新試験のPASSではない。
ユーザー環境で上記コマンドを実行し、serialログとmanifestを保存する。
