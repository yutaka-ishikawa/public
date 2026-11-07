#!/usr/bin/env bash
# Clean build intermediates created by scripts/build-install-k3.sh.
# Does not delete source code, patches, rootfs, or installed binaries.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD_DIR="$ROOT/.build-k3"
COMPONENT=all
DRY_RUN=0
usage() {
cat <<'HELP'
Usage: bash scripts/clean-k3.sh [--only qemu|firmware|linux] [--build-dir DIR] [--dry-run]
  --only NAME       Clean just one component; default: all
  --build-dir DIR   Build directory used with build-install-k3.sh (default .build-k3)
  --dry-run         Show directories that would be removed without deleting anything
  --help            Show usage

Removes build intermediates only: BUILD_DIR/{qemu,firmware,linux}.
Does NOT remove installation outputs (bin/, firmware/k3/, boot/k3/, lib/modules/)
or source directories (qemu/, opensbi/, keystone/, linux-k3/).
Stop any running build before cleaning.
HELP
}
while (($#)); do
  case "$1" in
    --only|--build-dir)
      (($#>=2)) || { usage >&2; exit 2; }
      if [[ "$1" == --only ]]; then COMPONENT=$2; else BUILD_DIR=$2; fi
      shift 2;;
    --dry-run) DRY_RUN=1; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2;;
  esac
done
case "$COMPONENT" in all|qemu|firmware|linux) :;; *) echo "Invalid component: $COMPONENT" >&2; exit 2;; esac
command -v python3 >/dev/null || { echo 'python3 is required for safe path validation' >&2; exit 2; }
# Resolve and validate without creating any directory during cleanup.
BUILD_DIR="$(python3 - "$ROOT" "$BUILD_DIR" <<'PY'
import os,sys
root=os.path.realpath(sys.argv[1]); build=os.path.realpath(sys.argv[2])
protected=[root, '/', os.path.join(root,'qemu'),os.path.join(root,'opensbi'),
           os.path.join(root,'keystone'),os.path.join(root,'linux-k3'),
           os.path.join(root,'patches'),os.path.join(root,'scripts'),os.path.join(root,'.github')]
if any(build==p or build.startswith(p+os.sep) for p in protected):
    raise SystemExit('Refusing unsafe build directory: '+build)
print(build)
PY
)"
if [[ ! -d "$BUILD_DIR" ]]; then
  echo "No build directory exists: $BUILD_DIR (nothing to clean)"
  exit 0
fi
if [[ "$COMPONENT" == all ]]; then
  names=(qemu firmware linux)
else
  names=("$COMPONENT")
fi
for name in "${names[@]}"; do
  target="$BUILD_DIR/$name"
  # Refuse symlinks, including a replaced/symlinked component path.
  if [[ -L "$target" ]]; then
    echo "Refusing symlinked build component: $target" >&2
    exit 3
  fi
  if [[ ! -e "$target" ]]; then
    echo "Already absent: $target"
    continue
  fi
  if [[ ! -d "$target" ]]; then
    echo "Unexpected non-directory component: $target" >&2
    exit 3
  fi
  if ((DRY_RUN)); then
    echo "Would remove: $target"
  else
    rm -rf -- "$target"
    echo "Removed: $target"
  fi
done
echo "Installed outputs and source directories were not touched."
