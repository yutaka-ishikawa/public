#!/usr/bin/env python3
"""Independent HKDF known-answer digest for the actual enclave file layout."""
import hashlib
import hmac
from pathlib import Path
import struct
import sys

seed_file, public_dir, loader, runtime, eapp, output = map(Path, sys.argv[1:])
root = seed_file.read_bytes()
sm_hash = (public_dir / 'sm-hash.bin').read_bytes()
if len(root) != 32 or len(sm_hash) != 64:
    raise SystemExit('Invalid development key/measurement input')


def hkdf(key, info, size):
    prk = hmac.new(bytes(64), key, hashlib.sha3_512).digest()
    out = b''
    previous = b''
    for index in range(1, (size + 63) // 64 + 1):
        previous = hmac.new(prk, previous + info + bytes([index]), hashlib.sha3_512).digest()
        out += previous
    return out[:size]


parts = [path.read_bytes() for path in (loader, runtime, eapp)]
sizes = [(len(part) + 4095) & ~4095 for part in parts]
digest = hashlib.sha3_512(struct.pack('<QQQ', *sizes))
for part, size in zip(parts, sizes):
    digest.update(part + bytes(size - len(part)))
sm_seed = hkdf(root, b'KEYSTONE-SM-IDENTITY-v1' + sm_hash, 32)
private = bytearray(hashlib.sha3_512(sm_seed).digest())
private[0] &= 248
private[31] = (private[31] & 63) | 64
key = hkdf(private, digest.digest() + b'verify-sealing\x00v1', 128)
output.write_bytes(hashlib.sha3_512(key).digest())
print('Generated sealing known-answer commitment (no raw sealing key output)')
