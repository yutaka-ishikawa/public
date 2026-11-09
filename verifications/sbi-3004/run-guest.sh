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
work=$(mktemp -d /tmp/stop-verify.XXXXXX)
trap 'rm -rf "$work"' EXIT
# Snapshot before makeself checksum/seek operations; keep checksum failures fatal.
cp -- "build/stop.ke" "$work/package.ke"
bash "$work/package.ke" --noexec --target "$work/payload"
cd "$work/payload"
./runner eapp eyrie-rt loader.bin
