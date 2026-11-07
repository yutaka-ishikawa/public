#!/usr/bin/env python3
"""Integration tests requiring mke2fs >= 1.47.2 with libarchive support."""
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildRootfsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="bianbu test ")
        self.addCleanup(self.tmp.cleanup)
        self.work = Path(self.tmp.name)
        self.archive = self.work / "fixture.tar.gz"
        with tarfile.open(self.archive, "w:gz") as tar:
            for name in ("etc", "usr", "usr/bin", "var", "dev"):
                info = tarfile.TarInfo(name)
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                tar.addfile(info)
            for name, data, uid, gid, mode in (
                ("etc/os-release", b"ID=bianbu-test\n", 0, 0, 0o644),
                ("usr/bin/bash", b"test executable\n", 0, 0, 0o755),
                ("var/owned", b"guest data\n", 123, 456, 0o640),
            ):
                info = tarfile.TarInfo(name)
                info.size, info.uid, info.gid, info.mode = len(data), uid, gid, mode
                tar.addfile(info, io.BytesIO(data))
            link = tarfile.TarInfo("var/hardlink")
            link.type, link.linkname = tarfile.LNKTYPE, "var/owned"
            link.uid, link.gid, link.mode = 123, 456, 0o640
            tar.addfile(link)
            link = tarfile.TarInfo("bin")
            link.type, link.linkname = tarfile.SYMTYPE, "usr/bin"
            tar.addfile(link)
            device = tarfile.TarInfo("dev/null")
            device.type = tarfile.CHRTYPE
            device.devmajor, device.devminor, device.mode = 1, 3, 0o666
            tar.addfile(device)
        self.image = self.work / "bianbu.img"

    def build(self, checksum=""):
        return subprocess.run([
            "bash", str(ROOT / "scripts/build-rootfs-k3.sh"), str(self.image),
            str(self.work / "build"), str(self.archive), checksum,
        ], text=True, capture_output=True)

    def debug(self, command):
        return subprocess.run(["debugfs", "-R", command, str(self.image)],
                              text=True, capture_output=True, check=True).stdout

    def test_image_layout_metadata_and_preservation(self):
        digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        result = self.build(digest)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.image.stat().st_size, 8 * 1024**3)
        self.assertIn("ID=bianbu-test", self.debug("cat /etc/os-release"))
        stat = self.debug("stat /var/owned")
        self.assertRegex(stat, r"User:\s+123\s+Group:\s+456")
        self.assertIn("Mode:  0640", stat)
        self.assertIn("Links: 2", stat)
        self.assertIn("Type: symlink", self.debug("stat /bin"))
        self.assertIn("Type: character special", self.debug("stat /dev/null"))
        self.assertIn("bianbu-root", self.debug("stats"))
        before = self.image.stat().st_mtime_ns
        self.assertEqual(self.build().returncode, 0)
        self.assertEqual(self.image.stat().st_mtime_ns, before)

    def test_bad_checksum_never_publishes_image(self):
        result = self.build("0" * 64)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.image.exists())

    def test_invalid_archive_never_publishes_image(self):
        self.archive.write_bytes(b"not a rootfs archive")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.image.exists())
        self.assertFalse(list(self.work.glob("bianbu.img.tmp.*")))


if __name__ == "__main__":
    unittest.main()
