#!/usr/bin/env python3
"""SDK startup CPU/binary regression; not a QEMU system-guest SM PASS."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get('KEYSTONE_TEST_BUILD_DIR', ROOT / '.build-keystone'))
CROSS = os.environ.get('CROSS_COMPILE', 'riscv64-linux-gnu-')


class NativeEntryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('qemu-riscv64'):
            raise unittest.SkipTest('qemu-riscv64 user-mode emulator required for startup probe')
        cls.archive = BUILD / 'sdk-install/lib/libkeystone-eapp.a'
        if not cls.archive.exists():
            raise unittest.SkipTest('Build the SDK first or set KEYSTONE_TEST_BUILD_DIR')

    def test_gp_start_and_main_entry_negative_control(self):
        with tempfile.TemporaryDirectory(prefix='keystone-native-entry-') as tmp:
            for fixed in (False, True):
                elf = Path(tmp) / ('fixed' if fixed else 'old-main-entry')
                options = '-Wl,-u,keystone_eapp_start,-e,keystone_eapp_start' if fixed else '-Wl,-e,main'
                command = [CROSS + 'gcc', '-O2', '-ffreestanding', '-fno-stack-protector',
                           '-static', '-nostdlib', options,
                           str(ROOT / 'scripts/tests/keystone-native-gp-probe.c'),
                           str(self.archive), '-o', str(elf)]
                # This supports the isolated cross toolchain used in cloud builds.
                if os.environ.get('KEYSTONE_TEST_SYSROOT'):
                    command.insert(1, '--sysroot=' + os.environ['KEYSTONE_TEST_SYSROOT'])
                subprocess.run(command, check=True, capture_output=True)
                checked = subprocess.run([sys.executable, str(ROOT / 'scripts/check-keystone-native-entry.py'),
                                          CROSS + 'nm', str(elf)], capture_output=True, text=True)
                self.assertEqual(checked.returncode, 0 if fixed else 1, checked.stdout + checked.stderr)
                executed = subprocess.run(['qemu-riscv64', str(elf)], capture_output=True)
                self.assertEqual(executed.returncode, 0 if fixed else 41, executed.stderr)


if __name__ == '__main__':
    unittest.main()
