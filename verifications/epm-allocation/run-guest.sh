#!/usr/bin/env bash
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
[[ $(uname -m) == riscv64 && $EUID == 0 ]] || {
    echo 'Run as root inside the K3 RISC-V guest.' >&2
    exit 2
}
bb=/usr/libexec/keystone/busybox
mkdir -p /proc /sys
if ! "$bb" mountpoint -q /proc; then "$bb" mount -t proc proc /proc; fi
if ! "$bb" mountpoint -q /sys; then "$bb" mount -t sysfs sysfs /sys; fi
if [[ ! -e /dev/keystone_enclave ]]; then
    "$bb" insmod "/usr/lib/modules/$(uname -r)/extra/keystone-driver.ko"
fi
cat /proc/cmdline
"$bb" grep -E 'MemAvailable|CmaTotal|CmaFree' /proc/meminfo
./build/epm-guest
