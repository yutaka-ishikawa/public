#!/usr/bin/env bash
# Create an unpartitioned Bianbu ext4 rootfs without loop mounts or sudo.
set -Eeuo pipefail
if (($# < 2 || $# > 4)); then
 echo "Usage: $0 OUTPUT_IMAGE BUILD_DIR [LOCAL_ARCHIVE] [SHA256]" >&2
 exit 2
fi
IMAGE=$1
WORK=$2
ARCHIVE=${3:-}
EXPECTED_SHA256=${4:-}
URL=https://archive.spacemit.com/bianbu-base/bianbu-base-26.04-base-riscv64.tar.gz
if [[ -e $IMAGE || -L $IMAGE ]]; then
 test -f "$IMAGE" && test -s "$IMAGE" || { echo "Existing rootfs is empty or invalid: $IMAGE" >&2; exit 3; }
 echo "Keeping existing Bianbu rootfs: $IMAGE"
 exit 0
fi
for tool in mke2fs e2fsck debugfs truncate sha256sum; do
 command -v "$tool" >/dev/null || { echo "Missing rootfs tool: $tool (install e2fsprogs, tar, coreutils)" >&2; exit 3; }
done
if [[ -n $EXPECTED_SHA256 && ! $EXPECTED_SHA256 =~ ^[[:xdigit:]]{64}$ ]]; then
 echo 'Rootfs SHA256 must contain exactly 64 hexadecimal digits' >&2; exit 2
fi
mkdir -p "$WORK" "$(dirname "$IMAGE")"
WORK=$(cd "$WORK" && pwd -P)
if [[ -z $ARCHIVE ]]; then
 command -v curl >/dev/null || { echo 'Missing curl' >&2; exit 3; }
 ARCHIVE="$WORK/bianbu-base-26.04-base-riscv64.tar.gz"
 if [[ ! -f $ARCHIVE ]]; then
  DOWNLOAD=$(mktemp "$WORK/download.XXXXXX")
  trap 'rm -f "$DOWNLOAD"' EXIT
  curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' --tlsv1.2 "$URL" -o "$DOWNLOAD"
  mv "$DOWNLOAD" "$ARCHIVE"
  trap - EXIT
 fi
fi
ARCHIVE=$(realpath -e "$ARCHIVE")
if [[ -n $EXPECTED_SHA256 ]]; then
 printf '%s  %s\n' "$EXPECTED_SHA256" "$ARCHIVE" | sha256sum -c -
fi
sha256sum "$ARCHIVE"
TEMP=$(mktemp "${IMAGE}.tmp.XXXXXX")
STAGE=""
trap 'rm -f "$TEMP"; if [[ -n $STAGE ]]; then rm -rf "$STAGE"; fi' EXIT
# Try direct tar import first; older mke2fs builds accept only a directory.
truncate -s 8G "$TEMP"
LOG=$(mktemp "$WORK/mke2fs.XXXXXX")
if LC_ALL=C mke2fs -q -t ext4 -F -L bianbu-root -O '^metadata_csum_seed,^orphan_file' \
    -E root_owner=0:0 -d "$ARCHIVE" "$TEMP" 2> "$LOG"; then
 rm -f "$LOG"
else
 if ! grep -q 'Not a directory' "$LOG"; then
  cat "$LOG" >&2
  rm -f "$LOG"
  exit 4
 fi
 rm -f "$LOG"
 command -v tar >/dev/null || { echo 'Install tar for directory-based rootfs creation.' >&2; exit 3; }
 runner=()
 if ((EUID != 0)); then
  command -v fakeroot >/dev/null || {
   echo 'This mke2fs needs directory input. Install fakeroot (Ubuntu/Debian: sudo apt-get install fakeroot), then rerun the same build command.' >&2
   exit 3
  }
  runner=(fakeroot --)
 fi
 STAGE=$(mktemp -d "$WORK/stage.XXXXXX")
 echo 'mke2fs lacks tar import; using directory input with preserved guest ownership.'
 # Keep extraction and image population in the same fakeroot session. This also
 # preserves device nodes without mounting the image or running the build as root.
 FAKEROOTDONTTRYCHOWN=1 "${runner[@]}" bash -euo pipefail -c '
  archive=$1; stage=$2; image=$3
  tar --extract --gzip --file "$archive" --directory "$stage" \
      --numeric-owner --same-owner --same-permissions
  truncate -s 0 "$image"
  truncate -s 8G "$image"
  mke2fs -q -t ext4 -F -L bianbu-root -O "^metadata_csum_seed,^orphan_file" \
      -E root_owner=0:0 -d "$stage" "$image"
 ' bash "$ARCHIVE" "$STAGE" "$TEMP"
fi
e2fsck -fn "$TEMP"
# Fail before publishing if the archive has the wrong directory layout.
CHECK=$(debugfs -R 'stat /etc/os-release' "$TEMP" 2>&1)
[[ $CHECK == *'Type: regular'* || $CHECK == *'Type: symlink'* ]] || { echo 'Archive lacks /etc/os-release' >&2; exit 4; }
CHECK=$(debugfs -R 'stat /usr/bin/bash' "$TEMP" 2>&1)
if [[ $CHECK != *'Type: regular'* ]]; then
 CHECK=$(debugfs -R 'stat /bin/bash' "$TEMP" 2>&1)
 [[ $CHECK == *'Type: regular'* ]] || { echo 'Archive lacks Bash' >&2; exit 4; }
fi
# Atomic no-clobber publication, including when another build races this one.
ln "$TEMP" "$IMAGE"
echo "Created 8 GiB Bianbu rootfs: $IMAGE"
