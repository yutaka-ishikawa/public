#!/usr/bin/env bash
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
if [[ $(uname -m) != riscv64 || $EUID != 0 ]]; then
    echo 'SETUP_ERROR: run as root inside the K3 QEMU Linux guest.' >&2
    exit 2
fi
bb=/usr/libexec/keystone/busybox
mkdir -p /proc /sys
"$bb" mountpoint -q /proc || "$bb" mount -t proc proc /proc
"$bb" mountpoint -q /sys || "$bb" mount -t sysfs sysfs /sys
for artifact in build/coordinator build/eapp build/eyrie-rt build/loader.bin; do
    test -s "$artifact" || { echo "SETUP_ERROR missing $artifact" >&2; exit 2; }
done
if [[ ! -e /dev/keystone_enclave ]]; then
    "$bb" insmod "/usr/lib/modules/$(uname -r)/extra/keystone-driver.ko"
fi
if [[ ( " $* " == *" --raw-only "* || " $* " == *" --faults "* || " $* " == *" --pmp-fault "* || " $* " == *" --ipi-fault "* ) && ! -e /dev/keystone_robustness ]]; then
    "$bb" insmod kernel/probe.ko
fi
# Only public provider pins may be passed via --crypto. No private seed is read.
exec ./build/coordinator "$@"
