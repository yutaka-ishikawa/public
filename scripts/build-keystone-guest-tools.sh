#!/usr/bin/env bash
# Static RISC-V module loader for base rootfs images without the kmod package.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
 echo "Usage: bash scripts/build-keystone-guest-tools.sh [OUTPUT [BUILD_DIR [JOBS]]]"
 echo 'Default output: .build-keystone/guest-tools/busybox (static RISC-V insmod/lsmod)'
 exit 0
fi
(($# <= 3)) || { echo 'Too many arguments' >&2; exit 2; }
OUTPUT=${1:-$ROOT/.build-keystone/guest-tools/busybox}
WORK=${2:-$ROOT/.build-keystone/guest-tools/build}
JOBS=${3:-$(nproc)}
CROSS_COMPILE=${CROSS_COMPILE:-riscv64-linux-gnu-}
[[ $JOBS =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid jobs' >&2; exit 2; }
for tool in curl sha256sum tar make "${CROSS_COMPILE}gcc" "${CROSS_COMPILE}readelf"; do
 command -v "$tool" >/dev/null || { echo "Missing $tool; install build prerequisites first." >&2; exit 3; }
done
mkdir -p "$WORK" "$(dirname "$OUTPUT")"
WORK=$(cd "$WORK" && pwd -P)
REV=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4
HASH=076a7449075feea57afc2750b10a9ff5e95e93f782ad454404fedd10a27c2933
ARCHIVE="$WORK/busybox-$REV.tar.gz"
if [[ ! -f $ARCHIVE ]]; then
 TEMP=$(mktemp "$WORK/download.XXXXXX")
 trap 'rm -f "$TEMP"' EXIT
 curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 \
  "https://codeload.github.com/mirror/busybox/tar.gz/$REV" -o "$TEMP"
 mv "$TEMP" "$ARCHIVE"
 trap - EXIT
fi
printf '%s  %s\n' "$HASH" "$ARCHIVE" | sha256sum -c -
SRC="$WORK/busybox-$REV"
if [[ ! -d $SRC ]]; then tar -xzf "$ARCHIVE" -C "$WORK"; fi
OUT="$WORK/objects"
mkdir -p "$OUT"
make -C "$SRC" O="$OUT" allnoconfig > "$WORK/config.log"
sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/; s/# CONFIG_BUSYBOX is not set/CONFIG_BUSYBOX=y/; s/# CONFIG_SHOW_USAGE is not set/CONFIG_SHOW_USAGE=y/; s/# CONFIG_INSMOD is not set/CONFIG_INSMOD=y/; s/# CONFIG_LSMOD is not set/CONFIG_LSMOD=y/' "$OUT/.config"
make -C "$SRC" O="$OUT" CROSS_COMPILE="$CROSS_COMPILE" -j "$JOBS"
"${CROSS_COMPILE}readelf" -h "$OUT/busybox" | grep -q 'Machine:.*RISC-V'
if "${CROSS_COMPILE}readelf" -l "$OUT/busybox" | grep -q INTERP; then
 echo 'Guest module loader must be statically linked' >&2; exit 4
fi
TEMP=$(mktemp "${OUTPUT}.tmp.XXXXXX")
trap 'rm -f "$TEMP"' EXIT
install -m 0755 "$OUT/busybox" "$TEMP"
mv -f "$TEMP" "$OUTPUT"
echo "Built static guest tools: $OUTPUT"
