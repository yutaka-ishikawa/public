#!/usr/bin/env bash
# Build all actual SM backends and matching system-QEMU integration artifacts.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
PREFIX="$ROOT/install/keystone"
BUILD_DIR="$ROOT/.build-keystone/rng"
SDK_BUILD="$ROOT/.build-keystone"
QEMU_BUILD="$ROOT/.build-k3"
JOBS=${JOBS:-$(nproc)}
BACKEND=all
SKIP_QEMU=0
SKIP_SDK=0
while (($#)); do
 case "$1" in
  --prefix|--build-dir|--sdk-build-dir|--qemu-build-dir|--jobs|--backend)
   (($# >= 2)) || { echo "Missing value: $1" >&2; exit 2; }
   case "$1" in
    --prefix) PREFIX=$2;; --build-dir) BUILD_DIR=$2;;
    --sdk-build-dir) SDK_BUILD=$2;; --qemu-build-dir) QEMU_BUILD=$2;;
    --jobs) JOBS=$2;; --backend) BACKEND=$2;;
   esac
   shift 2;;
  --skip-sdk) SKIP_SDK=1; shift;;
  --skip-qemu) SKIP_QEMU=1; shift;;
  --help|-h)
   echo 'Usage: bash scripts/build-verification-rng.sh [--backend all|zkr|jupiter2|counter] [--prefix DIR] [--build-dir DIR] [--sdk-build-dir DIR] [--qemu-build-dir DIR] [--jobs N] [--skip-qemu] [--skip-sdk]'
   exit 0;;
  *) echo "Unknown argument: $1" >&2; exit 2;;
 esac
done
case "$BACKEND" in all) backends=(zkr jupiter2 counter);; zkr|jupiter2|counter) backends=("$BACKEND");; *) exit 2;; esac
[[ $JOBS =~ ^[1-9][0-9]*$ ]] || exit 2
PREFIX=$(realpath -m "$PREFIX")
BUILD_DIR=$(realpath -m "$BUILD_DIR")
SDK_BUILD=$(realpath -m "$SDK_BUILD")
if ((!SKIP_QEMU)); then
 bash "$ROOT/scripts/build-install-k3.sh" --only qemu --prefix "$PREFIX" --build-dir "$QEMU_BUILD" --jobs "$JOBS"
fi
if ((!SKIP_SDK)); then
 bash "$ROOT/scripts/build-install-keystone.sh" --only sdk --build-dir "$SDK_BUILD" --jobs "$JOBS"
fi
for backend in "${backends[@]}"; do
 dest="$PREFIX/firmware/k3/rng-$backend"
 stage="$BUILD_DIR/$backend/install"
 KEYSTONE_RNG_BACKEND="$backend" bash "$ROOT/scripts/build-install-k3.sh" \
  --only firmware --prefix "$stage" --build-dir "$BUILD_DIR/$backend/build" --jobs "$JOBS"
 mkdir -p "$dest"
 cp "$stage/firmware/k3/"{fw_dynamic.bin,fw_dynamic.elf,sm-combined.o} "$dest/"
 { printf 'backend=%s\n' "$backend"; sha256sum "$dest/"fw_dynamic.*; } > "$dest/manifest.txt"
done
make -C "$ROOT/verifications/random" guest KEYSTONE_BUILD_DIR="$SDK_BUILD"
echo 'RNG artifacts built. Boot the existing matching kernel/rootfs to execute verifications/random/run-guest.sh.'
