#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 4 ]; then echo "Usage: $0 PKG_ROOT QEMU_ROOT OPENSBI_ROOT KEYSTONE_ROOT" >&2; exit 2; fi
PKG=$(realpath "$1")
for x in "$2" "$3" "$4"; do [ -d "$x" ] || { echo "Missing: $x" >&2; exit 2; }; done
check() {
  local label="$1" repo="$2" patch="$3" expected="$4"
  echo "=== $label ==="
  (cd "$repo"
   if [ -n "$expected" ]; then
     HEAD=$(git rev-parse HEAD); [ "$HEAD" = "$expected" ] || { echo "Unexpected HEAD: $HEAD" >&2; exit 1; }
   fi
   [ -z "$(git status --porcelain)" ] || { echo "Tree is not clean" >&2; exit 1; }
   git apply --check "$PKG/patches/$patch"
   echo "PASS: clean preimage and patch check"
  )
}
check QEMU "$2" qemu-11.1.3-jupiter2-k3-boot.patch ''
check OpenSBI "$3" opensbi-k3-keystone-cumulative.patch ad6091fd16c4d4a445168f6a479e1b8aadb4931c
check Keystone "$4" keystone-k3-cumulative.patch b8c2385355b2afb5b94a4e5dc503936f1502b972
