#!/usr/bin/env python3
"""Test guest staging and shutdown/mount control without privileged syscalls."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GuestToolsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="guest tools ")
        self.addCleanup(self.tmp.cleanup)
        self.work = Path(self.tmp.name)
        self.log = self.work / "calls"
        self.mock = self.work / "mock-busybox"
        self.mock.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$CALL_LOG"\n'
                             'case "$1" in mountpoint) exit 1;; mount) exit "${MOUNT_EXIT:-0}";; esac\n')
        self.mock.chmod(0o755)
        self.env = dict(os.environ, KEYSTONE_GUEST_BUSYBOX=str(self.mock), CALL_LOG=str(self.log))

    def run_guest(self, name, *args):
        return subprocess.run(["sh", str(ROOT / "scripts/guest" / name), *args],
                              text=True, capture_output=True, env=self.env)

    def test_stage_all_helpers_and_replace_symlink_entry(self):
        stage = self.work / "rootfs"
        sbin = stage / "usr/sbin"
        sbin.mkdir(parents=True)
        original = sbin / "systemctl"
        original.write_text("preserve systemctl")
        (sbin / "shutdown").symlink_to("systemctl")
        result = subprocess.run(["bash", str(ROOT / "scripts/install-keystone-guest-tools.sh"),
                                 str(stage), str(self.mock)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in ("usr/bin/run-keystone-hello", "usr/bin/mount-hostshare", "usr/sbin/shutdown",
                     "usr/sbin/poweroff", "usr/sbin/reboot", "usr/bin/shutdown", "usr/bin/poweroff",
                     "usr/bin/reboot", "usr/libexec/keystone/busybox"):
            self.assertTrue(os.access(stage / name, os.X_OK), name)
        self.assertFalse((sbin / "shutdown").is_symlink())
        self.assertEqual(original.read_text(), "preserve systemctl")

    def test_shutdown_syncs_and_remounts_before_poweroff(self):
        result = self.run_guest("shutdown", "-h", "now")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log.read_text().splitlines(), ["sync", "mount -o remount,ro / /", "poweroff -f"])

    def test_shutdown_cancels_if_remount_fails(self):
        self.env["MOUNT_EXIT"] = "1"
        self.assertNotEqual(self.run_guest("shutdown", "-h", "now").returncode, 0)
        self.assertNotIn("poweroff", self.log.read_text())

    def test_reboot_and_unsupported_time(self):
        self.assertEqual(self.run_guest("shutdown", "-r", "now").returncode, 0)
        self.assertIn("reboot -f", self.log.read_text())
        self.log.unlink()
        self.assertNotEqual(self.run_guest("shutdown", "+10").returncode, 0)
        self.assertFalse(self.log.exists())

    def test_mount_uses_qemu_tag_and_requested_target(self):
        result = self.run_guest("mount-hostshare", "/mnt/host files")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host files", self.log.read_text())


if __name__ == "__main__":
    unittest.main()
