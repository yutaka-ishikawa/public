#!/usr/bin/env python3
"""Execute raw loader stack setup at a relocated EPM address under QEMU user mode."""
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("KEYSTONE_TEST_BUILD_DIR", ROOT / ".build-keystone"))


class LoaderEntryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="loader entry ")
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.checker = Path(cls.tmp.name) / "check-entry"
        subprocess.run(["riscv64-linux-gnu-gcc", "-static", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(Path(__file__).with_name("keystone-loader-entry-check.c")),
                        "-o", str(cls.checker)], check=True)

    def execute_entry(self, loader):
        return subprocess.run(["qemu-riscv64", str(self.checker), str(loader)],
                              text=True, capture_output=True, timeout=20)

    def test_reproduces_the_old_got_stack_failure(self):
        # The old first instructions load _estack via a GOT slot at 0xa038.
        # That slot is missing from loader.bin and reads as zero in EPM.
        broken = Path(self.tmp.name) / "old-entry.bin"
        broken.write_bytes(struct.pack("<5I", 0x0000a117, 0x03813103, 0xfc810113,
                                       0x00013023, 0x000000ef))
        result = self.execute_entry(broken)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("signal=11", result.stdout)
        self.assertIn("stack_in_EPM=0", result.stdout)

    def test_linker_rejects_a_got_in_the_raw_loader(self):
        source = Path(self.tmp.name) / "got.S"
        obj = source.with_suffix(".o")
        source.write_text('.section .text\n.globl _start\n_start: nop\n.section .got,"aw"\n.quad 0x1234\n')
        subprocess.run(["riscv64-linux-gnu-as", str(source), "-o", str(obj)], check=True)
        result = subprocess.run(["riscv64-linux-gnu-ld", "-T",
                                 str(ROOT / "keystone/runtime/loader-binary/loader.lds"),
                                 str(obj), "-o", str(source.with_suffix(".elf"))],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsupported GOT", result.stderr)

    def test_hello_loader_has_a_relocated_aligned_stack(self):
        result = self.execute_entry(BUILD / "examples/hello/loader.bin")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("signal=5", result.stdout)  # Deliberate ebreak before load_runtime.
        self.assertIn("stack_in_EPM=1", result.stdout)

    def test_hello_native_loader_has_a_relocated_aligned_stack(self):
        result = self.execute_entry(BUILD / "examples/hello-native/loader.bin")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("stack_in_EPM=1", result.stdout)


if __name__ == "__main__":
    unittest.main()
