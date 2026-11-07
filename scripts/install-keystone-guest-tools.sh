#!/usr/bin/env bash
# Stage guest tools into an image overlay, or install them in a running RV64 guest.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
 echo 'Usage: bash scripts/install-keystone-guest-tools.sh [GUEST_ROOT [BUSYBOX_FILE]]'
 echo 'Default: install into / in a running RISC-V guest, using .build-keystone/guest-tools/busybox from the shared repository.'
 exit 0
fi
(($# <= 2)) || { echo 'Too many arguments' >&2; exit 2; }
TARGET=$(realpath -m "${1:-/}")
BINARY=${2:-$ROOT/.build-keystone/guest-tools/busybox}
if [[ $TARGET == / ]]; then
 [[ $(uname -m) == riscv64 && $EUID == 0 ]] || { echo 'Live installation into / requires root inside the RISC-V guest. On a host, specify an explicit staging directory.' >&2; exit 3; }
fi
test -s "$BINARY" || { echo 'Guest BusyBox missing. On the host run: bash scripts/build-keystone-guest-tools.sh' >&2; exit 3; }
copy_guest_file() {
 local source=$1 destination=$2 temporary
 mkdir -p "$(dirname "$destination")"
 temporary=$(mktemp "$(dirname "$destination")/.keystone-tools.XXXXXX")
 cp "$source" "$temporary"
 chmod 0755 "$temporary"
 # Replace the directory entry instead of following a pre-existing systemctl symlink.
 mv -f "$temporary" "$destination"
}
copy_guest_file "$BINARY" "$TARGET/usr/libexec/keystone/busybox"
for name in run-keystone-hello mount-hostshare; do copy_guest_file "$ROOT/scripts/guest/$name" "$TARGET/usr/bin/$name"; done
for name in shutdown poweroff reboot; do
 copy_guest_file "$ROOT/scripts/guest/$name" "$TARGET/usr/sbin/$name"
 # Bash launched directly as init may have a PATH containing /usr/bin but no sbin.
 copy_guest_file "$ROOT/scripts/guest/$name" "$TARGET/usr/bin/$name"
done
echo "Installed Keystone guest helpers at $TARGET"
if [[ $TARGET == / && ! -x /usr/share/keystone/examples/hello.ke ]]; then
 echo 'Keystone hello.ke is missing from this rootfs. The helper files are installed, but you must boot/build the Keystone image to run the example.' >&2
fi
