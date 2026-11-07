# K3 OpenSBI / Keystone / QEMU (Milk-V Jupiter 2)

Diagnostic GitHub Actions build environment for SpacemiT K3, QEMU 11.1.3 and Keystone Secure Monitor.

## Status

The workflow and four build/check scripts have been uploaded. **Three cumulative patch files are still missing**, so the workflow is intentionally fail-fast and not yet runnable to completion. No successful build or functional verification is claimed.

## Required files

Upload the following three files from `K3_GitHub_Actions_20261107.zip` into the repository with the exact paths:

- `patches/qemu-11.1.3-jupiter2-k3-boot.patch`
- `patches/opensbi-k3-keystone-cumulative.patch`
- `patches/keystone-k3-cumulative.patch`

Then open **Actions → K3 OpenSBI Keystone QEMU diagnostic build → Run workflow**.

## Source revisions

- QEMU: `v11.1.3`
- SpacemiT OpenSBI: `ad6091fd16c4d4a445168f6a479e1b8aadb4931c`
- Keystone: `b8c2385355b2afb5b94a4e5dc503936f1502b972`

The build creates diagnostic artifacts if all stages succeed, but firmware link success alone does **not** establish enclave operation, PMP/IOPMP safety, or hardware compatibility. Do not flash an unvalidated firmware image to the Milk-V Jupiter 2.
