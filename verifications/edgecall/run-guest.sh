#!/usr/bin/env bash
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
if [[ $(uname -m) != riscv64 ]]; then
    echo 'Run this script inside the Keystone RISC-V Linux guest.' >&2
    exit 2
fi
bb=/usr/libexec/keystone/busybox
# init=/bin/bash does not mount these; makeself/df needs /proc/mounts.
mkdir -p /proc /sys
if ! "$bb" mountpoint -q /proc; then
    "$bb" mount -t proc proc /proc
fi
if ! "$bb" mountpoint -q /sys; then
    "$bb" mount -t sysfs sysfs /sys
fi
if [[ ! -e /dev/keystone_enclave ]]; then
    "$bb" insmod "/usr/lib/modules/$(uname -r)/extra/keystone-driver.ko"
fi
if [[ ${1:-} != --data-only && ! -e /dev/keystone_edge_probe ]]; then
    "$bb" insmod kernel/edgecall_pmp_probe.ko
fi
workdir=$(mktemp -d /tmp/edgecall-verify.XXXXXX)
trap 'rm -rf "$workdir"' EXIT
bash "${EDGE_VERIFY_PACKAGE:-build/guest/edgecall.ke}" --noexec --target "$workdir/payload"
cd "$workdir/payload"
./runner eapp eyrie-rt loader.bin "$@"
