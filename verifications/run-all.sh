#!/usr/bin/env bash
# Execute real system-guest integrations only. Never compile or run host mocks.
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
if [[ $(uname -m) != riscv64 || $EUID != 0 ]]; then
    echo 'Run as root inside the K3 QEMU Linux guest; build on the host with make -C verifications guest.' >&2
    exit 2
fi
if (($# > 1)) || [[ ${1:-} != '' && ${1:-} != --data-only ]]; then
    echo 'Usage: bash verifications/run-all.sh [--data-only]' >&2
    exit 2
fi
# All builds must exist before beginning the test sequence.
for file in sbi-errors/build/test sbi-errors/kernel/keystone_sbi_verify.ko \
    edgecall/build/guest/edgecall.ke edgecall/build/guest-256/edgecall.ke; do
    test -s "$file" || { echo "Missing integration artifact: $file" >&2; exit 3; }
done
if [[ ${1:-} != --data-only ]]; then
    test -s edgecall/kernel/edgecall_pmp_probe.ko || {
        echo 'Missing real PMP probe module; build make kernel-modules or explicitly select --data-only.' >&2
        exit 3
    }
fi
bash sbi-errors/run-guest.sh
for package in build/guest/edgecall.ke build/guest-256/edgecall.ke; do
    printf 'Integration package: %s\n' "$package"
    EDGE_VERIFY_PACKAGE="$package" bash edgecall/run-guest.sh "$@"
done
printf 'PASS GUEST SUITE: real SBI errors and enclave lifecycle at 2/256 MiB free memory\n'
if [[ ${1:-} == --data-only ]]; then
    printf 'PMP direct-access probe omitted by explicit --data-only option\n'
fi
