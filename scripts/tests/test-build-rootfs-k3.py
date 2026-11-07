#!/usr/bin/env python3
"""Integration tests requiring mke2fs >= 1.47.2 with libarchive support."""
import hashlib
import io
import os
import shutil
import shlex
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

    def build(self, checksum="", env=None):
        return subprocess.run([
            "bash", str(ROOT / "scripts/build-rootfs-k3.sh"), str(self.image),
            str(self.work / "build"), str(self.archive), checksum,
        ], text=True, capture_output=True, env=env)

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

    def test_directory_fallback_preserves_metadata(self):
        real_mke2fs = shutil.which("mke2fs")
        bindir = self.work / "bin"
        bindir.mkdir()
        wrapper = bindir / "mke2fs"
        wrapper.write_text(
            '#!/bin/sh\n'
            'previous=""\n'
            'for arg in "$@"; do\n'
            ' if [ "$previous" = "-d" ] && [ ! -d "$arg" ]; then\n'
            '  echo "Not a directory" >&2; exit 1\n'
            ' fi\n previous=$arg\ndone\n'
            f'exec {shlex.quote(real_mke2fs)} "$@"\n'
        )
        wrapper.chmod(0o755)
        env = dict(os.environ, PATH=str(bindir) + os.pathsep + os.environ["PATH"])
        result = self.build(env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("using directory input", result.stdout)
        self.assertEqual(self.image.stat().st_size, 8 * 1024**3)
        stat = self.debug("stat /var/owned")
        self.assertRegex(stat, r"User:\s+123\s+Group:\s+456")
        self.assertIn("Mode:  0640", stat)
        self.assertIn("Links: 2", stat)
        self.assertIn("Type: character special", self.debug("stat /dev/null"))
        self.assertFalse(list((self.work / "build").glob("stage.*")))

    def test_keystone_overlay_is_separate_and_preserves_executable_mode(self):
        overlay = self.work / "overlay"
        example = overlay / "usr/share/keystone/examples/hello.ke"
        example.parent.mkdir(parents=True)
        example.write_bytes(b"example package fixture")
        example.chmod(0o755)
        module = overlay / "usr/lib/modules/test/extra/keystone-driver.ko"
        module.parent.mkdir(parents=True)
        module.write_bytes(b"driver fixture")
        bianbu = self.work / "bianbu-existing.img"
        bianbu.write_bytes(b"existing guest modifications")
        result = subprocess.run(["bash", str(ROOT / "scripts/build-rootfs-keystone.sh"),
                                 str(self.image), str(self.work / "keystone-build"),
                                 str(overlay), str(self.archive)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.debug("cat /usr/share/keystone/examples/hello.ke"), "example package fixture")
        self.assertIn("Mode:  0755", self.debug("stat /usr/share/keystone/examples/hello.ke"))
        self.assertEqual(self.debug("cat /usr/lib/modules/test/extra/keystone-driver.ko"), "driver fixture")
        self.assertIn("keystone-root", self.debug("stats"))
        self.assertEqual(bianbu.read_bytes(), b"existing guest modifications")
        before = self.image.stat().st_mtime_ns
        result = subprocess.run(["bash", str(ROOT / "scripts/build-rootfs-keystone.sh"),
                                 str(self.image), str(self.work / "keystone-build"),
                                 str(overlay), str(self.archive)], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
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
