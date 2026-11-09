#!/usr/bin/env bash
set -Eeuo pipefail
if [ "$#" -ne 2 ]; then echo "Usage: $0 /absolute/path/opensbi-k3 /absolute/path/workspace" >&2; exit 2; fi
S=$(realpath -e "$1"); W=$(realpath -e "$2")
SM_OBJ="$W/sm-combined.o"
O="$W/build-opensbi-k3-keystone"
[ -s "$SM_OBJ" ] || { echo "ERROR: missing $SM_OBJ; run scripts/build_keystone_sm.sh first" >&2; exit 3; }
readelf -h "$SM_OBJ" | grep -Eq 'Machine:.*RISC-V' || { echo 'ERROR: SM is not RISC-V ELF' >&2; exit 3; }
[ -f "$S/platform/generic/configs/k3_keystone_audit_defconfig" ] || { echo 'ERROR: audit defconfig absent in OpenSBI source' >&2; exit 3; }
mkdir -p "$W" "$O"
set -o pipefail
make -C "$S" -j"${JOBS:-$(nproc)}" LLVM=1 PLATFORM=generic PLATFORM_RISCV_XLEN=64 \
  PLATFORM_DEFCONFIG=k3_keystone_audit_defconfig K3_PMP_BOOT_TRACE=1 \
  KEYSTONE_K3_SM_LINK_PROBE="$SM_OBJ" O="$O" \
  "$O/platform/generic/firmware/fw_dynamic.bin" 2>&1 | tee "$W/opensbi-keystone-build.log"
for ext in elf bin; do test -s "$O/platform/generic/firmware/fw_dynamic.$ext" || { echo "ERROR: missing fw_dynamic.$ext" >&2; exit 4; }; done
readelf -h "$O/platform/generic/firmware/fw_dynamic.elf" | grep -E 'Class:|Machine:'
nm "$O/platform/generic/firmware/fw_dynamic.elf" | grep -E ' (sm_init|sbi_sm_create_enclave)$' || true
python3 - "$O/platform/generic/firmware/fw_dynamic.elf" <<'PY_VECTOR'
import subprocess
import sys
symbols = subprocess.check_output(['nm', sys.argv[1]], text=True).splitlines()
vector = [int(line.split()[0], 16) for line in symbols
          if len(line.split()) == 3 and line.split()[2] == 'trap_vector_enclave']
if len(vector) != 1 or vector[0] % 4:
    raise SystemExit('ERROR: enclave mtvec handler must be 4-byte aligned')
print(f'PASS enclave mtvec alignment: {vector[0]:#x}')
PY_VECTOR
sha256sum "$O/platform/generic/firmware/fw_dynamic.elf" "$O/platform/generic/firmware/fw_dynamic.bin" | tee "$W/fw-sha256.txt"
echo 'NOTE: successful link does not validate PMP/IOPMP or enclave operation.'

if [[ ${KEYSTONE_CRYPTO_PROVIDER:-unavailable} == qemu-dev ]]; then
 python3 "$(dirname "$0")/keystone-crypto-manifest.py" \
  "$O/platform/generic/firmware/fw_dynamic.elf" "$W/crypto-public" \
  "$W/keystone-sm-objects/device-public-key.bin"
fi
