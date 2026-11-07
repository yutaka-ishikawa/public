#!/usr/bin/env python3
"""Check the fixed Eyrie handoff address with build-id generation enabled."""
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get('KEYSTONE_TEST_BUILD_DIR', ROOT / '.build-keystone'))
BASE = 0xffffffffc0000000


def run(*args):
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stderr)
    return result.stdout


class RuntimeEntryTests(unittest.TestCase):
    def check_elf(self, path):
        data = path.read_bytes()
        self.assertEqual(struct.unpack_from('<Q', data, 24)[0], BASE)
        sections = run('riscv64-linux-gnu-readelf', '-SW', str(path))
        self.assertNotIn('.note.gnu.build-id', sections)
        symbols = run('riscv64-linux-gnu-nm', '-n', str(path))
        self.assertIn('ffffffffc0000000 T _start', symbols)
        # Inspect bytes through PT_LOAD, as the enclave loader does.
        phoff = struct.unpack_from('<Q', data, 32)[0]
        entsize, count = struct.unpack_from('<HH', data, 54)
        for i in range(count):
            kind, flags, offset, va, _, filesz, _, _ = struct.unpack_from(
                '<IIQQQQQQ', data, phoff + i * entsize)
            if kind == 1 and va <= BASE < va + filesz:
                self.assertTrue(flags & 1)
                first = struct.unpack_from('<I', data, offset + BASE - va)[0]
                self.assertNotEqual(first, 4, 'GNU note header at runtime entry')
                return
        self.fail('Runtime entry has no file-backed executable mapping')

    def test_link_with_build_id_and_unrelated_text_first(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            script = tmp / 'runtime.ld'
            script.write_text(run('riscv64-linux-gnu-gcc', '-E', '-P', '-x', 'c',
                                  '-D__PREPROCESSING__',
                                  '-I' + str(ROOT / 'keystone/runtime/include'),
                                  str(ROOT / 'keystone/runtime/runtime.ld.S')))
            source = tmp / 'entry.S'
            source.write_text('.text\n.word 0x12345678\n'
                              '.section .text._start,"ax",@progbits\n'
                              '.global _start\n_start: nop\n')
            obj = tmp / 'entry.o'
            run('riscv64-linux-gnu-as', str(source), '-o', str(obj))
            elf = tmp / 'runtime.elf'
            run('riscv64-linux-gnu-ld', '--build-id=sha1', '-T', str(script),
                str(obj), '-o', str(elf))
            self.check_elf(elf)

    def test_built_examples(self):
        for name in ('hello', 'hello-native'):
            with self.subTest(example=name):
                self.check_elf(BUILD / 'examples' / name / 'eyrie-rt')


if __name__ == '__main__':
    unittest.main()
