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
    edgecall/build/guest/edgecall.ke edgecall/build/guest-256/edgecall.ke \
    random/build/random.ke crypto/build/crypto.ke sbi-3004/build/stop.ke sbi-4000/build/multimem.ke sbi-4000/build/test sbi-4000/kernel/probe.ko; do
    test -s "$file" || { echo "Missing integration artifact: $file" >&2; exit 3; }
done
if [[ ${1:-} != --data-only ]]; then
    test -s edgecall/kernel/edgecall_pmp_probe.ko || {
        echo 'Missing real PMP probe module; build make kernel-modules or explicitly select --data-only.' >&2
        exit 3
    }
fi
bash sbi-errors/run-guest.sh
bash sbi-3004/run-guest.sh
bash sbi-4000/run-guest.sh
bash random/run-guest.sh
# Select the expected provider explicitly; default firmware has no key service.
bash crypto/run-guest.sh "${KEYSTONE_CRYPTO_EXPECT:-unavailable}"
# Recreate both sizes after verified DESTROY to exercise TOR teardown/reuse.
for iteration in 1 2; do
    for package in build/guest/edgecall.ke build/guest-256/edgecall.ke; do
        printf 'Integration cycle %s package: %s\n' "$iteration" "$package"
        EDGE_VERIFY_PACKAGE="$package" bash edgecall/run-guest.sh "$@"
    done
done
printf 'Crypto expectation: %s\n' "${KEYSTONE_CRYPTO_EXPECT:-unavailable}"
printf 'PASS GUEST SUITE SBI 2001/2002/2003/2004(undefined)/2005/3001/3002/3003/3004/3006/4000: real SBI errors and enclave lifecycle at 2/256 MiB free memory\n'
if [[ ${1:-} == --data-only ]]; then
    printf 'PMP direct-access probe omitted by explicit --data-only option\n'
fi
