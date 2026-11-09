#!/usr/bin/env python3
"""Real archive/pipe regression, unrelated to SM/SBI functional verification."""
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PackageTests(unittest.TestCase):
    def package(self, directory):
        payload = io.BytesIO()
        with tarfile.open(fileobj=payload, mode='w:gz') as archive:
            data = bytes(range(256)) * 128
            member = tarfile.TarInfo('binary-data')
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
        compressed = payload.getvalue()
        header = ['#!/bin/sh', 'skip="100"', f'filesizes="{len(compressed)}"',
                  f'MD5="{hashlib.md5(compressed).hexdigest()}"',
                  f'SHA="{hashlib.sha256(compressed).hexdigest()}"']
        header += ['# header padding'] * (100 - len(header))
        path = directory / 'test.ke'
        path.write_bytes(('\n'.join(header) + '\n').encode() + compressed)
        return path, data

    def extract(self, package, work):
        return subprocess.run(['bash', str(ROOT / 'verifications/extract-package.sh'),
                               str(package), str(work)], capture_output=True)

    def test_exact_binary_extraction(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package, expected = self.package(root)
            result = self.extract(package, root / 'work')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / 'work/payload/binary-data').read_bytes(), expected)

    def test_corrupt_and_truncated_rejected(self):
        for mode in ('corrupt', 'truncate'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                package, _ = self.package(root)
                data = bytearray(package.read_bytes())
                if mode == 'corrupt':
                    data[-10] ^= 1
                else:
                    del data[-10:]
                package.write_bytes(data)
                self.assertNotEqual(self.extract(package, root / 'work').returncode, 0)

    def test_sha256_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package, _ = self.package(root)
            data = package.read_bytes()
            position = data.index(b'SHA="') + len(b'SHA="')
            data = data[:position] + b'f' * 64 + data[position + 64:]
            package.write_bytes(data)
            self.assertNotEqual(self.extract(package, root / 'work').returncode, 0)

    def test_fixed_dd_record_count_truncates_short_pipe_reads(self):
        # The old makeself pipeline counts input records, not bytes. Real dd
        # reaches count=32 after 32 short records; it does not consume 32 KiB.
        # obs=1 makes each short record observable, providing a handshake
        # before the next write; this avoids timing-dependent fragmentation.
        process = subprocess.Popen(['dd', 'ibs=1024', 'obs=1', 'count=32'],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        output = bytearray()
        try:
            for i in range(32):
                fragment = bytes([i]) * 97
                process.stdin.write(fragment)
                process.stdin.flush()
                output.extend(process.stdout.read(len(fragment)))
            # dd exits with stdin still open: count=32 did not mean 32 KiB.
            self.assertEqual(process.wait(timeout=5), 0)
            self.assertEqual(len(output), 32 * 97)
            self.assertLess(len(output), 32 * 1024)
        finally:
            process.stdin.close()
            process.stdout.close()
            process.stderr.close()
            if process.poll() is None:
                process.kill()
                process.wait()



if __name__ == '__main__':
    unittest.main()
