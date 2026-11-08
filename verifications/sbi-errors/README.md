# 実OpenSBI/SMへのSBIエラー応答試験

この試験はユーザー空間のECALLをLinuxシステムコールとして実行する方式ではない。
`test.c` が検証専用misc deviceへioctlし、カーネルモジュールがS-modeから
`asm/sbi.h` の `sbi_ecall()` で起動中のOpenSBI/Keystone SMへ要求する。
SBI処理・PMP・戻り値を模擬したり、製品C関数を試験へ組み込んだりしない。

| FID | 実要求 | 期待するSM status |
| --- | --- | ---: |
| 2001 CREATE | カーネルメモリ上の全ゼロlayout | ILLEGAL_ARGUMENT = 100008 |
| 2002 DESTROY | ID = ULONG_MAX | INVALID_ID = 100001 |
| 2003 RUN | ID = ULONG_MAX | INVALID_ID = 100001 |
| 2004 | 未定義FID | NOT_IMPLEMENTED = 100100 |
| 2005 RESUME | ID = ULONG_MAX | INVALID_ID = 100001 |

要求はモジュール内の固定値だけで、ユーザーから任意のSBI引数を受け付けない。
デバイスは0600、実行にはrootとCAP_SYS_RAWIOが必要。
全ゼロlayoutは資源確保前に拒否される。SMが未初期化なら2001等の応答が一致せず失敗する。
標準SBIの負のエラーでなく、Keystone互換ABIの正のstatusを照合する。

ホストで：

```bash
make -C verifications/sbi-errors guest
make -C verifications/sbi-errors kernel-module KERNEL_OUT="$PWD/.build-k3/linux"
```

ゲストで：

```bash
cd /mnt/host/verifications/sbi-errors
bash run-guest.sh
```

対応するImage、完全なModule.symvers、SM対応ファームウェアが必要。
[全体の手順](../README.md)を参照。
CREATE/RUN/RESUME/DESTROYの正常経路はedgecall試験で確認する。
ここで確認する異常系は上表の5件のみで、従来の模擬試験の網羅性を引き継いだとはしない。
実ゲストでの新規試験結果は未確認。
