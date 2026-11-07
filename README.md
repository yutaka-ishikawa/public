# Milk-V Jupiter 2 (SpacemiT K3) integration

## Saved patched source trees (2026-11-07)

Three patched source-code snapshots have been pushed successfully and independently verified on GitHub:

- [QEMU 11.1.3](https://github.com/yutaka-ishikawa/public/tree/sources/qemu-k3) — upstream `qemu/qemu` tag `v11.1.3`, file `hw/riscv/jupiter2.c` verified.
- [K3 OpenSBI](https://github.com/yutaka-ishikawa/public/tree/sources/opensbi-k3) — upstream `spacemit-com/opensbi` at `ad6091fd16c4d4a445168f6a479e1b8aadb4931c`; `platform/generic/configs/k3_keystone_audit_defconfig` verified.
- [Keystone SM](https://github.com/yutaka-ishikawa/public/tree/sources/keystone-k3) — upstream `keystone-enclave/keystone` at `b8c2385355b2afb5b94a4e5dc503936f1502b972`; `sm/tools/k3_rv64_compile_audit.py` verified.

The patches themselves are retained on `main` under `patches/` along with GitHub Actions and build scripts. The OpenSBI patch's trailing whitespace was normalized at application time; original patch files remain unchanged.

**Important:** These are *source snapshots*, not complete upstream Git histories. Original upstream SHAs are recorded in the corresponding GitHub Actions run artifacts. GitHub's Actions token rejected pushing upstream `.github/workflows` under its original path, so the source workflow files are retained under `upstream-github-workflows/` in the affected snapshots.

## Actions

- [Publish OpenSBI and Keystone snapshots — passed](https://github.com/yutaka-ishikawa/public/actions/runs/42330102326)
- [Publish QEMU snapshot — passed](https://github.com/yutaka-ishikawa/public/actions/runs/42330183299)
- [Build workflow](https://github.com/yutaka-ishikawa/public/blob/main/.github/workflows/k3-build.yml) — separate diagnostic build, **not yet verified successful**.

**Do not flash firmware from this experimental port until hardware PMP/IOPMP and firmware safety tests are completed.**

## K3 Linux 6.18 source and kernel build

- [Patched K3 Linux source](linux-k3/) — vendor `spacemit-com/linux-6.18`, branch `k3-br-v1.0.y`, SHA `7d17e358c660f1046eb1f27f26eea33955813afd`.
- [Linux build script](scripts/build-linux-k3.sh) — builds `Image dtbs modules` with the requested VirtIO, generic PCI host, networking, and 9P options.
- [Linux kernel build workflow](.github/workflows/build-linux-k3.yml) — Ubuntu 24.04 RISC-V cross-build with artifacts for kernel Image, DTBs, modules, and effective Kconfig.

Run locally at repository root (after installing `gcc-riscv64-linux-gnu`, `binutils-riscv64-linux-gnu`, `bc`, `bison`, `flex`, `libssl-dev`, `libelf-dev`, `dwarves`):

```bash
JOBS=15 bash scripts/build-linux-k3.sh
```

Outputs are under `linux-k3/build-k3/`. This generates a kernel `Image`, not a bootable disk/rootfs image. On GitHub Actions, built artifacts are downloadable from the workflow run once the job completes successfully.

Linux vendor source has been stored with requested config changes in `arch/riscv/configs/k3_bianbu_defconfig`; no additional Linux source patch file was provided for this step.
