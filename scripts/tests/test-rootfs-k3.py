#!/usr/bin/env python3
"""Exercise rootfs selection and preservation without booting a guest."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RootfsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="k3 test ")
        self.addCleanup(self.tmp.cleanup)
        self.prefix = Path(self.tmp.name)
        for name in ("firmware/k3/fw_dynamic.bin", "boot/k3/Image"):
            p = self.prefix / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"fixture")
        qemu = self.prefix / "bin/qemu-system-riscv64"
        qemu.parent.mkdir()
        qemu.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
        qemu.chmod(0o755)
        (self.prefix / "rootfs").mkdir()

    def run_guest(self, *args, image="bianbu"):
        return subprocess.run(
            ["bash", str(ROOT / "scripts/run-k3.sh"), "--prefix", str(self.prefix),
             *(["--image", image] if image else []), *args],
            text=True, capture_output=True,
        )

    def test_bianbu_default_and_explicit_override(self):
        minimal = self.prefix / "rootfs/k3-minimal-rootfs.img"
        minimal.write_bytes(b"minimal")
        result = self.run_guest()
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertIn("Missing bianbu rootfs", result.stderr)
        bianbu = self.prefix / "rootfs/bianbu-rootfs.img"
        bianbu.write_bytes(b"bianbu")
        result = self.run_guest()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"file={bianbu},format=raw,if=none,id=hd0", result.stdout)
        result = self.run_guest("--rootfs", str(minimal))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"file={minimal},format=raw,if=none,id=hd0", result.stdout)

    def test_missing_and_empty_images_fail(self):
        result = self.run_guest()
        self.assertEqual(result.returncode, 3)
        self.assertIn("build-install-k3.sh", result.stderr)
        (self.prefix / "rootfs/bianbu-rootfs.img").touch()
        (self.prefix / "rootfs/k3-minimal-rootfs.img").write_bytes(b"minimal")
        result = self.run_guest()
        self.assertEqual(result.returncode, 3)
        self.assertIn("bianbu-rootfs.img", result.stderr)

    def test_keystone_is_default_and_overrides_are_forwarded(self):
        rootfs = self.prefix / "rootfs/keystone-rootfs.img"
        rootfs.write_bytes(b"keystone")
        result = self.run_guest(image=None)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("jupiter2", result.stdout)
        self.assertIn("fw_dynamic.bin", result.stdout)
        self.assertIn("virtio-blk-pci,drive=hd0", result.stdout)
        self.assertIn("keystone-rootfs.img", result.stdout)
        self.assertIn("init=/bin/bash", result.stdout)
        custom = self.prefix / "custom Image"
        custom.write_bytes(b"custom")
        result = self.run_guest("--kernel", str(custom), "--smp", "2", "--append", "custom args", image="keystone")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(str(custom), result.stdout)
        self.assertIn("custom args", result.stdout)
        self.assertIn("-smp\n2\n", result.stdout)

    def test_invalid_profile_is_rejected(self):
        self.assertEqual(self.run_guest(image="invalid").returncode, 2)

    def test_existing_guest_data_is_preserved(self):
        for name in ("bianbu-rootfs.img",):
            image = self.prefix / "rootfs" / name
            image.write_bytes(b"precious guest data")
            result = subprocess.run(
                ["bash", str(ROOT / "scripts/build-install-k3.sh"), "--only", "rootfs",
                 "--prefix", str(self.prefix), "--build-dir", str(self.prefix / "build")],
                text=True, capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(image.read_bytes(), b"precious guest data")


if __name__ == "__main__":
    unittest.main()
