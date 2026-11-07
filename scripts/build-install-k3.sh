#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
PREFIX="$ROOT"
BUILD_DIR="$ROOT/.build-k3"
JOBS="${JOBS:-$(nproc)}"
COMPONENT=all
ROOTFS_ARCHIVE=""
ROOTFS_SHA256=""
usage() {
cat <<'HELP'
Usage: bash scripts/build-install-k3.sh [--prefix DIR] [--build-dir DIR] [--jobs N] [--only qemu|firmware|linux|rootfs] [--rootfs-archive FILE] [--rootfs-sha256 HASH]
Build from repository's patched qemu/, opensbi/, keystone/, and linux-k3/ sources.
Default installation prefix is repository root. Outputs: bin/, firmware/k3/, boot/k3/, lib/modules/.
Full builds also create rootfs/bianbu-rootfs.img (8 GiB ext4, Bianbu 26.04).
Existing images are preserved. --only rootfs prepares just the guest image.
--rootfs-archive FILE uses a local Bianbu tar.gz instead of downloading it.
--rootfs-sha256 HASH verifies the archive against a separately obtained SHA256.
HELP
}
while (($#)); do
 case "$1" in
  --prefix|--build-dir|--jobs|--only|--rootfs-archive|--rootfs-sha256)
    (($#>=2)) || { usage >&2; exit 2; }
    case "$1" in --prefix) PREFIX=$2;; --build-dir) BUILD_DIR=$2;; --jobs) JOBS=$2;; --only) COMPONENT=$2;; --rootfs-archive) ROOTFS_ARCHIVE=$2;; --rootfs-sha256) ROOTFS_SHA256=$2;; esac
    shift 2;;
  -h|--help) usage; exit 0;;
  *) echo "Unknown argument: $1" >&2; usage >&2; exit 2;;
 esac
done
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid --jobs: $JOBS" >&2; exit 2; }
case "$COMPONENT" in all|qemu|firmware|linux|rootfs) :;; *) echo "Unknown component: $COMPONENT" >&2; exit 2;; esac
mkdir -p "$PREFIX" "$BUILD_DIR"
PREFIX="$(cd "$PREFIX" && pwd -P)"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd -P)"
# Prepare guest storage first so missing tools fail before expensive builds.
if [[ "$COMPONENT" == all || "$COMPONENT" == rootfs ]]; then
 bash "$ROOT/scripts/build-rootfs-k3.sh" "$PREFIX/rootfs/bianbu-rootfs.img" "$BUILD_DIR/rootfs" "$ROOTFS_ARCHIVE" "$ROOTFS_SHA256"
fi
# Mark the generated build directory for safe targeted cleanup.
printf "k3-build-dir-v1\n" > "$BUILD_DIR/.k3-build-root"
mkdir -p "$PREFIX/share/k3"
MANIFEST="$PREFIX/share/k3/build-manifest.txt"
{
 printf 'timestamp=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
 printf 'prefix=%s\n' "$PREFIX"
 printf 'repository=%s\n' "$ROOT"
 printf 'build_dir=%s\n' "$BUILD_DIR"
} > "$MANIFEST"
if [[ "$COMPONENT" == all || "$COMPONENT" == qemu ]]; then
 test -f "$ROOT/qemu/configure" || { echo 'Missing qemu/configure' >&2; exit 3; }
 command -v ninja >/dev/null || { echo 'ninja missing' >&2; exit 3; }
 mkdir -p "$BUILD_DIR/qemu"
 ( cd "$BUILD_DIR/qemu" && "$ROOT/qemu/configure" --prefix="$PREFIX" --target-list=riscv64-softmmu --enable-tcg --disable-docs --disable-werror )
 ninja -C "$BUILD_DIR/qemu" -j "$JOBS" qemu-system-riscv64
 ninja -C "$BUILD_DIR/qemu" -j "$JOBS" install
 test -x "$PREFIX/bin/qemu-system-riscv64"
 "$PREFIX/bin/qemu-system-riscv64" -machine help > "$PREFIX/share/k3/qemu-machines.txt"
 grep -qi jupiter2 "$PREFIX/share/k3/qemu-machines.txt" || { echo 'jupiter2 machine missing' >&2; exit 4; }
 sha256sum "$PREFIX/bin/qemu-system-riscv64" >> "$MANIFEST"
fi
if [[ "$COMPONENT" == all || "$COMPONENT" == firmware ]]; then
 test -f "$ROOT/keystone/sm/tools/k3_rv64_compile_audit.py" || { echo 'Missing Keystone K3 patch' >&2; exit 3; }
 test -f "$ROOT/opensbi/platform/generic/configs/k3_keystone_audit_defconfig" || { echo 'Missing OpenSBI K3 patch' >&2; exit 3; }
 mkdir -p "$BUILD_DIR/firmware"
 bash "$ROOT/scripts/build_keystone_sm.sh" "$ROOT/keystone" "$ROOT/opensbi" "$BUILD_DIR/firmware"
 bash "$ROOT/scripts/build_opensbi_link.sh" "$ROOT/opensbi" "$BUILD_DIR/firmware"
 FW="$BUILD_DIR/firmware/build-opensbi-k3-keystone/platform/generic/firmware"
 for ext in bin elf; do
   test -s "$FW/fw_dynamic.$ext" || { echo "Missing fw_dynamic.$ext" >&2; exit 4; }
   install -D -m 0644 "$FW/fw_dynamic.$ext" "$PREFIX/firmware/k3/fw_dynamic.$ext"
   sha256sum "$PREFIX/firmware/k3/fw_dynamic.$ext" >> "$MANIFEST"
 done
 if test -s "$BUILD_DIR/firmware/sm-combined.o"; then
   install -D -m 0644 "$BUILD_DIR/firmware/sm-combined.o" "$PREFIX/firmware/k3/sm-combined.o"
 fi
fi
if [[ "$COMPONENT" == all || "$COMPONENT" == linux ]]; then
 test -f "$ROOT/linux-k3/Makefile" || { echo 'Missing Linux K3 source' >&2; exit 3; }
 KERNEL_OUT="$BUILD_DIR/linux"
 KERNEL_SRC="$ROOT/linux-k3" OUT_DIR="$KERNEL_OUT" JOBS="$JOBS" bash "$ROOT/scripts/build-linux-k3.sh"
 IMAGE="$KERNEL_OUT/arch/riscv/boot/Image"
 test -s "$IMAGE"
 install -D -m 0644 "$IMAGE" "$PREFIX/boot/k3/Image"
 mkdir -p "$PREFIX/boot/k3/dtbs"
 (cd "$KERNEL_OUT/arch/riscv/boot/dts" && find . -type f -name '*.dtb' -print0 | tar --null -T - -cf -) | (cd "$PREFIX/boot/k3/dtbs" && tar -xf -)
 cp "$KERNEL_OUT/.config" "$PREFIX/boot/k3/config"
 make -C "$ROOT/linux-k3" O="$KERNEL_OUT" ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE:-riscv64-linux-gnu-}" INSTALL_MOD_PATH="$PREFIX" modules_install
 sha256sum "$PREFIX/boot/k3/Image" >> "$MANIFEST"
fi
echo "Installed at $PREFIX"
echo "Manifest $MANIFEST"
