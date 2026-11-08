# SBI 3001実装とQEMU検証環境

## ABIと経路

Keystone拡張 `0x08424b45`、FID `3001`。a0は正のKeystone status、a1は64ビット値です。
成功はstatus=0で、値0も成功です。失敗時のa1は0です。
Linuxホストからの呼び出しは100014（PROHIBITED）、enclaveのEyrieからだけ許可します。
OpenSBI共通dispatcherに正のstatusを変換させず、既存Keystone専用return処理を使います。
3002（attestation）と3003（sealing）は引き続き未実装です。

経路は `eapp getrandom → Eyrie sbi_random_checked → ecall → OpenSBI adapter →
sbi_sm_random → keystone_rng_read64 → backend` です。

`keystone/sm/src/rng/rng.c` に選択、read64、エラー処理を集約しました。
`KEYSTONE_RNG_BACKEND=auto|zkr|jupiter2|counter` をビルド時に指定します。
autoはZkr→WTM DTノード→counterの順で選択します。存在するハードウェアの
初期化・読み出しが失敗した場合はエラーを返し、別方式へ退避しません。
初期化で検出したエラーはその起動中保持します。spinlockで取得を直列化します。
初期化失敗でenclaveのPMP機能まで無効にすることはありません。

## 3方式

### Zkr

OpenSBIのhart拡張検出を再利用します。seed CSR 0x015は `csr_swap`（CSRRW）で
読み出し、ES16を4回集めて64ビットにします。CSRRSによる単なる読み出しは使いません。
WAIT/BIST待ちは最大10000試行、期限超過は100101、DEADは100102です。
拡張がなければCSRに触れず100100を返します。QEMU既存のZkr実装を使います。

### Jupiter2 WTM

SpacemiT公式OP-TEE K3実装の `k3_prng.c` と `k3_ccu.c` を参考に、
同じCPU MMIOアクセス方式をSM側に実装しました。OP-TEE自体を呼び出す方式や
セキュアプロセッサとのmailbox RPCではありません。

参照コミット: `spacemit-com/optee_os` のK3系
`2fbd6ae54d89ff8621cdc1b5379c6bbe6f2d3c59`。
[調査記録](jupiter2-security-engine-rng.md) も参照してください。

- WTMマップ `0xf0703000/0x1000`、RNGはその中のoffset `0x800`。
- PMUMAIN `0xd4050000+0x1024` のbit5を有効化。
- PMUAP `0xd4282000+0x868` にvendor値 `0x1cff003f` を設定し、readback。
- PRNG設定、enable、ready待ち、FIFO clear、32ビットDATAを2回取得。
- 待ちは10000回または利用可能なtimerで約100ms。期限超過は100101。

有効な `compatible="spacemit,k3-wtm-rng"` と既知のreg範囲を持つDTノードが必要です。
QEMUでは `--rng jupiter2` がこのノードとMMIO/clockモデルを追加します。
実機の既存DTにこの独自ノードがあるとは限りません。実機適用にはDT追加と
Linux/OP-TEEとのclock・RNG所有権調整が必要で、今回実機では確認していません。

QEMUモデルはCPUアクセスで使用するレジスタの部分モデルです。データ源は
QEMUの `qemu_guest_getrandom`。DMAやTE200全体、PRNG内部のseed/reseedはモデル化しません。
`not-ready` と `zero` を選択してエラーと有効な0値を検証できます。
実機のセキュアプロセッサの信頼性や暗号品質をQEMU結果から推定できません。

### Counter（他機種向け開発用）

`platform_rng_read_counter` と `platform_rng_get_ops` はweakの移植用インターフェイスです。
標準実装はOpenSBI timer APIで得る値、hart ID、起動内のsequenceを既存SHA3で混合します。
Zicntrまたはtimer_value providerがなければ100100にします。
無条件にrdcycle/rdtimeを実行する実装ではありません。

これは暗号学的なエントロピー源ではありません。ハッシュはエントロピーを追加しません。
起動ログは `quality=development` と表示します。将来の機種はopsまたはcounter hookを
置き換え、失敗時はstatusを返す契約を維持してください。

## Eyrieの累積変更

- `sbi_random_checked` はa0とa1の両方を取得。失敗時に出力を0にします。
- 既存 `sbi_random()` はエラーでenclaveを終了し、statusを乱数に流用しません。
- `rt_util_getrandom` は8バイト単位で取得し、最終端数だけコピーします。
  未整列uintptr_tポインタへの直接書き込みを廃止しました。
- getrandomはflags、長さ、アドレスoverflow、user/writable PTEを確認します。
- 100100→ENOSYS(-38)、100101→EAGAIN(-11)、その他RNG異常→EIO(-5)。
  途中取得済みなら取得済みバイト数を返します。コピー失敗はEFAULT(-14)。
- `AT_RANDOM` は従来の16バイト取得数チェックにエラーを伝播し、起動を中止します。
- page swapの鍵・counterページ初期化も、要求バイト数を取得できなければ終了します。

getrandomのblocking/nonblockingの違いによる無期限待ちは実装しません。
両方とも有限待ち後にEAGAINを返します。暗号利用の可否は選択したbackendの品質に依存します。

## ビルド・起動・実行

[検証README](../verifications/random/README.md) にコマンドと7ケースの一覧を記載しました。
`--rng` は方式別ファームウェアを選び、同時にCPU ZkrとWTMモデルを設定します。
`--bios` を明示した場合はそのファイルが優先されるので、組み合わせに注意してください。
`--rng auto` は従来の標準firmwareパスを使用します。autoを使う場合は標準firmwareも
`bash scripts/build-install-k3.sh --only firmware --prefix "$PWD/install/keystone"`
で今回のソースからビルドしてください。方式別ビルドは標準firmwareを置換しません。`--rng` を省略した既存起動は従来どおりです。

## 今回の検証結果と限界

- QEMU riscv64 system emulator、qemu-img: ビルド成功。
- Zkr/Jupiter2/counterの3つのSMとOpenSBI: コンパイル・リンク成功。
- SDK、ENV_SETUP有効のEyrie、検証専用Eyrie、native/error/libc eappとrunner:
  クロスビルド成功。3001のhost禁止チェックを追加した検証モジュールも
  カーネルオブジェクトをコンパイル済み（対応するModule.symversがないため.koリンクは未実施）。
- 実QEMU CPUでOpenSBI/SMを起動し、初期化結果を確認:

| 起動設定 | SMのselected | status |
| --- | --- | --- |
| Zkrあり | zkr | 0 |
| WTMあり | jupiter2 | 0 |
| counter | counter | 0 |
| Zkrなしを強制 | zkr | 100100 |
| WTMなしを強制 | jupiter2 | 100100 |
| WTM not-ready | jupiter2 | 100101 |
| WTM zero | jupiter2 | 0 |

各ケースのfirmware/QEMU SHA256と実際の起動ログの該当行は
[初期化結果JSON](../verifications/random/initialization-results.json) に保存しました。

上記は実際のCSR/MMIO/timerを使うSM初期化確認です。SBI 3001をenclaveから
発行した結果ではありません。Linux Image/rootfsと対応ドライバがこの作業環境に揃っていないため、
`run-qemu-matrix.py` とゲスト内 `run-guest.sh` の統合実行は未確認です。
ビルド成功と初期化成功をゲストのPASSとは扱いません。
Zkr WAIT/DEADの故障注入、WTM clock readback失敗、counter provider不存在の
異常分岐は今回のQEMUマトリクスでは再現していません。
実機Jupiter2での実行、乱数品質、所有権・DMA隔離も未確認です。
