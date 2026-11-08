#!/usr/bin/env python3
"""Independently measure linked immutable ELF sections, not a returned report."""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

elf, directory, public_file = map(Path, sys.argv[1:])
sections = subprocess.check_output(['readelf', '-SW', str(elf)], text=True)
image = elf.read_bytes()
regions = []
for name in ('.text', '.rodata'):
    match = re.search(r'\[\s*\d+\]\s+' + re.escape(name) +
                      r'\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', sections)
    if not match:
        raise SystemExit('Missing measured ELF section: ' + name)
    address, offset, size = [int(value, 16) for value in match.groups()]
    regions.append({'section': name, 'address': address, 'offset': offset, 'size': size})
relocs = subprocess.check_output(['readelf', '-rW', str(elf)], text=True)
for target in re.findall(r'^([0-9a-f]{8,16})\s+\S+\s+R_RISCV_', relocs, re.MULTILINE):
    address = int(target, 16)
    if any(region['address'] <= address < region['address'] + region['size'] for region in regions):
        raise SystemExit('Relocation targets a measured section; canonical measurement requires review')
digest = hashlib.sha3_512(struct.pack('<QQ', *(region['size'] for region in regions)))
for region in regions:
    digest.update(image[region['offset']:region['offset'] + region['size']])
public = public_file.read_bytes()
if len(public) != 32:
    raise SystemExit('Invalid device public pin')
directory.mkdir(parents=True, exist_ok=True)
(directory / 'sm-hash.bin').write_bytes(digest.digest())
(directory / 'device-public-key.bin').write_bytes(public)
(directory / 'manifest.json').write_text(json.dumps({
    'provider': 'qemu-dev', 'hardware_endorsement': False,
    'measurement': 'SHA3-512(LE64 text_size || LE64 rodata_size || text || rodata)',
    'regions': regions, 'sm_hash': digest.hexdigest(), 'device_public_key': public.hex(),
    'elf_sha256': hashlib.sha256(image).hexdigest(),
    'excluded': ['writable data/BSS', 'development root', 'dynamic symbols/relocations', 'padding outside measured sections']
}, indent=2) + '\n')
print('Generated independent qemu-dev measurement and public pin manifest')
