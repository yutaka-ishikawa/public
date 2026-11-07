# K3 OpenSBI / Keystone / QEMU — Milk-V Jupiter 2

Public integration repository for three **separately forked** upstream source trees, pinned to the K3 port revisions.

## Current status (2026-11-07)

- GitHub Actions workflow: installed; tests QEMU and OpenSBI+Keystone independently.
- Forked and patched source branches: **not yet verified / not yet created by this automation**.
- **No confirmed successful GitHub Actions builds.**
- No actual enclave execution or PMP/IOPMP security verification is claimed.

## Fork upstream sources, apply patches, and push branches

On a machine with `git` and GitHub CLI (`gh`) installed, download and extract `K3_GitHub_Actions_20261107.zip` and run:

```bash
gh auth login
git clone https://github.com/yutaka-ishikawa/public.git k3-integration
cd k3-integration
PATCH_ROOT=/absolute/path/to/K3_GitHub_Actions_20261107 \
  bash scripts/fork_patch_push.sh
```

The script creates or verifies real GitHub forks, checks pristine versions, applies each provided patch exactly once, and commits to branch `k3-patched-20261107`. Fork sources:

| Fork target | Upstream | Pinned base |
| --- | --- | --- |
| `yutaka-ishikawa/qemu` | `qemu/qemu` (official GitHub mirror) | `v11.1.3` |
| `yutaka-ishikawa/opensbi` | `spacemit-com/opensbi` | `ad6091fd16c4d4a445168f6a479e1b8aadb4931c` |
| `yutaka-ishikawa/keystone` | `keystone-enclave/keystone` | `b8c2385355b2afb5b94a4e5dc503936f1502b972` |

After all three pushes, navigate to **Actions > K3 fork build validation > Run workflow**. GitHub Actions checks patched QEMU compilation and the Jupiter2 machine, Keystone SM compilation and K3 OpenSBI firmware link. Linux runtime testing needs a compatible kernel and rootfs and is **not** part of this workflow.

**Do not flash diagnostic firmware to real hardware** without independent platform safety review. 
