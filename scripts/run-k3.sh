#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
PREFIX=""
IMAGE=keystone
QEMU=""
BIOS=""
KERNEL=""
ROOTFS=""
HOSTFS="$ROOT"
GDB=0
GDB_PORT=1234
CONSOLE=0
MONITOR=0
MONITOR_PORT=4444
SMP=""
MEM=2G
CMDLINE=""
APPEND_SET=0
CRYPTO=""
RNG=""
ZKR=""
WTM=""
WTM_FAULT=none
ROOTFS_FORMAT=raw
usage() {
cat <<'EOF'
Usage: bash scripts/run-k3.sh [options]
  --image keystone|bianbu   Boot profile (default: keystone)
  --prefix DIR             Version installation directory (default: install/PROFILE)
  --qemu FILE              Override QEMU executable
  --kernel FILE            Override Linux Image
  --bios FILE              Override firmware
  --rootfs FILE            Raw guest rootfs image (default: selected profile image)
  --crypto unavailable|qemu-dev  Choose normal or explicit development crypto firmware
  --rng auto|zkr|jupiter2|counter  Select matching RNG firmware and devices
  --zkr on|off             Override Zkr CPU extension for negative tests
  --wtm-rng on|off         Override WTM MMIO peripheral
  --wtm-rng-fault none|not-ready|zero  WTM fault injection (QEMU only)
  --rootfs-format raw|qcow2 Rootfs disk format (default raw)
  --hostfs DIR             Host shared directory (default: repository root)
  --gdb                    Pause guest (-S) and listen on 127.0.0.1:1234
  --gdb-port N             GDB port, implies --gdb
  --console                QEMU interactive serial/monitor multiplexer (-nographic, Ctrl-A C)
  --monitor                Enable QEMU monitor on 127.0.0.1:4444
  --monitor-port N         Monitor port, implies --monitor
  --smp N                  Hart count (default: 16)
  --memory SIZE            Memory (default 2G)
  --append TEXT            Override Linux kernel command line
  --help                   Show help

No options starts QEMU normally: no GDB stop, serial attached to the terminal.
Build Keystone with scripts/build-install-keystone.sh, or Bianbu with scripts/build-install-k3.sh.
EOF
}
while (($#)); do
 case "$1" in
  --crypto|--rng|--zkr|--wtm-rng|--wtm-rng-fault|--rootfs-format|--image|--prefix|--qemu|--kernel|--bios|--rootfs|--hostfs|--gdb-port|--monitor-port|--smp|--memory|--append)
   (($#>=2)) || { echo "Missing value for $1" >&2; exit 2; }
   case "$1" in
    --crypto) CRYPTO=$2;; --rng) RNG=$2;; --zkr) ZKR=$2;; --wtm-rng) WTM=$2;;
    --wtm-rng-fault) WTM_FAULT=$2;; --rootfs-format) ROOTFS_FORMAT=$2;;
    --image) IMAGE=$2;; --qemu) QEMU=$2;; --kernel) KERNEL=$2;; --bios) BIOS=$2;;
    --prefix) PREFIX=$2;; --rootfs) ROOTFS=$2;; --hostfs) HOSTFS=$2;;
    --gdb-port) GDB=1; GDB_PORT=$2;; --monitor-port) MONITOR=1; MONITOR_PORT=$2;;
    --smp) SMP=$2;; --memory) MEM=$2;; --append) CMDLINE=$2; APPEND_SET=1;;
   esac
   shift 2;;
  --gdb) GDB=1; shift;;
  --console) CONSOLE=1; shift;;
  --monitor) MONITOR=1; shift;;
  --help|-h) usage; exit 0;;
  *) echo "Unknown argument: $1" >&2; usage >&2; exit 2;;
 esac
done
case "$IMAGE" in
 keystone|bianbu) :;;
 *) echo "Unknown --image: $IMAGE (use keystone or bianbu)" >&2; exit 2;;
esac
if [[ -z $PREFIX ]]; then PREFIX="$ROOT/install/$IMAGE"; fi
PREFIX=$(realpath -m "$PREFIX")
if [[ -z $QEMU ]]; then QEMU="$PREFIX/bin/qemu-system-riscv64"; fi
: "${SMP:=16}"
case "$CRYPTO" in
 ""|unavailable) :;;
 qemu-dev)
  [[ -z $RNG || $RNG == auto ]] || { echo 'Use --crypto qemu-dev with --rng auto (or omit --rng).' >&2; exit 2; }
  : "${BIOS:=$PREFIX/firmware/k3/crypto-qemu-dev/fw_dynamic.bin}";;
 *) echo 'Invalid --crypto' >&2; exit 2;;
