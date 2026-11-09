#!/usr/bin/env bash
# Extract one gzip makeself payload without its pipe/dd block-count reader.
set -Eeuo pipefail
(($# == 2)) || { echo 'Usage: extract-package.sh PACKAGE WORKDIR' >&2; exit 2; }
package=$1
work=$2
mkdir -p "$work"
cp -- "$package" "$work/package.ke"
snapshot="$work/package.ke"
header=$(head -n 80 "$snapshot")
skip=$(printf '%s\n' "$header" | sed -n 's/^skip="\([0-9][0-9]*\)"$/\1/p')
size=$(printf '%s\n' "$header" | sed -n 's/^filesizes="\([0-9][0-9]*\)"$/\1/p')
expected_sha=$(printf '%s\n' "$header" | sed -n 's/^SHA="\([0-9a-f]\{64\}\)"$/\1/p')
expected=$(printf '%s\n' "$header" | sed -n 's/^MD5="\([0-9a-f]\{32\}\)"$/\1/p')
[[ $skip =~ ^[1-9][0-9]{0,5}$ && $size =~ ^[1-9][0-9]{0,11}$ &&
   $expected =~ ^[0-9a-f]{32}$ && $expected != 00000000000000000000000000000000 ]] || {
    echo 'Invalid package header: require one checksummed makeself gzip payload' >&2
    exit 2
}
offset=$(head -n "$skip" "$snapshot" | wc -c)
total=$(wc -c < "$snapshot")
((total - offset == size)) || { echo 'Package size mismatch' >&2; exit 2; }
archive="$work/payload.tar.gz"
# tail reads through EOF; no count of potentially short pipe reads is used.
tail -c "+$((offset + 1))" "$snapshot" > "$archive"
actual=$(md5sum "$archive")
actual=${actual%% *}
[[ $actual == "$expected" ]] || { echo "Package MD5 mismatch: $actual != $expected" >&2; exit 2; }
if [[ -n $expected_sha && $expected_sha != $(printf '%064d' 0) ]]; then
    actual_sha=$(sha256sum "$archive")
    [[ ${actual_sha%% *} == "$expected_sha" ]] || {
	echo 'Package SHA256 mismatch' >&2
	exit 2
    }
fi
gzip -t "$archive"
mkdir "$work/payload"
tar -xzf "$archive" -C "$work/payload"
printf 'PASS PACKAGE: %s; size=%s md5=%s\n' "${package##*/}" "$size" "$actual"
