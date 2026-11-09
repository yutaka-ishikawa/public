# helloworld：元ソースとの差分・理由・評価

元は`keystone/examples/hello`。元ソースは保持し、統合先の既存名helloworldも維持する。

| 対象・変更 | 理由 | 判定 |
| --- | --- | --- |
| eapp/hello.c：main(void)と書式 | 指定スタイルへ統一 | 中立。printfと終了値0は維持 |
| host/host.cpp：簡易runner→common/runner.hpp | SDK・終了値・OCALL・DESTROY失敗の検査を共有 | 失敗検出は改善。共通header依存、名前による分岐、責務混在は設計上の後退 |
| Linux syscall OCALLをSDKへ配送 | libc printf等を維持 | 元と同じ機能を継続。初回草稿の配送漏れは不具合として修正（KSB-039） |
| free 256 KiB→2 MiB | 共通runnerの値へ統一 | メモリ要求は増加。helloに必要と実証した増量ではなく、資源効率面では後退。UTMは256 KiBで同じ |
| libkeystone-verifierにもリンク | 全sample用共通runnerにreport検証があるため | 非暗号helloには余分な依存。単純さは劣化、サイズ・性能の比較は未測定 |
| CMake→Makefile、同じhello用Eyrie profile | ユーザー指定のビルド方式へ | 要求適合。アプリのprintf動作自体は同等 |

libc CRTは維持し、native用の-nostdlibは使わない。
通常profileでも動作し、現QEMUでのPASSは[検証記録](../results.md)に記載する。
共同部分の評価は[全体評価](../../doc/keystone-samples-change-review.md)を参照。
