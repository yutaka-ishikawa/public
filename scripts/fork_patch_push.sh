#!/usr/bin/env bash
# Run in an authenticated GitHub CLI shell with the K3 package extracted.
set -Eeuo pipefail
: "${GITHUB_OWNER:=yutaka-ishikawa}"
: "${PATCH_ROOT:?Set PATCH_ROOT to the extracted K3_GitHub_Actions_20261107 directory}"
: "${WORKDIR:=$PWD/k3-fork-work}"
: "${BRANCH:=k3-patched-20261107}"
: "${QEMU_REF:=v11.1.3}"
: "${OPENSBI_SHA:=ad6091fd16c4d4a445168f6a479e1b8aadb4931c}"
: "${KEYSTONE_SHA:=b8c2385355b2afb5b94a4e5dc503936f1502b972}"
command -v gh >/dev/null || { echo 'Install GitHub CLI (gh)' >&2; exit 2; }
command -v git >/dev/null || { echo 'Install git' >&2; exit 2; }
gh auth status
PATCH_ROOT=$(realpath -e "$PATCH_ROOT")
mkdir -p "$WORKDIR"
WORKDIR=$(realpath -e "$WORKDIR")
fork_one() {
  local upstream="$1" target="$2" ref="$3" patch="$4"
  local fork="$GITHUB_OWNER/$target" src="$WORKDIR/$target"
  test -s "$PATCH_ROOT/patches/$patch" || { echo "Missing patch: $patch" >&2; exit 2; }
  if gh repo view "$fork" >/dev/null 2>&1; then
    echo "Existing fork/repository: $fork"
    local parent
    parent=$(gh repo view "$fork" --json parent --jq '.parent.nameWithOwner // ""')
    if [ "$parent" != "$upstream" ]; then echo "ERROR: $fork is not a fork of $upstream (parent=$parent)" >&2; exit 3; fi
  else
    gh repo fork "$upstream" --clone=false
  fi
  if [ -e "$src" ]; then echo "ERROR: $src already exists; use clean WORKDIR" >&2; exit 3; fi
  git clone --filter=blob:none "https://github.com/$fork.git" "$src"
  git -C "$src" remote add upstream "https://github.com/$upstream.git"
  git -C "$src" fetch --no-tags upstream "$ref"
  git -C "$src" checkout -b "$BRANCH" FETCH_HEAD
  echo "Base for $fork: $(git -C "$src" rev-parse HEAD)"
  if [ "$target" != "qemu" ]; then test "$(git -C "$src" rev-parse HEAD)" = "$ref"; fi
  git -C "$src" apply --check "$PATCH_ROOT/patches/$patch"
  git -C "$src" apply "$PATCH_ROOT/patches/$patch"
  git -C "$src" apply --reverse --check "$PATCH_ROOT/patches/$patch"
  git -C "$src" add -A
  if git -C "$src" diff --cached --quiet; then echo "ERROR: patch made no changes" >&2; exit 4; fi
  git -C "$src" -c user.name="K3 CI Setup" -c user.email="k3-ci-setup@users.noreply.github.com" commit -m "Apply K3 2026-11-07 cumulative patch"
  git -C "$src" push -u origin "$BRANCH"
  echo "Pushed https://github.com/$fork/tree/$BRANCH"
}
fork_one qemu/qemu qemu "$QEMU_REF" qemu-11.1.3-jupiter2-k3-boot.patch
fork_one spacemit-com/opensbi opensbi "$OPENSBI_SHA" opensbi-k3-keystone-cumulative.patch
fork_one keystone-enclave/keystone keystone "$KEYSTONE_SHA" keystone-k3-cumulative.patch
echo "All 3 fork branches created and patches committed. Run Actions workflow in $GITHUB_OWNER/public."
