#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build SpacemiT K3 Linux from the repo's linux-k3 source snapshot.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
KERNEL_SRC="${KERNEL_SRC:-$ROOT/linux-k3}"
OUT_DIR="${OUT_DIR:-$KERNEL_SRC/build-k3}"
JOBS="${JOBS:-15}"
export ARCH=riscv
export CROSS_COMPILE="${CROSS_COMPILE:-riscv64-linux-gnu-}"
if [ ! -f "$KERNEL_SRC/Makefile" ] || [ ! -f "$KERNEL_SRC/arch/riscv/configs/k3_bianbu_defconfig" ]; then
  echo "Missing K3 kernel source/config in $KERNEL_SRC" >&2
  exit 2
fi
for tool in make "${CROSS_COMPILE}gcc" "${CROSS_COMPILE}ld" bc bison flex; do
  command -v "$tool" >/dev/null || { echo "Required tool not found: $tool" >&2; exit 2; }
done
mkdir -p "$OUT_DIR"
OUT_DIR=$(cd "$OUT_DIR" && pwd)
echo "Kernel source: $KERNEL_SRC"
echo "Output: $OUT_DIR"
echo "Architecture: $ARCH; Cross compiler: $CROSS_COMPILE"
make -C "$KERNEL_SRC" O="$OUT_DIR" k3_bianbu_defconfig
"$KERNEL_SRC/scripts/config" --file "$OUT_DIR/.config" \
  -e VIRTIO_MENU \
  -e PCI_MSI \
  -e VIRTIO_PCI \
  -e VIRTIO_MMIO \
  -e VIRTIO_BLK \
  -e PCI_HOST_GENERIC \
  -e NET_9P \
  -e NET_9P_VIRTIO \
  -e 9P_FS \
  -e VIRTIO_NET
make -C "$KERNEL_SRC" O="$OUT_DIR" olddefconfig
make -C "$KERNEL_SRC" O="$OUT_DIR" syncconfig
# Fail early if requested options were disabled due to unmet Kconfig dependencies.
python3 - "$OUT_DIR/.config" <<'PY'
from pathlib import Path
import sys
cfg=Path(sys.argv[1]).read_text().splitlines()
keys=['VIRTIO_MENU','PCI_MSI','VIRTIO_PCI','VIRTIO_MMIO','VIRTIO_BLK',
      'PCI_HOST_GENERIC','NET_9P','NET_9P_VIRTIO','9P_FS','VIRTIO_NET']
values={}
for line in cfg:
    if line.startswith('CONFIG_') and '=' in line:
        k,v=line.split('=',1);values[k[7:]]=v
missing=[key for key in keys if values.get(key) != 'y']
for key in keys: print(f'CONFIG_{key}={values.get(key, "n")}')
if missing:
    raise SystemExit('Kconfig dependencies disabled required options: '+', '.join(missing))
PY
make -C "$KERNEL_SRC" O="$OUT_DIR" -j"$JOBS" Image dtbs modules
test -s "$OUT_DIR/arch/riscv/boot/Image"
echo "Built kernel Image: $OUT_DIR/arch/riscv/boot/Image"
find "$OUT_DIR/arch/riscv/boot/dts" -type f -name '*.dtb' | sort | tee "$OUT_DIR/k3-dtbs-list.txt" | head -n 100
sha256sum "$OUT_DIR/arch/riscv/boot/Image" | tee "$OUT_DIR/Image.sha256"
echo 'Kernel build completed.'
