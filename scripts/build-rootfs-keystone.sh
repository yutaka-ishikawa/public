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
python3 - "$OVERLAY" "$STAGE/rootfs.img" "$STAGE" <<'PY'
import hashlib
import pathlib
import subprocess
import sys

overlay = pathlib.Path(sys.argv[1]).resolve()
image = sys.argv[2]
work = pathlib.Path(sys.argv[3])

def quote(value):
    text = str(value)
    if any(c in text for c in '\n\r\0'):
        raise SystemExit('Unsupported newline or NUL in overlay path')
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'

def exists(guest):
    result = subprocess.run(['debugfs', '-R', f'stat {quote(guest)}', image],
                            text=True, capture_output=True, check=True)
    return 'Inode:' in result.stdout

commands = []
files = []
for source in sorted(overlay.rglob('*')):
    guest = '/' + source.relative_to(overlay).as_posix()
    if source.is_symlink():
        raise SystemExit(f'Overlay symlinks are unsupported: {source}')
    if source.is_dir():
        if not exists(guest):
            commands.append(f'mkdir {quote(guest)}')
    elif source.is_file():
        if exists(guest):
            commands.append(f'rm {quote(guest)}')
        commands.extend((f'write {quote(source)} {quote(guest)}',
                         f"set_inode_field {quote(guest)} mode 0{(0o100000 | (source.stat().st_mode & 0o7777)):o}"))
        files.append((source, guest))
    else:
        raise SystemExit(f'Unsupported overlay entry: {source}')
script = work / 'debugfs.commands'
script.write_text('\n'.join(commands) + '\n')
# debugfs returns zero for some command errors. Verify every imported byte below.
subprocess.run(['debugfs', '-w', '-f', str(script), image], check=True, stdout=subprocess.DEVNULL)
for index, (source, guest) in enumerate(files):
    output = work / f'verify-{index}'
    subprocess.run(['debugfs', '-R', f'dump {quote(guest)} {quote(output)}', image], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not output.is_file() or hashlib.sha256(output.read_bytes()).digest() != hashlib.sha256(source.read_bytes()).digest():
        raise SystemExit(f'Failed to populate guest file: {guest}')
print(f'Verified {len(files)} Keystone guest files')
PY
tune2fs -L keystone-root "$STAGE/rootfs.img"
e2fsck -fn "$STAGE/rootfs.img"
# The staging directory may be on another filesystem, so copy to an adjacent temp.
TEMP=$(mktemp "${IMAGE}.tmp.XXXXXX")
trap 'rm -rf "$STAGE"; rm -f "$TEMP"' EXIT
cp --sparse=always "$STAGE/rootfs.img" "$TEMP"
ln "$TEMP" "$IMAGE"
echo "Created Keystone guest image: $IMAGE"
