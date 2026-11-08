#!/usr/bin/env python3
"""Provision an explicit development-only root; never print the secret."""
import os
from pathlib import Path
import stat
import subprocess
import sys

seed_file, output = map(Path, sys.argv[1:])
root = Path(__file__).resolve().parents[1]
seed_file.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
try:
    fd = os.open(seed_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
except FileExistsError:
    pass
else:
    with os.fdopen(fd, 'wb') as stream:
        stream.write(os.urandom(32))
if seed_file.is_symlink() or not seed_file.is_file() or stat.S_IMODE(seed_file.stat().st_mode) & 0o077:
    raise SystemExit('Development seed must be a private regular file (mode 0600)')
seed = seed_file.read_bytes()
if len(seed) != 32:
    raise SystemExit('Development seed must contain exactly 32 bytes; refusing to regenerate it')
output.mkdir(parents=True, exist_ok=True)
include = output / 'keystone_dev_root.inc'
include.write_text('/* DEVELOPMENT ONLY: extractable from firmware. */\n'
                   'static unsigned char keystone_dev_root[32] '
                   '__attribute__((section(".data.keystone_dev_root"))) = {' +
                   ','.join(str(byte) for byte in seed) + '};\n')
include.chmod(0o600)
helper = output / 'dev-public-key-tool'
sdk = root / 'keystone/sdk'
subprocess.run(['cc', '-O2', '-fno-builtin', '-I' + str(sdk / 'include'),
                '-I' + str(sdk / 'include/verifier'),
                str(root / 'scripts/keystone-dev-public-key.c'),
                *[str(sdk / 'src/verifier/ed25519' / f'{name}.c') for name in ('keypair', 'fe', 'ge', 'sc', 'verify')],
                str(sdk / 'src/common/sha3.c'), '-o', str(helper)], check=True)
public = subprocess.check_output([str(helper), str(seed_file)])
if len(public) != 32:
    raise SystemExit('Invalid development public key')
(output / 'device-public-key.bin').write_bytes(public)
print('Prepared qemu-dev root and public pin; NOT a hardware endorsement')
