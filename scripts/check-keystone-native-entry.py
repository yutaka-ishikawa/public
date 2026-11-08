#!/usr/bin/env python3
"""Inspect real native ELF entry and decode its non-relaxed gp initialization."""
from pathlib import Path
import struct
import subprocess
import sys

if len(sys.argv) != 3:
    raise SystemExit('Usage: check-keystone-native-entry.py NM EAPP_ELF')
path = Path(sys.argv[2])
data = path.read_bytes()
if data[:6] != b'\x7fELF\x02\x01':
    raise SystemExit('Native entry checker requires RV64 little-endian ELF')
if struct.unpack_from('<H', data, 18)[0] != 243:
    raise SystemExit('Not a RISC-V ELF')
symbols = {}
for line in subprocess.check_output([sys.argv[1], '-n', str(path)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3 and fields[2] in ('keystone_eapp_start', '__global_pointer$'):
        symbols[fields[2]] = int(fields[0], 16)
entry = struct.unpack_from('<Q', data, 24)[0]
if entry != symbols.get('keystone_eapp_start') or '__global_pointer$' not in symbols:
    raise SystemExit('Native ELF must enter keystone_eapp_start with a defined global pointer')
phoff = struct.unpack_from('<Q', data, 32)[0]
phsize, phnum = struct.unpack_from('<HH', data, 54)
for index in range(phnum):
    offset = phoff + phsize * index
    kind, flags, file_offset, virtual, physical, file_size, memory_size, alignment = struct.unpack_from('<IIQQQQQQ', data, offset)
    if kind == 1 and virtual <= entry and entry + 8 <= virtual + file_size:
        code = file_offset + entry - virtual
        break
else:
    raise SystemExit('ELF entry is not backed by a loadable segment')
upper, lower = struct.unpack_from('<II', data, code)
# AUIPC gp, imm20; ADDI gp, gp, imm12. No instruction may rely on the old gp.
if upper & 0xfff != 0x197 or lower & 0xfffff != 0x18193:
    raise SystemExit('Entry must initialize gp with non-relaxed AUIPC/ADDI')
hi = upper & 0xfffff000
if hi & 0x80000000:
    hi -= 1 << 32
lo = lower >> 20
if lo & 0x800:
    lo -= 1 << 12
if entry + hi + lo != symbols['__global_pointer$']:
    raise SystemExit('Native entry initializes the wrong gp address')
print(f'PASS native entry: {path.name}, gp initialized before main')
