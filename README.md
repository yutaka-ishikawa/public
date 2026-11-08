# Milk-V Jupiter 2 / SpacemiT K3 integrated source tree

The `main` branch contains `qemu/`, `opensbi/`, `keystone/`, and `linux-k3/`. Original patches are under `patches/`.

独立したアプリ開発用サンプルは、[samples/helloworld/README.md](samples/helloworld/README.md)を参照してください。通常のMakefileでhelloをビルド・パッケージ化できます。

実機情報と現在のファームウェアの適合性は、[Milk-V Jupiter 2実機調査](doc/jupiter2-hardware-compatibility.md)にまとめています。

未移植機能の一覧と実装順序は、[K3 Keystone移植計画](doc/keystone-porting-plan.md)を参照してください。

SBI 2001–2004のソース検証・テスト・結果は、[verifications/README.md](verifications/README.md)にまとめています。

検証の意味と実際のQEMUゲストでのテスト方法は、[検証範囲とQEMU実行手順](doc/keystone-verification-methods.md)を参照してください。

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

This builds the SDK, Eyrie and `hello.ke`/`hello-native.ke` using the distro RISC-V toolchain. It reuses K3 QEMU/kernel artifacts from `install/bianbu/` when present, builds missing components using the existing K3 scripts, and always rebuilds firmware from the current SM/OpenSBI sources. The OpenSBI link also tracks the external SM object so an SM-only update cannot silently reuse an older firmware ELF. It builds `keystone-driver.ko` against the **same kernel build output** as the installed Image and creates a separate rootfs using `scripts/build-rootfs-keystone.sh`. The Bianbu guest filesystem is not modified.

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

### Host filesystem mounts, guest tools and shutdown

New Keystone images contain `/usr/bin/run-keystone-hello`, `/usr/bin/mount-hostshare`, `shutdown`, `poweroff` and `reboot`, plus `insmod`, `rmmod`, `lsmod`, `modprobe`, `depmod`, `modinfo`, `ip`, `ping`, `ping6`, `ifconfig`, `route`, `netstat` and `hostname`. Static BusyBox provides the module-loading, mount and power-control operations without requiring kmod or a running init daemon. From the guest:

```bash
mkdir -p /proc /sys
mountpoint -q /proc || mount -t proc proc /proc
mountpoint -q /sys || mount -t sysfs sysfs /sys
mount-hostshare                       # mount hostshare at /mnt/host
mount-hostshare /mnt/project          # alternative guest mount point
/usr/bin/run-keystone-hello
shutdown -h now                       # sync, remount root read-only, power off
# Alternatively: poweroff, reboot, or shutdown -r now
```

With the default `init=/bin/bash`, mount `/proc` and `/sys` after each boot.
If `lsmod` reports that `/proc/modules` is missing, mount procfs as above;
see [guest command troubleshooting](doc/keystone-guest-commands.md).

The shutdown helper supports only immediate shutdown (`now` or `0`), and cancels if the root filesystem cannot be remounted read-only. This behavior is intended for the default diagnostic `init=/bin/bash` boot. QEMU exposes the repository by default; choose another host directory with `bash scripts/run-k3.sh --hostfs /path/to/share`.

If shutdown stops at `sbi_hsm_exit: ERR: The hart is in invalid state [0]`, update the firmware as described in [Keystone system reset fix](doc/keystone-system-reset.md). This fixes the fixed-online HSM/reset mismatch and enables QEMU syscon poweroff/reboot; it does not require rebuilding the guest image.


If an existing image lacks the helper files or `insmod`, update it without rebuilding. On the host, from the repository root:

```bash
git pull
bash scripts/build-keystone-guest-tools.sh
```

In the running guest, bootstrap the share using the existing mount command (the launcher must share the repository):

```bash
mkdir -p /mnt/host
mountpoint -q /mnt/host || mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host
bash /mnt/host/scripts/install-keystone-guest-tools.sh
/usr/bin/run-keystone-hello
shutdown -h now
```

The installer copies only guest helper tools. It reports if `hello.ke` is absent: in that case, build/boot a Keystone rootfs containing the examples and matching driver. Installing the helper alone does not add examples to a plain Bianbu image.

For a fresh image, retain the existing image and build into a new destination:

