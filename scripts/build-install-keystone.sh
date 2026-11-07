#!/usr/bin/env bash
# Use the current K3 QEMU/OpenSBI/Linux with a separate Keystone-enabled rootfs.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
PREFIX="$ROOT/install/keystone"
BASE_PREFIX="$ROOT/install/bianbu"
BUILD_DIR="$ROOT/.build-keystone"
K3_BUILD_DIR="$ROOT/.build-k3"
JOBS=${JOBS:-$(nproc)}
ARCHIVE=""; HASH=""; ONLY=all
usage() {
 cat <<'HELP'
Usage: bash scripts/build-install-keystone.sh [options]
  --prefix DIR          Keystone destination (default: install/keystone)
  --base-prefix DIR     Reuse K3 QEMU/firmware/kernel from this Bianbu installation
                        (default: install/bianbu). Missing components are built.
  --build-dir DIR       Keystone SDK/examples/driver output (default: .build-keystone)
  --k3-build-dir DIR    Matching K3 kernel build output parent (default: .build-k3)
  --jobs N              Parallel jobs
  --rootfs-archive FILE Local Bianbu base tar.gz; otherwise download Bianbu 26.04
  --rootfs-sha256 HASH  Independently obtained archive checksum
  --only sdk            Build just SDK and basic hello examples for development
Uses the SAME K3 machine, Keystone-capable OpenSBI and Linux as the Bianbu build.
Builds the matching driver and installs hello.ke and hello-native.ke in a separate
8 GiB guest image. Existing Keystone guest images are never overwritten; choose
another prefix to install a fresh stack. Enclave runtime behavior needs validation.
HELP
}
while (($#)); do
 case "$1" in
  --prefix|--base-prefix|--build-dir|--k3-build-dir|--jobs|--rootfs-archive|--rootfs-sha256|--only)
   (($# >= 2)) || { usage >&2; exit 2; }
   case "$1" in
    --prefix) PREFIX=$2;; --base-prefix) BASE_PREFIX=$2;; --build-dir) BUILD_DIR=$2;;
    --k3-build-dir) K3_BUILD_DIR=$2;; --jobs) JOBS=$2;; --rootfs-archive) ARCHIVE=$2;;
    --rootfs-sha256) HASH=$2;; --only) ONLY=$2;;
   esac
   shift 2;;
  --help|-h) usage; exit 0;;
  *) echo "Unknown argument: $1" >&2; exit 2;;
 esac
done
[[ $JOBS =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid --jobs' >&2; exit 2; }
case "$ONLY" in all|sdk) :;; *) echo 'Only --only sdk is supported' >&2; exit 2;; esac
if [[ $ONLY == all && ( -e "$PREFIX/rootfs/keystone-rootfs.img" || -L "$PREFIX/rootfs/keystone-rootfs.img" ) ]]; then
 echo 'Keystone image already exists. Use a new --prefix to preserve guest changes.' >&2; exit 3
fi
for tool in cmake make gcc g++ python3 makeself "${CROSS_COMPILE:-riscv64-linux-gnu-}gcc" "${CROSS_COMPILE:-riscv64-linux-gnu-}g++"; do
 command -v "$tool" >/dev/null || { echo "Missing $tool; install scripts/install-build-deps.sh prerequisites." >&2; exit 3; }
done
PREFIX=$(realpath -m "$PREFIX")
BASE_PREFIX=$(realpath -m "$BASE_PREFIX")
if [[ $ONLY == all && $PREFIX == "$BASE_PREFIX" ]]; then
 echo 'Keystone and Bianbu must use separate installation directories.' >&2; exit 2
fi
if [[ $ONLY == all ]]; then
 for tool in mke2fs debugfs e2fsck tune2fs; do
  command -v "$tool" >/dev/null || { echo "Missing $tool; install e2fsprogs first." >&2; exit 3; }
 done
fi
mkdir -p "$BUILD_DIR"
BUILD_DIR=$(cd "$BUILD_DIR" && pwd -P)
K3_BUILD_DIR=$(realpath -m "$K3_BUILD_DIR")
BASE_PREFIX=$(realpath -m "$BASE_PREFIX")
# Keystone's SDK expects the Buildroot triplet. Provide aliases to the supported
# distro cross tools, without editing SDK sources or the host installation.
mkdir -p "$BUILD_DIR/toolchain"
for tool in gcc g++ as ld ar objcopy objdump; do
 source_tool=$(command -v "${CROSS_COMPILE:-riscv64-linux-gnu-}$tool")
 ln -sfn "$source_tool" "$BUILD_DIR/toolchain/riscv64-buildroot-linux-gnu-$tool"
done
export PATH="$BUILD_DIR/toolchain:$PATH"
SDK="$BUILD_DIR/sdk-install"
# sm_call.h currently needs cstdint visible to SDK C++ users.
CMAKE_ARGS=(-DKEYSTONE_BITS=64 -DKEYSTONE_SDK_DIR="$SDK" -DCMAKE_BUILD_TYPE=Release
 -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=riscv64
 -DCMAKE_C_COMPILER=riscv64-buildroot-linux-gnu-gcc -DCMAKE_CXX_COMPILER=riscv64-buildroot-linux-gnu-g++
 "-DCMAKE_C_FLAGS=${CFLAGS:-}" "-DCMAKE_CXX_FLAGS=${CXXFLAGS:-} -include cstdint")