esac
case "$RNG" in
 ""|auto) :;;
 zkr|jupiter2|counter) : "${BIOS:=$PREFIX/firmware/k3/rng-$RNG/fw_dynamic.bin}";;
 *) echo 'Invalid --rng' >&2; exit 2;;
esac
case "$ROOTFS_FORMAT" in raw|qcow2) :;; *) echo 'Invalid disk format' >&2; exit 2;; esac
case "$WTM_FAULT" in none|not-ready|zero) :;; *) echo 'Invalid WTM fault' >&2; exit 2;; esac
if [[ -n $RNG ]]; then
 case "$RNG" in zkr|auto) : "${ZKR:=on}";; *) : "${ZKR:=off}";; esac
 case "$RNG" in jupiter2) : "${WTM:=on}";; *) : "${WTM:=off}";; esac
fi
for value in "$ZKR" "$WTM"; do
 case "$value" in ""|on|off) :;; *) echo 'Use on or off for device overrides' >&2; exit 2;; esac
done
: "${BIOS:=$PREFIX/firmware/k3/fw_dynamic.bin}"
: "${KERNEL:=$PREFIX/boot/k3/Image}"
if [[ $IMAGE == keystone ]]; then
 : "${ROOTFS:=$PREFIX/rootfs/keystone-rootfs.img}"
else
 : "${ROOTFS:=$PREFIX/rootfs/bianbu-rootfs.img}"
fi
if ((!APPEND_SET)); then
 CMDLINE='root=/dev/vda rw rootwait console=ttyS0,115200 earlycon=sbi init=/bin/bash loglevel=7'
 # RISC-V's default CMA reservation limit is 4 GiB, below all K3 RAM.
 # Supply an explicit allocation range; memblock selects available RAM in it.
 if [[ $IMAGE == keystone ]]; then CMDLINE+=' cma=512M@4G-16G'; fi
fi
for value in "$SMP" "$GDB_PORT" "$MONITOR_PORT"; do
 [[ $value =~ ^[1-9][0-9]*$ ]] || { echo "Invalid numeric value: $value" >&2; exit 2; }
done
if [[ ! -e "$ROOTFS" && ! -L "$ROOTFS" ]]; then
 if [[ $IMAGE == keystone ]]; then builder=build-install-keystone.sh; else builder=build-install-k3.sh; fi
 printf 'Missing %s rootfs: %s. Build with bash scripts/%s --prefix %q\n' "$IMAGE" "$ROOTFS" "$builder" "$PREFIX" >&2
 exit 3
fi

for entry in "$QEMU" "$BIOS" "$KERNEL" "$ROOTFS"; do
 test -s "$entry" || { echo "Missing required file: $entry" >&2; exit 3; }
done
test -x "$QEMU" || { echo "QEMU binary is not executable: $QEMU" >&2; exit 3; }
test -d "$HOSTFS" || { echo "Host shared directory missing: $HOSTFS" >&2; exit 3; }
ROOTFS="$(realpath -e "$ROOTFS")"
HOSTFS="$(realpath -e "$HOSTFS")"
args=(-accel tcg -smp "$SMP" -m "$MEM" -bios "$BIOS" -kernel "$KERNEL")
machine=jupiter2
cpu=rva23s64,k3-pmacfg=true,pmp=true,num-pmp-regions=16,pmp-granularity=16
if [[ -n $ZKR ]]; then cpu+=",zkr=$ZKR"; fi
if [[ -n $WTM || $WTM_FAULT != none ]]; then
 machine+=",wtm-rng=${WTM:-on},wtm-rng-fault=$WTM_FAULT"
fi
args+=(-M "$machine" -cpu "$cpu")
block_device=virtio-blk-pci
net_device=virtio-net-pci
args+=(
 -drive "file=$ROOTFS,format=$ROOTFS_FORMAT,if=none,id=hd0"
 -device "$block_device,drive=hd0"
 -fsdev "local,id=fs0,path=$HOSTFS,security_model=mapped-xattr"
 -device virtio-9p-pci,fsdev=fs0,mount_tag=hostshare
 -netdev user,id=net0 -device "$net_device,netdev=net0"
 -append "$CMDLINE"
)
if ((CONSOLE)); then
 args+=(-nographic -serial mon:stdio)
else
 args+=(-display none -serial stdio)
fi
if ((MONITOR)); then
 if ((CONSOLE)); then
   echo "--console includes interactive monitor via Ctrl-A C; --monitor also opens TCP port $MONITOR_PORT"
 fi
 args+=(-monitor "tcp:127.0.0.1:$MONITOR_PORT,server=on,wait=off")
fi
if ((GDB)); then
 args+=(-S -gdb "tcp:127.0.0.1:$GDB_PORT")
 echo "GDB paused: target remote 127.0.0.1:$GDB_PORT"
fi
echo "Launching QEMU: $QEMU"
exec "$QEMU" "${args[@]}"
