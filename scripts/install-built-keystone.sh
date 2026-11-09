#!/usr/bin/env bash
# Publish prepared outputs only. Never invoke a compiler or a build system.
set -Eeuo pipefail
(($# == 6)) || { echo 'Usage: install-built-keystone.sh COMPONENT STAGE PREFIX BUILD ARCHIVE HASH' >&2; exit 2; }
component=$1
stage=$(realpath -m "$2")
prefix=$(realpath -m "$3")
build=$(realpath -m "$4")
archive=$5
hash=$6
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
[[ $stage != "$prefix" ]] || { echo 'Build stage and installation prefix must differ.' >&2; exit 2; }
copy_tree() {
 test -d "$1" || { echo "Missing build output: $1; run make build first." >&2; exit 3; }
 mkdir -p "$2"
 cp -a "$1/." "$2/"
}
case "$component" in
 qemu)
  test -x "$stage/bin/qemu-system-riscv64"
  test -x "$stage/bin/qemu-img"
  copy_tree "$stage/bin" "$prefix/bin"
  copy_tree "$stage/share/qemu" "$prefix/share/qemu";;
 opensbi)
  for name in fw_dynamic.bin fw_dynamic.elf sm-combined.o; do
   test -s "$stage/firmware/k3/$name"
   install -D -m 0644 "$stage/firmware/k3/$name" "$prefix/firmware/k3/$name"
  done;;
 linux)
  test -s "$stage/boot/k3/Image"
  copy_tree "$stage/boot/k3" "$prefix/boot/k3"
  copy_tree "$stage/lib/modules" "$prefix/lib/modules";;
 eyrie)
  copy_tree "$build/sdk-install" "$prefix/sdk"
  for name in hello hello-native; do
   test -s "$build/examples/$name/$name.ke"
   install -D -m 0755 "$build/examples/$name/$name.ke" "$prefix/share/keystone/examples/$name.ke"
   for file in eyrie-rt loader.bin .options_log; do
    test -f "$build/examples/$name/$file"
    install -D -m 0644 "$build/examples/$name/$file" "$prefix/runtime/$name/$file"
   done
  done;;
 keystone)
  overlay="$build/guest-overlay"
  test -d "$overlay/usr/share/keystone/examples" || { echo 'Run make keystone first.' >&2; exit 3; }
  test -s "$build/guest-overlay.kernel.sha256" || { echo 'Rebuild make keystone to record matching kernel.' >&2; exit 3; }
  test -s "$prefix/boot/k3/Image" || { echo 'Run make install-linux first.' >&2; exit 3; }
  expected=$(cat "$build/guest-overlay.kernel.sha256")
  actual=$(sha256sum "$prefix/boot/k3/Image" | cut -d' ' -f1)
  [[ $expected == "$actual" ]] || { echo 'Kernel/driver mismatch. Run make keystone and make install-linux first.' >&2; exit 3; }
  if [[ -e "$prefix/rootfs/keystone-rootfs.img" ]]; then
   python3 "$root/scripts/update-keystone-rootfs.py" "$prefix/rootfs/keystone-rootfs.img" "$overlay"
  else
   bash "$root/scripts/build-rootfs-keystone.sh" "$prefix/rootfs/keystone-rootfs.img" "$build/rootfs" "$overlay" "$archive" "$hash"
  fi;;
 rng)
  for name in zkr jupiter2 counter; do
   test -s "$stage/firmware/k3/rng-$name/fw_dynamic.bin"
   copy_tree "$stage/firmware/k3/rng-$name" "$prefix/firmware/k3/rng-$name"
  done;;
 crypto)
  test -s "$stage/firmware/k3/crypto-qemu-dev/fw_dynamic.bin"
  copy_tree "$stage/firmware/k3/crypto-qemu-dev" "$prefix/firmware/k3/crypto-qemu-dev"
  copy_tree "$stage/share/keystone/crypto" "$prefix/share/keystone/crypto";;
 verifications)
  for file in crypto/build/crypto.ke random/build/random.ke sbi-3004/build/stop.ke \
   sbi-4000/build/multimem.ke sbi-4000/build/test sbi-4000/kernel/probe.ko sbi-errors/build/test \
   sbi-errors/kernel/keystone_sbi_verify.ko edgecall/kernel/edgecall_pmp_probe.ko \
   edgecall/build/guest/edgecall.ke edgecall/build/guest-256/edgecall.ke; do
   test -s "$root/verifications/$file" || { echo "Missing verification output: $file" >&2; exit 3; }
  done
  copy_tree "$root/verifications" "$prefix/share/keystone/verifications";;
 samples)
  for file in helloworld/build/hello.ke hello-native/build/hello-native.ke \
   attestation/build/attestor.ke tests/build/tests.ke; do
   test -s "$root/samples/$file" || { echo "Missing sample output: $file" >&2; exit 3; }
  done
  copy_tree "$root/samples" "$prefix/share/keystone/samples";;
 *) echo "Unknown component: $component" >&2; exit 2;;
esac
printf 'Installed %s at %s\n' "$component" "$prefix"
