#!/usr/bin/env bash
# Run only inside the RISC-V system guest, with a matching installed driver.
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
[[ $(uname -m) == riscv64 && $EUID == 0 ]] || { echo 'Run as root inside QEMU Linux.' >&2; exit 2; }
case ${1:-} in '') basic=0;; --basic) basic=1;; *) echo 'Usage: run-guest.sh [--basic]' >&2; exit 2;; esac
bb=/usr/libexec/keystone/busybox
mkdir -p /proc /sys
"$bb" mountpoint -q /proc || "$bb" mount -t proc proc /proc
"$bb" mountpoint -q /sys || "$bb" mount -t sysfs sysfs /sys
if [[ ! -e /dev/keystone_enclave ]]; then
    "$bb" insmod "/usr/lib/modules/$(uname -r)/extra/keystone-driver.ko"
fi
# The metadata must correspond to the firmware selected at QEMU startup.
if [[ -d ../crypto ]]; then
    default_public=$(cd ../crypto && pwd)
else
    default_public=$(pwd)/../install/keystone/share/keystone/crypto
fi
export KEYSTONE_PUBLIC_DIR=${KEYSTONE_PUBLIC_DIR:-$default_public}
work=$(mktemp -d /tmp/keystone-samples.XXXXXX)
trap 'rm -rf "$work"' EXIT
for spec in helloworld:hello.ke hello-native:hello-native.ke attestation:attestor.ke tests:tests.ke; do
    sample=${spec%:*}
    if ((basic)) && [[ $sample == attestation ]]; then
        echo 'SKIP SAMPLE attestation: explicit --basic mode'
        continue
    fi
    bash common/extract-package.sh "$sample/build/${spec#*:}" "$work/$sample"
    case $sample in
        helloworld) (cd "$work/$sample/payload"; ./hello-runner hello eyrie-rt loader.bin);;
        hello-native) (cd "$work/$sample/payload"; ./hello-native-runner hello-native eyrie-rt loader.bin 0);;
        attestation) (cd "$work/$sample/payload"; ./attestor-runner attestor eyrie-rt loader.bin 0);;
        tests) args=(); if ((basic)); then args=(--basic); fi
            bash "$work/$sample/payload/run-tests.sh" "${args[@]}";;
    esac
done
printf 'PASS SAMPLE SUITE: selected examples completed (basic=%s)\n' "$basic"