```bash
bash scripts/build-install-keystone.sh \
  --base-prefix "$PWD/install/keystone" --prefix "$PWD/install/keystone-v2"
bash scripts/run-k3.sh --prefix "$PWD/install/keystone-v2"
```

### Eyrie link errors about stack canaries

If an older checkout fails to link Eyrie with undefined `__stack_chk_guard` or `__stack_chk_fail`, update with `git pull` and rerun the same Keystone build command. Eyrie, its freestanding SDK app/edge libraries, and the native enclave example explicitly disable compiler-generated stack protection because they have no libc/canary provider. Host runners retain the toolchain's normal protection settings. The build refreshes generated Eyrie sources and rebuilds affected objects; deleting the existing guest images is unnecessary.

### Updating the SM/OpenSBI firmware without rebuilding a guest image

Stop the guest first, then rebuild into the installation directory you actually boot:

```bash
git pull
bash scripts/build-install-k3.sh --only firmware --prefix "$PWD/install/keystone"
bash scripts/run-k3.sh --image keystone
```

For another installation (for example `install/keystone-v2`), use that same directory for both the build's and launcher's `--prefix`. No rootfs replacement or kernel rebuild is needed for the SM trap-return/ABI fixes.

With `CONFIG_K3_KEYSTONE_SM`, firmware deliberately uses the machine-timer path required by the current SM. It clears Sstc access and removes `sstc` from both device-tree ISA encodings, so Linux and SBI SET_TIMER agree on that policy. This is a compatibility policy, not full Sstc context virtualization. Firmware builds without Keystone enabled keep their existing timer behavior.

### RUN reaches the enclave entry but the raw loader fails

Distro PIE defaults previously caused the raw loader's first `la sp, _estack` to read an absolute address from a GOT. `loader.bin` contains only `.text`, so that GOT was absent; zero-filled EPM padding supplied a zero stack pointer. Relocating that image to `0x10f400000` reproduced a fault at `0x10f40000c` with SP `0xffffffffffffffc8`, before `load_runtime` could run.

The loader now uses local PC-relative address loads, disables PIE/PIC and linker relaxation for this freestanding path, includes small data/BSS in the raw image, and enforces a 16-byte-aligned stack. A linker assertion rejects any GOT in the raw loader. The Eyrie build always checks its generated build tree, so source fixes reach packages even after an earlier successful build.

This fix is in the example packages, **not just the firmware**. To update an existing guest, first stop the stuck QEMU instance. On the host:

```bash
git pull
bash scripts/build-install-keystone.sh --only sdk
bash scripts/run-k3.sh
```

In the restarted guest, use the repository host share and replace both packages:

```bash
mount-hostshare
cp /mnt/host/.build-keystone/examples/hello/hello.ke /usr/share/keystone/examples/hello.ke
cp /mnt/host/.build-keystone/examples/hello-native/hello-native.ke /usr/share/keystone/examples/hello-native.ke
chmod +x /usr/share/keystone/examples/*.ke
run-keystone-hello
```

For a custom `--build-dir` or `--hostfs`, adjust the shared package paths. A fresh `build-install-keystone.sh` image includes the rebuilt packages automatically. No kernel or rootfs rebuild is needed for this package update. The entry regression tests execute the actual raw loader prefix at a relocated address under QEMU user mode and stop before privileged loader code; they do not replace a full guest enclave test.

### Illegal instruction at the Eyrie virtual entry

原因・GDBの証拠・修正内容・更新手順の詳細は、[Keystone Eyrieの入口でGNU build-idを実行して停止した問題](doc/keystone-eyrie-entry-build-id.md)を参照してください。

An observed `mepc=0xffffffffc0000000`, `mcause=2`, `mtval=4` was accompanied by the words `00000004 00000014 00000003 00554e47` at the entry. These are a GNU build-id note header, not runtime instructions. The loader enters a fixed virtual address; an orphan `.note.gnu.build-id` placed there by the linker made it execute metadata instead of `_start`.

The runtime now explicitly places its dedicated `_start` section first, discards the build-id note, declares the ELF entry, and asserts that `_start` equals the fixed runtime base. Update the SDK and copy both rebuilt example packages into the guest using the commands above. This change does not require rebuilding OpenSBI or replacing the rootfs. The runtime entry tests link with build-id generation enabled and inspect the executable PT_LOAD bytes of both example ELFs; complete enclave execution still requires a guest test.

