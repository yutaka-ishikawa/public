# Milk-V Jupiter 2 / SpacemiT K3 integrated source tree

The `main` branch contains `qemu/`, `opensbi/`, `keystone/`, and `linux-k3/`. Original patches are under `patches/`.

## Host dependencies

On Ubuntu 24.04 or an equivalent APT-based distribution:

```bash
sudo bash scripts/install-build-deps.sh
```

This installs the QEMU/kernel build dependencies, Clang/LLD/LLVM, RISC-V GCC/G++, cross libc development files, CMake, makeself, e2fsprogs and fakeroot. Run the builds below as your normal user. Older mke2fs versions use directory input under fakeroot; no e2fsprogs upgrade is required.

## Separate Bianbu and Keystone installations

Both versions use the **same K3 QEMU machine, Keystone-capable OpenSBI and K3 Linux sources**. The Keystone version adds the matching Linux driver and basic SDK/Eyrie examples to a separate Bianbu-based rootfs. It does not switch to upstream Keystone's QEMU `virt` platform.

| Version | Build script | Default installation | Guest filesystem |
| --- | --- | --- | --- |
| Bianbu | `scripts/build-install-k3.sh` | `install/bianbu/` | `rootfs/bianbu-rootfs.img` |
| Keystone | `scripts/build-install-keystone.sh` | `install/keystone/` | `rootfs/keystone-rootfs.img` |

### Bianbu

The existing image builder remains available:

```bash
bash scripts/build-install-k3.sh
# Just create the 8 GiB Bianbu filesystem:
bash scripts/build-install-k3.sh --only rootfs
# A local archive or alternative installation directory:
bash scripts/build-install-k3.sh --prefix /path/to/bianbu --jobs 5 \
  --rootfs-archive /path/to/bianbu-base-26.04-base-riscv64.tar.gz
```

`--only qemu`, `--only firmware`, and `--only linux` are also available. Build intermediates remain in `.build-k3/`; installed files now go to `install/bianbu/`, rather than the repository root. Previously installed root-level files are left untouched.

`rootfs/bianbu-rootfs.img` is an 8 GiB sparse ext4 filesystem, labeled `bianbu-root`, populated from [Bianbu 26.04 base for RISC-V](https://archive.spacemit.com/bianbu-base/bianbu-base-26.04-base-riscv64.tar.gz). Existing nonempty images are retained. Downloads use HTTPS with certificate verification; `--rootfs-sha256 HASH` checks an independently obtained checksum. No distributor checksum is bundled. Older mke2fs builds use extraction and directory input under fakeroot, retaining numeric owners, permission bits, links and device nodes; extended ACLs and file capabilities are not restored by that fallback.

### Keystone

```bash
bash scripts/build-install-keystone.sh --jobs 5
```

This builds the SDK, Eyrie and `hello.ke`/`hello-native.ke` using the distro RISC-V toolchain. It reuses K3 QEMU/firmware/kernel artifacts from `install/bianbu/` when present, and builds missing components using the existing K3 scripts. It builds `keystone-driver.ko` against the **same kernel build output** as the installed Image and creates a separate rootfs using `scripts/build-rootfs-keystone.sh`. The Bianbu guest filesystem is not modified.

Keystone build outputs live in `.build-keystone/`; the shared K3 build output remains `.build-k3/`. The rootfs overlay is populated without loop mounts or sudo and every imported file's content is verified. A pre-existing Keystone image is never overwritten: choose a new `--prefix` for a fresh installation. Generated SDK tool aliases and CMake compatibility flags avoid changing the system toolchain.

If Bianbu was previously installed at the repository root, reuse it explicitly:

```bash
bash scripts/build-install-keystone.sh --base-prefix "$PWD" \
  --k3-build-dir "$PWD/.build-k3" --jobs 5
```

Other options include `--prefix DIR`, `--build-dir DIR`, `--rootfs-archive FILE`, `--rootfs-sha256 HASH`. The directory passed to `--k3-build-dir` must contain `linux/` with the matching Image and Module.symvers. For SDK/example development without creating a guest image:

```bash
bash scripts/build-install-keystone.sh --only sdk
```

### Existing images reporting `insmod: not found`

For an already-running guest, a full image rebuild is unnecessary. On the host, from the repository root:

```bash
git pull
bash scripts/build-keystone-guest-tools.sh
```

In the guest, when using the launcher's default repository host share:

```bash
mkdir -p /mnt/host
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host
/mnt/host/.build-keystone/guest-tools/busybox insmod \
  "/usr/lib/modules/$(uname -r)/extra/keystone-driver.ko"
/usr/bin/run-keystone-hello
```

For a fresh image with the loader included, keep the existing image and build into a new destination:

```bash
bash scripts/build-install-keystone.sh \
  --base-prefix "$PWD/install/keystone" --prefix "$PWD/install/keystone-v2"
bash run-k3.sh --prefix "$PWD/install/keystone-v2"
```

### Eyrie link errors about stack canaries

If an older checkout fails to link Eyrie with undefined `__stack_chk_guard` or `__stack_chk_fail`, update with `git pull` and rerun the same Keystone build command. Eyrie, its freestanding SDK app/edge libraries, and the native enclave example explicitly disable compiler-generated stack protection because they have no libc/canary provider. Host runners retain the toolchain's normal protection settings. The build refreshes generated Eyrie sources and rebuilds affected objects; deleting the existing guest images is unnecessary.

## QEMU startup: Keystone is the default

```bash
bash run-k3.sh                         # install/keystone/
bash run-k3.sh --image keystone
bash run-k3.sh --image bianbu           # install/bianbu/
# A custom version installation directory:
bash run-k3.sh --image bianbu --prefix /path/to/bianbu
# Previously installed root-level Bianbu artifacts:
bash run-k3.sh --image bianbu --prefix "$PWD"
```

`--image` selects the default boot-file set; it does not rebuild anything. Both profiles use `jupiter2`, the K3 CPU configuration, 16 harts and 2 GiB RAM. Each selects its own QEMU, `firmware/k3/fw_dynamic.bin`, `boot/k3/Image`, and rootfs from the version installation directory. Individual files can be overridden:

```bash
bash run-k3.sh --image keystone \
  --qemu /path/to/qemu-system-riscv64 \
  --bios /path/to/fw_dynamic.bin \
  --kernel /path/to/Image \
  --rootfs /path/to/keystone-rootfs.img
```

`bash scripts/run-k3.sh` is equivalent. `--gdb` pauses at reset on loopback port 1234; `--console` enables the serial/monitor multiplexer; `--monitor` opens a loopback monitor on port 4444. `--smp`, `--memory`, `--hostfs` and `--append` remain available. The default command line uses `init=/bin/bash`, giving a diagnostic guest shell rather than starting the distribution init system.

In the Keystone guest shell:

```bash
/usr/bin/run-keystone-hello
# After the driver has been loaded:
/usr/share/keystone/examples/hello-native.ke
```

The helper mounts `/proc` and `/sys`, loads the driver matching `uname -r`, then runs `hello.ke`. Since the base rootfs may lack `insmod`, new Keystone images include a statically linked RISC-V BusyBox with `insmod` and `lsmod` at `/usr/libexec/keystone/busybox`. The helper uses it when the system command is unavailable. The build downloads BusyBox 1.36.1 from a pinned, SHA-256-verified archive on `codeload.github.com`; sources remain in the guest-tools build directory. Packaging an image or loading a module does not establish that SM enclave creation succeeds. Current K3 code separates basic PMP-only execution from unavailable secure key/entropy services; attestation and sealing must not be inferred to work from a hello test.

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
