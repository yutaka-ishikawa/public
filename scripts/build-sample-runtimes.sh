#!/usr/bin/env bash
# Build the two Eyrie profiles; application compilation is handled by samples/.
set -Eeuo pipefail
[[ $# == 2 ]] || { echo 'Usage: build-sample-runtimes.sh KEYSTONE_BUILD_DIR JOBS' >&2; exit 2; }
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
build=$(realpath -m "$1")
jobs=$2
[[ $jobs =~ ^[1-9][0-9]*$ ]] || exit 2
sdk=$build/sdk-install
cross=${CROSS_COMPILE:-riscv64-linux-gnu-}
compiler=$(command -v "${cross}gcc")
for profile in hello hello-native; do
    work=$build/sample-runtimes/$profile
    mkdir -p "$work/src" "$build/examples/$profile"
    cp -a "$root/keystone/runtime/." "$work/src/"
    enabled=OFF
    if [[ $profile == hello ]]; then enabled=ON; fi
    cmake -S "$work/src" -B "$work/build" \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=riscv64 \
        -DCMAKE_C_COMPILER="$compiler" -DCMAKE_ASM_COMPILER="$compiler" \
        "-DCMAKE_C_FLAGS=${CFLAGS:-}" -DKEYSTONE_SDK_DIR="$sdk" \
        -DIO_SYSCALL="$enabled" -DLINUX_SYSCALL="$enabled" -DENV_SETUP="$enabled"
    rm -f "$work/build/.options_log"
    cmake --build "$work/build" --parallel "$jobs"
    cp "$work/src/eyrie-rt" "$work/src/loader.bin" "$work/build/.options_log" \
        "$build/examples/$profile/"
done
