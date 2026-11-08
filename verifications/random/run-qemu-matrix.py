#!/usr/bin/env python3
"""Boot real system guests; never substitute host implementations for the SM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
CASES = [
    ("zkr", "zkr", 0, []),
    ("jupiter2", "jupiter2", 0, []),
    ("counter", "counter", 0, []),
    ("zkr-absent", "zkr", -38, ["--zkr", "off"]),
    ("wtm-absent", "jupiter2", -38, ["--wtm-rng", "off"]),
    ("wtm-not-ready", "jupiter2", -11, ["--wtm-rng-fault", "not-ready"]),
    ("wtm-zero", "jupiter2", 0, ["--wtm-rng-fault", "zero"]),
]


def sha256(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run_case(args, case):
    name, backend, error, overrides = case
    prefix = args.prefix.resolve()
    bios = prefix / f"firmware/k3/rng-{backend}/fw_dynamic.bin"
    qemu = prefix / "bin/qemu-system-riscv64"
    kernel = prefix / "boot/k3/Image"
    record = {"case": name, "expected_errno": error, "pass": False,
              "sha256": {str(p): sha256(p) for p in (bios, qemu, kernel)}}
    logpath = args.logs / f"{name}.log"
    record["log"] = str(logpath)
    with tempfile.TemporaryDirectory(prefix="rng-qemu-") as tmp:
        disk = Path(tmp) / "rootfs.qcow2"
        subprocess.run([str(args.qemu_img), "create", "-f", "qcow2", "-F", "raw",
                        "-b", str(args.rootfs.resolve()), str(disk)], check=True)
        command = ["bash", str(ROOT / "scripts/run-k3.sh"), "--prefix", str(prefix),
                   "--rng", backend, "--rootfs", str(disk), "--rootfs-format", "qcow2",
                   "--hostfs", str(ROOT), *overrides]
        record["command"] = command
        process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        captured = b""
        sent = False
        deadline = time.monotonic() + args.timeout
        try:
            with logpath.open("wb") as log:
                while time.monotonic() < deadline:
                    for key, _ in selector.select(1):
                        chunk = os.read(key.fd, 65536)
                        if not chunk:
                            raise RuntimeError("QEMU exited before result marker")
                        log.write(chunk)
                        log.flush()
                        captured += chunk
                    if not sent and re.search(rb"root@[^\r\n]*#", captured):
                        setup = ("mkdir -p /proc /sys /mnt/host; "
                                 "mount -t proc proc /proc 2>/dev/null; "
                                 "mount -t sysfs sysfs /sys 2>/dev/null; "
                                 "mount -t 9p -o trans=virtio,version=9p2000.L "
                                 "hostshare /mnt/host && "
                                 f"bash /mnt/host/verifications/random/run-guest.sh {error}; "
                                 "rc=$?; printf '\\nRNG_CASE_EXIT=%d\\n' \"$rc\"\n")
                        process.stdin.write(setup.encode())
                        process.stdin.flush()
                        sent = True
                    match = re.search(rb"RNG_CASE_EXIT=(\d+)\r?\n", captured)
                    if match:
                        status = 0 if error == 0 else 100100 if error == -38 else 100101
                        banner = re.search(
                            rb"\[SM/RNG\] selected=(\w+) quality=[\w-]+ status=(\d+)\r?\n",
                            captured)
                        record["pass"] = (match[1] == b"0" and banner is not None and
                                          banner[1].decode() == backend and
                                          int(banner[2]) == status and
                                          b"PASS GUEST SBI 3001:" in captured)
                        return record
                raise TimeoutError("Guest boot/test timed out")
        finally:
            selector.close()
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=ROOT / "install/keystone")
    parser.add_argument("--rootfs", type=Path)
    parser.add_argument("--qemu-img", type=Path)
    parser.add_argument("--logs", type=Path, default=ROOT / "verifications/random/results")
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--case", choices=[case[0] for case in CASES], action="append")
    args = parser.parse_args()
    args.rootfs = args.rootfs or args.prefix / "rootfs/keystone-rootfs.img"
    args.qemu_img = args.qemu_img or args.prefix / "bin/qemu-img"
    args.logs.mkdir(parents=True, exist_ok=True)
    results = []
    for case in CASES:
        if args.case and case[0] not in args.case:
            continue
        try:
            result = run_case(args, case)
        except (OSError, RuntimeError, TimeoutError, subprocess.SubprocessError) as exc:
            result = {"case": case[0], "pass": False, "error": str(exc)}
        results.append(result)
        print(("PASS" if result["pass"] else "FAIL"), case[0], flush=True)
        (args.logs / "manifest.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0 if results and all(item["pass"] for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
