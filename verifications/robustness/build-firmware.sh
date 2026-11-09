#!/usr/bin/env bash
# Build diagnostic outputs separately; never overwrite the normal installation.
set -Eeuo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
profile=${1:-unavailable}
case "$profile" in unavailable|qemu-dev) :;; *) echo 'Usage: build-firmware.sh [unavailable|qemu-dev]' >&2; exit 2;; esac
build=${ROBUSTNESS_BUILD_DIR:-$root/.build-keystone/robustness-$profile}
KEYSTONE_VERIFICATION=1 KEYSTONE_CRYPTO_PROVIDER="$profile" \
    bash "$root/scripts/build-install-k3.sh" --only firmware \
    --prefix "$build/install" --build-dir "$build/k3" --jobs "${JOBS:-4}"
printf 'Diagnostic BIOS: %s/install/firmware/k3/fw_dynamic.bin\n' "$build"
if [[ $profile == qemu-dev ]]; then
    printf 'Public pins: %s/k3/firmware/crypto-public\n' "$build"
fi
