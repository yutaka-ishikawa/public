#!/usr/bin/env bash
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
[[ $(uname -m) == riscv64 && $EUID == 0 ]] || { echo 'Run as root inside QEMU Linux.' >&2; exit 2; }
mode=${1:-unavailable}
case "$mode" in unavailable|qemu-dev) :;; *) echo 'Usage: run-guest.sh [unavailable|qemu-dev [PUBLIC_DIR]]' >&2; exit 2;; esac
public=$(realpath -m "${2:-../../install/keystone/share/keystone/crypto}")
bb=/usr/libexec/keystone/busybox
mkdir -p /proc /sys
"$bb" mountpoint -q /proc || "$bb" mount -t proc proc /proc
"$bb" mountpoint -q /sys || "$bb" mount -t sysfs sysfs /sys
if [[ ! -e /dev/keystone_enclave ]]; then
 "$bb" insmod "/usr/lib/modules/$(uname -r)/extra/keystone-driver.ko"
fi
work=$(mktemp -d /tmp/crypto-verify.XXXXXX)
trap 'rm -rf "$work"' EXIT
bash build/crypto.ke --noexec --target "$work/payload"
cd "$work/payload"
if [[ $mode == unavailable ]]; then
 ./runner eapp-unavailable eyrie-rt loader.bin - - -
else
 sha256sum -c "$public/package-sha256.txt"
 # A repeated after explicit DESTROY, then B with different measured bytes.
 for app in eapp eapp eapp-b; do
  ./runner "$app" eyrie-rt loader.bin "$public/device-public-key.bin" "$public/sm-hash.bin" "$public/$app-key-digest.bin"
 done
fi
