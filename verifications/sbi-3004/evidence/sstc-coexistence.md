# Sstc共存の実QEMU観測記録

2026-11-09 UTC。Linuxを使わない調査用S-mode runnerから実SM・Eyrie・native eappを起動。
eappは専用runtimeのsyscall 1100 operation=2でguest timerを取り消してからrdtimeで待機した。
ホストはRUN前に期限を設定し、EXIT後に期限切れSTIPを確認した。
Sstc onではS-modeでstimecmpの元の絶対値への復元も直接確認した。
Linux driver、nanosleep、11 OCALLの全suite実行を代替する結果ではない。

firmware ELF SHA256: `2b693fc4464bb7de4a9b794c3ef515c6ab96d92f093f3227ecd6ef58fcd273e5`

## Sstc on

M_TIMER→STOP/RESUME trace数: 17。Tはhost timerの復元・保留確認成功、Pはenclave終了値0。
Pに続く1桁は調査runnerの中断数の下1桁。

```text
[SM/TIMER] host=sstc enclave=mtimer slice-us=10000
Boot HART ISA Extensions    : smaia,smstateen,sscofpmf,sstc,zicntr,zihpm,zicboz,zicbom,svpbmt,sdtrig,svade,smnpm
[SM-TRACE] hart=0 STOP asynchronous cause a=7 b=0xffffffffc0000494
[SM-TRACE] hart=0 STOP asynchronous cause a=7 b=0x1016c
[SM-TRACE] hart=0 STOP asynchronous cause a=7 b=0x1016c
[SM-TRACE] hart=0 sbi.enter a=3006 b=0xffffffffc00004be
TP7[SM-TRACE] hart=0 sbi.enter a=2002 b=0x10220016c
```

## Sstc off

M_TIMER→STOP/RESUME trace数: 17。Tはhost timerの復元・保留確認成功、Pはenclave終了値0。
Pに続く1桁は調査runnerの中断数の下1桁。

```text
[SM/TIMER] host=sbi-mtimer enclave=mtimer slice-us=10000
Boot HART ISA Extensions    : smaia,smstateen,sscofpmf,zicntr,zihpm,zicboz,zicbom,svpbmt,sdtrig,svade,smnpm
[SM-TRACE] hart=1 STOP asynchronous cause a=7 b=0xffffffffc0000494
[SM-TRACE] hart=1 STOP asynchronous cause a=7 b=0x1016c
[SM-TRACE] hart=1 STOP asynchronous cause a=7 b=0x1016c
[SM-TRACE] hart=1 sbi.enter a=3006 b=0xffffffffc00004be
TP7[SM-TRACE] hart=1 sbi.enter a=2002 b=0x10220016e
```

Linuxを含む再実行手順・未確認範囲は [共存文書](../../../doc/keystone-sstc-coexistence.md) を参照。
