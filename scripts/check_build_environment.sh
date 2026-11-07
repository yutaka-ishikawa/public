#!/usr/bin/env bash
set -Eeuo pipefail
if [ "$#" -ne 2 ]; then echo "Usage: $0 keystone_source opensbi_source" >&2; exit 2; fi
for command in python3 clang ld.lld readelf nm git make; do
  if command -v "$command" >/dev/null; then printf 'OK   %s: %s\n' "$command" "$(command -v "$command")"; else printf 'FAIL %s\n' "$command"; exit 1; fi
done
for f in "$1/sm/tools/k3_rv64_compile_audit.py" "$1/sm/src/objects.mk" "$2/platform/generic/configs/k3_keystone_audit_defconfig"; do
  test -f "$f" || { echo "FAIL missing file $f"; exit 1; }
  printf 'OK   %s\n' "$f"
done
