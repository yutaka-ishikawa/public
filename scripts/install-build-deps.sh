#!/usr/bin/env bash
# Install host dependencies for QEMU, Keystone/OpenSBI, Linux and Bianbu rootfs.
set -Eeuo pipefail
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
 echo 'Usage: sudo bash scripts/install-build-deps.sh'
 echo 'Install K3 build dependencies using apt-get (Ubuntu 24.04 or equivalent).'
 exit 0
fi
if (($#)); then
 echo "Unknown argument: $1" >&2
 exit 2
fi
if ((EUID != 0)); then
 echo 'Run: sudo bash scripts/install-build-deps.sh' >&2
 exit 1
fi
command -v apt-get >/dev/null || { echo 'This script requires an APT-based distribution.' >&2; exit 1; }
packages=(
 git build-essential ninja-build meson pkg-config cmake makeself
 g++-riscv64-linux-gnu libc6-dev-riscv64-cross
 python3 python3-venv curl
 libglib2.0-dev libpixman-1-dev libfdt-dev libslirp-dev
 zlib1g-dev libcapstone-dev
 clang lld llvm binutils
 gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
 bc bison flex libssl-dev libelf-dev dwarves
 device-tree-compiler e2fsprogs fakeroot
)
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y "${packages[@]}"
echo 'Build dependencies installed. Run as your normal user: bash scripts/build-install-k3.sh'
