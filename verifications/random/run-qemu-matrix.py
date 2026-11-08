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


def evaluate_output(captured, backend, error):
    """Interpret serial output; this is not a substitute for executing a guest."""
    match = re.search(rb"RNG_CASE_EXIT=(\d+)\r*\n", captured)
    if not match:
        return None
    result = {"guest_exit": int(match[1]), "pass": False}
    banner = re.search(
        rb"\[SM/RNG\] selected=(\w+) quality=[\w-]+ status=(\d+)\r*\n",
        captured)
    status = {0: 0, -38: 100100, -11: 100101, -5: 100102}[error]
    problems = []
    if result["guest_exit"]:
        problems.append(f"guest setup/test exited with status {result['guest_exit']}")
    if not banner:
        problems.append("SM/RNG startup banner missing (old/wrong firmware or boot failure)")
    else:
        result["selected_backend"] = banner[1].decode()
        result["sm_status"] = int(banner[2])
        if result["selected_backend"] != backend or result["sm_status"] != status:
            problems.append(f"SM reports {result['selected_backend']}/status={result['sm_status']}; "
                            f"expected {backend}/status={status}")
    if b"PASS GUEST SBI 3001:" not in captured:
        problems.append("guest RNG PASS message missing")
    if b"PASS GUEST SBI 3001: AT_RANDOM" not in captured:
        problems.append("AT_RANDOM PASS message missing")
    result["pass"] = not problems
    if problems:
        result["error"] = "; ".join(problems)
    return result


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
                            raise RuntimeError(f"QEMU exited before result marker (exit={process.poll()})")
                        log.write(chunk)
                        log.flush()
                        captured += chunk
                    if not sent and re.search(rb"(?:root@[^\r\n]*|bash-[0-9.]+|sh-[0-9.]+)#", captured):
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
                    outcome = evaluate_output(captured, backend, error)
                    if outcome is not None:
                        record.update(outcome)
                        return record
                phase = "guest test after command submission" if sent else "guest root prompt"
                raise TimeoutError(f"Timed out after {args.timeout}s waiting for {phase}")
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
            result = {"case": case[0], "pass": False, "error": str(exc),
                      "log": str(args.logs / f"{case[0]}.log")}
        results.append(result)
        print(("PASS" if result["pass"] else "FAIL"), case[0], flush=True)
        if not result["pass"]:
            print("  Reason:", result.get("error", "unknown failure"), flush=True)
            print("  Log:", result.get("log"), flush=True)
            logpath = Path(result["log"])
            if logpath.is_file():
                print("  Last serial lines:", flush=True)
                for line in logpath.read_text(errors="replace").splitlines()[-20:]:
                    print("   ", line, flush=True)
        (args.logs / "manifest.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0 if results and all(item["pass"] for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
