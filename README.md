# Milk-V Jupiter 2 / SpacemiT K3 integrated source tree

The `main` branch contains four source directories: `qemu/`, `opensbi/`, `keystone/`, `linux-k3/`. Original patches are kept under `patches/`; `patches/linux-k3-config.patch` records the Linux Kconfig modifications. No Linux patch remains in repository root.

## All-in-one build and installation

Install prerequisites on Ubuntu 24.04 or equivalent: `build-essential`, `ninja-build`, `meson`, `pkg-config`, `libglib2.0-dev`, `libpixman-1-dev`, `libfdt-dev`, `libslirp-dev`, `zlib1g-dev`, `libcapstone-dev`, `clang`, `lld`, `llvm`, `binutils`, `gcc-riscv64-linux-gnu`, `binutils-riscv64-linux-gnu`, `bc`, `bison`, `flex`, `libssl-dev`, `libelf-dev`, `dwarves`, `python3`.

From repository root:

```bash
bash scripts/build-install-k3.sh
# Alternative destination and parallelism:
bash scripts/build-install-k3.sh --prefix /opt/jupiter2-k3 --jobs 15
# Individual builds:
bash scripts/build-install-k3.sh --only qemu
bash scripts/build-install-k3.sh --only firmware
bash scripts/build-install-k3.sh --only linux
```

Default installation directory is the repository root; generated files are separate from the source files:

```text
bin/qemu-system-riscv64
firmware/k3/fw_dynamic.bin
firmware/k3/fw_dynamic.elf
firmware/k3/sm-combined.o
boot/k3/Image
boot/k3/dtbs/
boot/k3/config
lib/modules/
share/k3/build-manifest.txt
```

Build intermediates go into `.build-k3/`. QEMU's install target may also install runtime support files in `share/`. No rootfs image is built.

## QEMU boot (from supplied example)

Provide an existing **raw Bianbu rootfs image** explicitly or put it in `rootfs/bianbu-rootfs.img`. Normal boot does **not** pause at reset:

```bash
bash scripts/run-k3.sh --rootfs /path/to/bianbu-rootfs.img
# GDB: wait on loopback port 1234, boot after gdb 'continue'
bash scripts/run-k3.sh --rootfs /path/to/bianbu-rootfs.img --gdb
# Interactive QEMU monitor multiplexed with serial (Ctrl-A C)
bash scripts/run-k3.sh --rootfs /path/to/bianbu-rootfs.img --console
# Optional TCP QEMU monitor on loopback 4444
bash scripts/run-k3.sh --rootfs /path/to/bianbu-rootfs.img --monitor
# Use another installation location:
bash scripts/run-k3.sh --prefix /opt/jupiter2-k3 --rootfs /path/to/bianbu-rootfs.img
```

The boot configuration uses `-M jupiter2`, `rva23s64,k3-pmacfg=true,pmp=true,num-pmp-regions=16,pmp-granularity=16`, 16 Harts, 2 GiB RAM, virtio block/network, 9P host share, and serial Linux console. The rootfs image is **not** distributed in this repository. Port forwarding and mount commands may require guest adjustments.

## Verification status

Saving patched source trees and the Linux import succeeded. **Successful full QEMU + Keystone SM + OpenSBI + Linux compilation and an actual Linux boot have not been established by this setup.** The firmware build scripts perform a diagnostic SM compile/link and must not be treated as evidence of secure enclave functionality or hardware PMP/IOPMP safety. Inspect build logs and fail closed on build errors before using generated images.

## Clean build intermediates after a failed build

The integration build uses `.build-k3/{qemu,firmware,linux}` by default. To clean all three components:

```bash
bash scripts/clean-k3.sh
```

Or clean only the failed component (for example, the OpenSBI/Keystone firmware build):

```bash
bash scripts/clean-k3.sh --only firmware
```

Use `--only qemu` or `--only linux` for the other components. Preview deletions with `--dry-run`. If building with `--build-dir /path/to/work`, provide the same option to the clean script. Custom build directories must contain the `.k3-build-root` marker created by `build-install-k3.sh`.

The script **never removes** source trees, patches, rootfs files, or files installed to `bin/`, `firmware/k3/`, `boot/k3/`, or `lib/modules/`. Stop running builds before cleaning. This is not a full uninstall.
