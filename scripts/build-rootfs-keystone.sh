#!/usr/bin/env bash
# Preserve the Bianbu builder; add a Keystone guest overlay to a separate image.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
if (($# < 3 || $# > 5)); then
 echo "Usage: $0 OUTPUT_IMAGE BUILD_DIR GUEST_OVERLAY [BIANBU_ARCHIVE] [SHA256]" >&2; exit 2
fi
IMAGE=$1; WORK=$2; OVERLAY=$3; ARCHIVE=${4:-}; HASH=${5:-}
[[ ! -e $IMAGE && ! -L $IMAGE ]] || { echo "Refusing to overwrite guest image: $IMAGE" >&2; exit 3; }
test -d "$OVERLAY/usr/share/keystone/examples" || { echo 'Missing Keystone examples overlay' >&2; exit 3; }
for tool in python3 debugfs tune2fs e2fsck; do command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 3; }; done
mkdir -p "$WORK" "$(dirname "$IMAGE")"
WORK=$(cd "$WORK" && pwd -P)
STAGE=$(mktemp -d "$WORK/image.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT
bash "$ROOT/scripts/build-rootfs-k3.sh" "$STAGE/rootfs.img" "$WORK/base" "$ARCHIVE" "$HASH"
python3 "$ROOT/scripts/populate-keystone-rootfs.py" "$OVERLAY" "$STAGE/rootfs.img" "$STAGE"
tune2fs -L keystone-root "$STAGE/rootfs.img"
e2fsck -fn "$STAGE/rootfs.img"
# The staging directory may be on another filesystem, so copy to an adjacent temp.
TEMP=$(mktemp "${IMAGE}.tmp.XXXXXX")
trap 'rm -rf "$STAGE"; rm -f "$TEMP"' EXIT
cp --sparse=always "$STAGE/rootfs.img" "$TEMP"
ln "$TEMP" "$IMAGE"
echo "Created Keystone guest image: $IMAGE"
