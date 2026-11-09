# M_TIMER preemption調査記録

日付：2026-11-09 UTC。Linux/rootfsを使わず、実QEMU Jupiter2・実OpenSBI/SM・
既存hello-native用Eyrie/loader・rdtime待機だけを行うnative eappを使用した。
調査用S-mode runnerは実HSMで16 hartをSTARTEDにし、実2001/2003/2005/2002を呼んだ。
SMやCSRの模擬、製品C関数の抽出実行ではない。
Linuxドライバ、SDK run-loopの全suiteを確認する経路でもない。

修正前の最終vector symbolは0x33d56で4-byte未整列、QEMU実測のmtvecは
通常handlerの0x1020005f0のままだった。Sstc抑止だけを修正しても中断0（F0）。
.balign 4修正後はvector=0x33d58、実mtvecも0x102033d58になった。
下記のP1は調査用runnerが終了値0・中断回数1を確認した印。

修正後firmware ELF SHA256: `19062c9b132f1870d6c4dc289209cf9dee817dfc02e0a9dec786372b9df00070`

```text
Boot HART ISA Extensions    : smaia,smstateen,sscofpmf,zicntr,zihpm,zicboz,zicbom,svpbmt,sdtrig,svade,smnpm
[SM-TRACE] hart=6 STOP asynchronous cause a=7 b=0xffffffffc000043a
[SM-TRACE] hart=6 sbi.enter a=2005 b=0x102200102
[SM-TRACE] hart=6 sbi.switch-return a=2005 b=0xffffffffc000043a
[SM-TRACE] hart=6 sbi.enter a=3006 b=0xffffffffc0000464
P1[SM-TRACE] hart=6 sbi.enter a=2002 b=0x10220013c
```

Linux専用試験の再現手順は [README](../README.md)。今回追加した修正を含む
全Linux suiteのPASSはまだ受領していない。最新ユーザーlogの直接yield3件と
不正request3件の成功、timer区間0による失敗も区別して記録する。