(cd "$BUILD_DIR"; cmake -S "$ROOT/keystone/sdk" -B "$BUILD_DIR/sdk" "${CMAKE_ARGS[@]}")
cmake --build "$BUILD_DIR/sdk" --parallel "$JOBS"
cmake --install "$BUILD_DIR/sdk"
(cd "$BUILD_DIR"; cmake -S "$ROOT/keystone/examples" -B "$BUILD_DIR/examples" "${CMAKE_ARGS[@]}" -DKEYSTONE_EYRIE_RUNTIME="$ROOT/keystone/runtime")
# ExternalProject copies Eyrie only once. Refresh existing generated copies so
# source fixes are not silently hidden by a cached download stamp.
for name in hello hello-native; do
 runtime_copy="$BUILD_DIR/examples/$name/runtime/src/eyrie-$name-eyrie"
 if [[ -d $runtime_copy ]]; then cp -a "$ROOT/keystone/runtime/." "$runtime_copy/"; fi
done
cmake --build "$BUILD_DIR/examples" --target hello-package hello-native-package --parallel "$JOBS"
if [[ $ONLY == sdk ]]; then echo "SDK and examples built at $BUILD_DIR"; exit 0; fi
mkdir -p "$PREFIX"
PREFIX=$(cd "$PREFIX" && pwd -P)
for component in qemu firmware linux; do
 case "$component" in
  qemu) artifact=bin/qemu-system-riscv64;; firmware) artifact=firmware/k3/fw_dynamic.bin;; linux) artifact=boot/k3/Image;;
 esac
 if [[ -s "$BASE_PREFIX/$artifact" ]]; then
  case "$component" in
   qemu) mkdir -p "$PREFIX/bin"; install -m 0755 "$BASE_PREFIX/$artifact" "$PREFIX/$artifact";
    if [[ -d "$BASE_PREFIX/share/qemu" ]]; then mkdir -p "$PREFIX/share/qemu"; cp -a "$BASE_PREFIX/share/qemu/." "$PREFIX/share/qemu/"; fi;;
   firmware) mkdir -p "$PREFIX/firmware/k3"; cp -a "$BASE_PREFIX/firmware/k3/." "$PREFIX/firmware/k3/";;
   linux) mkdir -p "$PREFIX/boot/k3"; cp -a "$BASE_PREFIX/boot/k3/." "$PREFIX/boot/k3/";;
  esac
 else
  bash "$ROOT/scripts/build-install-k3.sh" --only "$component" --prefix "$PREFIX" --build-dir "$K3_BUILD_DIR" --jobs "$JOBS"
 fi
done
KERNEL_OUT="$K3_BUILD_DIR/linux"
# A driver must match the installed Image, not merely a similar source version.
cmp "$PREFIX/boot/k3/Image" "$KERNEL_OUT/arch/riscv/boot/Image" || {
 echo 'Matching kernel build output required. Set --k3-build-dir to the directory used for the Bianbu kernel.' >&2; exit 4;
}
test -s "$KERNEL_OUT/Module.symvers" || { echo 'Kernel Module.symvers missing; complete the Linux Image/modules build first.' >&2; exit 4; }
DRIVER="$BUILD_DIR/driver"
mkdir -p "$DRIVER"
cp -a "$ROOT/keystone/linux-keystone-driver/." "$DRIVER/"
make -C "$ROOT/linux-k3" O="$KERNEL_OUT" ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE:-riscv64-linux-gnu-}" \
 M="$DRIVER" KEYSTONE_SDK_DIR="$SDK" -j "$JOBS" modules
RELEASE=$(make -s -C "$ROOT/linux-k3" O="$KERNEL_OUT" ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE:-riscv64-linux-gnu-}" kernelrelease)
[[ $RELEASE =~ ^[A-Za-z0-9._+-]+$ ]] || { echo 'Invalid kernel release' >&2; exit 4; }
GUEST=$(mktemp -d "$BUILD_DIR/guest.XXXXXX")
trap 'rm -rf "$GUEST"' EXIT
install -D -m 0644 "$DRIVER/keystone-driver.ko" "$GUEST/usr/lib/modules/$RELEASE/extra/keystone-driver.ko"
for name in hello hello-native; do
 install -D -m 0755 "$BUILD_DIR/examples/$name/$name.ke" "$GUEST/usr/share/keystone/examples/$name.ke"
done
bash "$ROOT/scripts/build-keystone-guest-tools.sh" "$BUILD_DIR/guest-tools/busybox" "$BUILD_DIR/guest-tools/build" "$JOBS"
bash "$ROOT/scripts/install-keystone-guest-tools.sh" "$GUEST" "$BUILD_DIR/guest-tools/busybox"
bash "$ROOT/scripts/build-rootfs-keystone.sh" "$PREFIX/rootfs/keystone-rootfs.img" "$BUILD_DIR/rootfs" "$GUEST" "$ARCHIVE" "$HASH"
mkdir -p "$PREFIX/share/keystone"
printf 'profile=keystone-k3\nkernelrelease=%s\nsdk=%s\n' "$RELEASE" "$SDK" > "$PREFIX/share/keystone/build-manifest.txt"
echo "Installed K3 Keystone stack at $PREFIX"
echo 'Boot with bash run-k3.sh; in the guest run /usr/bin/run-keystone-hello.'
echo 'This packages the stack; it does not certify enclave, attestation or DMA isolation behavior.'
