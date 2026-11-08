#!/usr/bin/env python3
"""Exercise real ext4 import/update and failure preservation; no SM emulation."""
import fcntl
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
UPDATE = ROOT / 'scripts/update-keystone-rootfs.py'


class ImageUpdateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='keystone-update-test-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.image = self.work / 'rootfs.img'
        self.image.touch()
        self.image.chmod(0o640)
        with self.image.open('r+b') as stream:
            stream.truncate(64 * 1024 * 1024)
        subprocess.run(['mkfs.ext4', '-q', '-F', str(self.image)], check=True)
        self.overlay = self.work / 'overlay'
        (self.overlay / 'usr/bin').mkdir(parents=True)
        self.tool = self.overlay / 'usr/bin/new-tool'
        self.tool.write_bytes(b'#!/bin/sh\necho updated\n')
        self.tool.chmod(0o755)
        keep = self.work / 'keep'
        keep.write_bytes(b'user-data-must-survive')
        subprocess.run(['debugfs', '-w', '-R', f'write {keep} /keep', str(self.image)],
                       check=True, capture_output=True)

    def update(self):
        return subprocess.run([sys.executable, str(UPDATE), str(self.image),
                               str(self.overlay)], capture_output=True, text=True)

    def digest(self):
        return hashlib.sha256(self.image.read_bytes()).digest()

    def dump(self, guest):
        output = self.work / 'dump'
        output.unlink(missing_ok=True)
        subprocess.run(['debugfs', '-R', f'dump {guest} {output}', str(self.image)],
                       check=True, capture_output=True)
        return output.read_bytes()

    def test_update_and_replacement_preserve_user_data(self):
        for data in (self.tool.read_bytes(), b'new revision\n'):
            self.tool.write_bytes(data)
            result = self.update()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(self.dump('/usr/bin/new-tool'), data)
            self.assertEqual(self.dump('/keep'), b'user-data-must-survive')
            self.assertEqual(self.image.stat().st_mode & 0o777, 0o640)
            stat = subprocess.check_output(['debugfs', '-R', 'stat /usr/bin/new-tool',
                                            str(self.image)], stderr=subprocess.DEVNULL)
            self.assertIn(b'0755', stat)

    def test_invalid_overlay_keeps_original(self):
        (self.overlay / 'unsupported-link').symlink_to('usr/bin/new-tool')
        before = self.digest()
        self.assertNotEqual(self.update().returncode, 0)
        self.assertEqual(self.digest(), before)
        self.assertEqual(list(self.work.glob('.keystone-update-*')), [])

    def test_active_disk_lock_refuses_update(self):
        before = self.digest()
        with self.image.open('r+b') as stream:
            fcntl.lockf(stream, fcntl.LOCK_EX | fcntl.LOCK_NB, 1, 100)
            result = self.update()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('in use', result.stderr)
        self.assertEqual(self.digest(), before)


if __name__ == '__main__':
    required = ['mkfs.ext4', 'debugfs', 'e2fsck', 'cp']
    missing = [tool for tool in required if not shutil.which(tool)]
    if missing:
        raise SystemExit('Missing real filesystem tools: ' + ', '.join(missing))
    unittest.main()