### Locate a CREATE/RUN hang before changing SM behavior

An RCU stall is Linux's later report that a CPU stopped making progress; it does not identify the timer as the cause of a stuck `run-keystone-hello`. Enable opt-in firmware progress markers to locate the last completed stage:

```bash
KEYSTONE_SM_TRACE=1 bash scripts/build-install-k3.sh --only firmware \
  --prefix "$PWD/install/keystone"
bash scripts/run-k3.sh
```

Stop the old guest before rebuilding and use the actual installation path for both commands if it differs. At boot, confirm the `[SM-TRACE] ... boot trace enabled; built ...` marker, then run `run-keystone-hello` in the guest. Capture the boot marker and all `[SM-TRACE]` lines through the stall. They distinguish SBI entry, CREATE argument copying, PMP broadcast/ACK, shared-memory cleanup, hashing, and RUN context switching. An entry with `a=2001` is CREATE and `a=2003` is RUN; `sbi.return` records status and `sbi.switch-return` records the selected return PC. These markers contain no memory contents or key material.

No SBI entry after a confirmed trace-enabled boot points to an earlier user/kernel path; a final `CREATE args-copy begin` narrows the issue to the copy/trap path; `CREATE pmp-global begin` narrows it to PMP/IPI handling. A hash-start marker without completion also permits slow M-mode hashing as an explanation, rather than assuming an infinite loop. The trace is disabled unless `KEYSTONE_SM_TRACE=1`; rebuild with `KEYSTONE_SM_TRACE=0` to remove it. It is diagnostic logging, not a timeout increase or a bypass of PMP checks.

## QEMU startup: Keystone is the default

```bash
bash scripts/run-k3.sh                         # install/keystone/
bash scripts/run-k3.sh --image keystone
bash scripts/run-k3.sh --image bianbu           # install/bianbu/
# A custom version installation directory:
bash scripts/run-k3.sh --image bianbu --prefix /path/to/bianbu
# Previously installed root-level Bianbu artifacts:
bash scripts/run-k3.sh --image bianbu --prefix "$PWD"
```

`--image` selects the default boot-file set; it does not rebuild anything. Both profiles use `jupiter2`, the K3 CPU configuration, 16 harts and 2 GiB RAM. Each selects its own QEMU, `firmware/k3/fw_dynamic.bin`, `boot/k3/Image`, and rootfs from the version installation directory. Individual files can be overridden:

```bash
bash scripts/run-k3.sh --image keystone \
  --qemu /path/to/qemu-system-riscv64 \
  --bios /path/to/fw_dynamic.bin \
  --kernel /path/to/Image \
  --rootfs /path/to/keystone-rootfs.img
```

`--gdb` pauses at reset on loopback port 1234; `--console` enables the serial/monitor multiplexer; `--monitor` opens a loopback monitor on port 4444. `--smp`, `--memory`, `--hostfs` and `--append` remain available. The default command line uses `init=/bin/bash`, giving a diagnostic guest shell rather than starting the distribution init system.

In the Keystone guest shell:

```bash
/usr/bin/run-keystone-hello
# After the driver has been loaded:
/usr/share/keystone/examples/hello-native.ke
```

The helper mounts `/proc` and `/sys`, loads the driver matching `uname -r`, then runs `hello.ke`. Since the base rootfs may lack `insmod`, new Keystone images include a statically linked RISC-V BusyBox with module and network applets at `/usr/libexec/keystone/busybox`. The helper uses it when the system command is unavailable. The build downloads BusyBox 1.36.1 from a pinned, SHA-256-verified archive on `codeload.github.com`; sources remain in the guest-tools build directory. Packaging an image or loading a module does not establish that SM enclave creation succeeds. Current K3 code separates basic PMP-only execution from unavailable secure key/entropy services; attestation and sealing must not be inferred to work from a hello test.

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

Keystoneの大容量EPM（256 MiB）には、修正版ドライバと起動時のCMA予約を使う。
[設定・ビルド・実ゲスト検証手順](verifications/epm-allocation/README.md)を参照。

For module/network commands, updating an existing image and `depmod` usage, see
[Keystone guest commands](doc/keystone-guest-commands.md).
