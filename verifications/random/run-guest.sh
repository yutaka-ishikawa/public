#!/usr/bin/env bash
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
[[ $(uname -m) == riscv64 ]] || { echo 'Run inside the K3 QEMU Linux guest.' >&2; exit 2; }
expected=${1:-0}
case "$expected" in 0|-38|-11|-5) :;; *) echo 'Expected errno: 0, -38, -11, -5' >&2; exit 2;; esac
bb=/usr/libexec/keystone/busybox
mkdir -p /proc /sys
"$bb" mountpoint -q /proc || "$bb" mount -t proc proc /proc
"$bb" mountpoint -q /sys || "$bb" mount -t sysfs sysfs /sys
if [[ ! -e /dev/keystone_enclave ]]; then
 "$bb" insmod "/usr/lib/modules/$(uname -r)/extra/keystone-driver.ko"
fi
work=$(mktemp -d /tmp/rng-verify.XXXXXX)
trap 'rm -rf "$work"' EXIT
bash build/random.ke --noexec --target "$work/payload"
cd "$work/payload"
eapp=eapp
if [[ $expected != 0 ]]; then eapp=eapp-error; fi
./runner "$eapp" eyrie-rt loader.bin "$expected"

./runner env env-runtime env-loader.bin "$expected" env
