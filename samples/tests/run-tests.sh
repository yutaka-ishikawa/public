#!/usr/bin/env bash
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
case ${1:-} in '') basic=0;; --basic) basic=1;; *) echo 'Usage: run-tests.sh [--basic]' >&2; exit 2;; esac
for spec in stack:12345 loop:54321 malloc:11411 long-nop:12345 fibonacci:14930352 fib-bench:any untrusted:13 attestation:0 data-sealing:0; do
    test=${spec%:*}
    expected=${spec#*:}
    if ((basic)) && [[ $test == attestation || $test == data-sealing ]]; then
        echo "SKIP SAMPLE test-$test: explicit --basic mode"
        continue
    fi
    ./test-runner "test-$test" eyrie-rt loader.bin "$expected"
done
printf 'PASS SAMPLE tests: selected cases completed (basic=%s)\n' "$basic"
