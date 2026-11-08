#!/usr/bin/env bash
# Diagnostic Keystone SM relocatable object, not a validated security image.
set -Eeuo pipefail
if [ "$#" -ne 3 ]; then echo "Usage: $0 /path/keystone /path/opensbi /path/work" >&2; exit 2; fi
K=$(realpath -e "$1")
S=$(realpath -e "$2")
W=$(realpath -m "$3")
[ -f "$K/sm/tools/k3_rv64_compile_audit.py" ] || { echo 'ERROR: Keystone audit compiler missing' >&2; exit 3; }
[ -f "$S/include/sbi/sbi_types.h" ] || { echo 'ERROR: OpenSBI source missing' >&2; exit 3; }
[ -f "$K/sm/src/platform/spacemit_k3/platform.c" ] || { echo 'ERROR: K3 platform missing' >&2; exit 3; }
for tool in python3 clang ld.lld readelf nm; do command -v "$tool" >/dev/null || { echo "ERROR missing $tool" >&2; exit 3; }; done
trace_args=(--rng "${KEYSTONE_RNG_BACKEND:-auto}" --crypto "${KEYSTONE_CRYPTO_PROVIDER:-unavailable}")
case ${KEYSTONE_SM_TRACE:-0} in
 0) :;;
 1) trace_args+=(--trace);;
 *) echo 'KEYSTONE_SM_TRACE must be 0 or 1' >&2; exit 2;;
esac
mkdir -p "$W"
W=$(realpath -e "$W")
OBJDIR="$W/keystone-sm-objects"
SM_OBJ="$W/sm-combined.o"
mkdir -p "$OBJDIR"
if [[ ${KEYSTONE_CRYPTO_PROVIDER:-unavailable} == qemu-dev ]]; then
 seed=${KEYSTONE_CRYPTO_SEED_FILE:-${XDG_STATE_HOME:-$HOME/.local/state}/keystone-k3/qemu-dev.seed}
 python3 "$(dirname "$0")/prepare-keystone-dev-root.py" "$seed" "$OBJDIR"
fi
find "$OBJDIR" -maxdepth 1 -type f -name '*.o' -delete
rm -f "$SM_OBJ"

python3 "$K/sm/tools/k3_rv64_compile_audit.py" "${trace_args[@]}" --opensbi "$S" --output "$OBJDIR" --compiler clang 2>&1 | tee "$W/keystone-sm-compile.log"
python3 - "$K/sm/tools/k3_rv64_compile_audit.py" "$OBJDIR" <<'PY_AUDIT'
import ast, pathlib, sys
p=pathlib.Path(sys.argv[1]);d=pathlib.Path(sys.argv[2]);t=ast.parse(p.read_text())
sources=None
for n in t.body:
    if isinstance(n,ast.Assign) and any(isinstance(x,ast.Name) and x.id=='SOURCES' for x in n.targets):
        sources=ast.literal_eval(n.value)
if not sources: raise SystemExit('ERROR: audit SOURCES unavailable')
missing=[]
for src in sources:
    obj=d/(src.replace('/','__').rsplit('.',1)[0]+'.o')
    if not obj.is_file() or not obj.stat().st_size: missing.append(str(obj))
if missing: raise SystemExit('ERROR: missing SM objects: '+', '.join(missing))
print('Verified %d compiled SM objects' % len(sources))
PY_AUDIT
mapfile -d '' objs < <(find "$OBJDIR" -maxdepth 1 -type f -name '*.o' -print0 | sort -z)
[ "${#objs[@]}" -ge 20 ] || { echo 'ERROR: too few SM objects' >&2; exit 4; }
ld.lld -m elf64lriscv -r -o "$SM_OBJ" "${objs[@]}" 2>&1 | tee "$W/keystone-sm-link.log"
readelf -h "$SM_OBJ" | tee "$W/keystone-sm-elf-header.log"
readelf -h "$SM_OBJ" | grep -Eq 'Machine:.*RISC-V' || { echo 'ERROR not RISC-V ELF' >&2; exit 4; }
nm -g --defined-only "$SM_OBJ" > "$W/keystone-sm-defined-symbols.txt"
nm -u "$SM_OBJ" > "$W/keystone-sm-undefined-symbols.txt" || true
grep -Eq '[[:space:]]sm_init$' "$W/keystone-sm-defined-symbols.txt" || { echo 'ERROR: sm_init missing' >&2; exit 4; }
sha256sum "$SM_OBJ" | tee "$W/keystone-sm-sha256.txt"
echo "CREATED: $SM_OBJ"
