#!/usr/bin/env python3
"""Generate C material at build time using Keystone's actual SHA3-based keypair.

Development ONLY: the generated file and final firmware disclose the private
key to anyone who can read the build outputs. No production attestation claim.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

SCRIPT = Path(__file__).resolve()
SM = SCRIPT.parent.parent
DEFAULT = SM / 'src/platform/spacemit_k3/k3_dev_key_generated.inc'


def emit_array(name, data):
    rows = [data[i:i + 12] for i in range(0, len(data), 12)]
    return 'static const unsigned char ' + name + '[' + str(len(data)) + '] = {\n' + ''.join(
        '    ' + ', '.join('0x%02x' % b for b in row) + ',\n' for row in rows
    ) + '};\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=DEFAULT)
    parser.add_argument('--opensbi-include', type=Path, required=True,
                        help='Path to OpenSBI include/; used only to compile host helper')
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise SystemExit('refusing to overwrite existing key; delete it explicitly for key rotation')
    with tempfile.TemporaryDirectory(prefix='k3-devkey-') as tmp:
        helper = Path(tmp) / 'keygen'
        src = SM / 'src'
        cmd = [args.cc, '-std=c11', '-O2', '-D__riscv_xlen=64',
               '-I' + str(args.opensbi_include.resolve()), '-o', str(helper),
               str(SCRIPT.parent / 'k3_dev_keypair_host.c'),
               str(src / 'ed25519/keypair.c'), str(src / 'ed25519/ge.c'),
               str(src / 'ed25519/fe.c'), str(src / 'sha3/sha3.c')]
        subprocess.run(cmd, check=True)
        seed = bytearray(os.urandom(32))
        try:
            pair = subprocess.run([str(helper)], input=seed, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, check=True).stdout
        finally:
            seed[:] = bytes(32)
    if len(pair) != 96 or not any(pair[:32]) or not any(pair[32:]):
        raise SystemExit('host keypair generator returned invalid output')
    material = ('/* AUTOMATICALLY GENERATED. DEVELOPMENT ONLY. DO NOT DISTRIBUTE.\n'
                ' * A firmware image containing this material exposes the private key.\n'
                ' * This is not a hardware-bound or manufacturer-certified attestation key. */\n'
                '#define K3_DEV_KEY_MATERIAL_PRESENT 1\n' +
                emit_array('k3_dev_pubkey', pair[:32]) +
                emit_array('k3_dev_privkey', pair[32:]))
    fd = os.open(output, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    with os.fdopen(fd, 'w', encoding='ascii') as stream:
        stream.write(material)
    print('Generated development-only key include:', output)
    print('Public key SHA-256:', __import__('hashlib').sha256(pair[:32]).hexdigest())
    print('Private key not printed. NEVER publish firmware with this key as trusted attestation.')

if __name__ == '__main__':
    main()
