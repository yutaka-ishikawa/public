#!/usr/bin/env bash
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
[[ $(uname -m) == riscv64 && $EUID == 0 ]] || { echo 'Run as root inside QEMU Linux.' >&2; exit 2; }
bb=/usr/libexec/keystone/busybox
mkdir -p /proc /sys
"$bb" mountpoint -q /proc || "$bb" mount -t proc proc /proc
"$bb" mountpoint -q /sys || "$bb" mount -t sysfs sysfs /sys
if [[ ! -e /dev/keystone_enclave ]]; then
    "$bb" insmod "/usr/lib/modules/$(uname -r)/extra/keystone-driver.ko"
fi
if [[ ! -e /dev/keystone_multimem_verify ]]; then
    "$bb" insmod kernel/probe.ko
fi
printf 'FID 4000 guest kernel: %s\n' "$(uname -r)"
sha256sum "/usr/lib/modules/$(uname -r)/extra/keystone-driver.ko" kernel/probe.ko build/multimem.ke
case ${1:-} in
    '') args=();;
    --disabled) export MULTIMEM_DISABLED=1; args=(--disabled);;
    *) echo 'Usage: run-guest.sh [--disabled]' >&2; exit 2;;
esac
./build/test
work=$(mktemp -d /tmp/multimem-verify.XXXXXX)
trap 'rm -rf "$work"' EXIT
bash ../extract-package.sh "build/multimem.ke" "$work"
cd "$work/payload"
./runner eapp eyrie-rt loader.bin "${args[@]}"
