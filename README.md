# Milk-V Jupiter 2 (SpacemiT K3): QEMU / OpenSBI / Keystone CI

**Source strategy: clone upstream, apply our committed patch files, then build in GitHub Actions.** No forks are needed. This integration repository contains the patch set, scripts, and workflow; upstream source checkouts and patched trees live only in a temporary GitHub Actions workspace.

## Sources and patches

| Software | Upstream repository | Pinned version | Applied patch |
|---|---|---|---|
| QEMU | https://github.com/qemu/qemu | tag `v11.1.3` | `patches/qemu-11.1.3-jupiter2-k3-boot.patch` |
| OpenSBI (K3) | https://github.com/spacemit-com/opensbi | `ad6091fd16c4d4a445168f6a479e1b8aadb4931c` | `patches/opensbi-k3-keystone-cumulative.patch` |
| Keystone | https://github.com/keystone-enclave/keystone | `b8c2385355b2afb5b94a4e5dc503936f1502b972` | `patches/keystone-k3-cumulative.patch` |

## How to run

Open **https://github.com/yutaka-ishikawa/public/actions**, select **K3 upstream clone patch and build**, and inspect the run triggered by changes to `.github/workflows/k3-build.yml`. The workflow can also be run manually via **Run workflow**.

The QEMU job runs `git apply --check`, applies its patch, builds `qemu-system-riscv64`, and checks that `jupiter2` is listed as a machine.

The OpenSBI+Keystone job clones both source trees, checks patch preimages, applies patches, compiles SM objects, and attempts an OpenSBI `fw_dynamic.elf`/`fw_dynamic.bin` link. Diagnostic artifacts are uploaded if available.

## Important distinction

A registered workflow or a clean patch application **does not demonstrate a successful build**. Only completed GitHub Actions runs with successful build steps establish that. A successful firmware build also does **not** establish that it can be safely flashed or that PMP/IOPMP/enclave operation is correct on real hardware.

This CI does not boot a Linux kernel or exercise real 16-Hart PMP/HSM behavior.

`scripts/fork_patch_push.sh` is retained as an optional separate workflow, but **is not used by CI**.
