#!/usr/bin/env python3
"""Run the integrated Keystone samples in actual K3 QEMU Linux guests."""
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

ROOT = Path(__file__).resolve().parents[1]


def run(args, sstc):
    log = args.logs / f'{args.crypto}-sstc-{sstc}.log'
    record = {'sstc': sstc, 'pass': False, 'log': str(log)}
    try:
        paths = [args.prefix / ('firmware/k3/crypto-qemu-dev/fw_dynamic.bin' if args.crypto == 'qemu-dev' else 'firmware/k3/fw_dynamic.bin'),
                 args.prefix / 'boot/k3/Image', args.prefix / 'bin/qemu-system-riscv64',
                 *[ROOT / 'samples' / p for p in ('helloworld/build/hello.ke', 'hello-native/build/hello-native.ke', 'attestation/build/attestor.ke', 'tests/build/tests.ke')]]
        record['source_revision'] = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        record['source_dirty'] = bool(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True))
        record['sha256'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
        with tempfile.TemporaryDirectory(prefix='samples-qemu-') as temporary:
            overlay = Path(temporary) / 'rootfs.qcow2'
            subprocess.run([str(args.prefix / 'bin/qemu-img'), 'create', '-f', 'qcow2',
                            '-F', 'raw', '-b', str(args.rootfs.resolve()), str(overlay)], check=True)
            command = ['bash', str(ROOT / 'scripts/run-k3.sh'), '--prefix', str(args.prefix),
                       '--crypto', args.crypto, '--sstc', sstc, '--rootfs', str(overlay), '--rootfs-format', 'qcow2']
            record['basic'] = args.basic
            record['crypto'] = args.crypto
            record['command'] = command
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT)
            captured = b''
            sent = False
            selector = selectors.DefaultSelector()
            selector.register(process.stdout, selectors.EVENT_READ)
            deadline = time.monotonic() + args.timeout
            try:
                with log.open('wb') as stream:
                    while time.monotonic() < deadline:
                        for key, _ in selector.select(1):
                            data = os.read(key.fd, 65536)
                            if not data:
                                raise RuntimeError('QEMU exited before verification completed')
                            stream.write(data)
                            stream.flush()
                            captured += data
                        if b'Kernel panic - not syncing' in captured or b'[runtime] non-handlable' in captured:
                            raise RuntimeError('Guest kernel panic')
                        if not sent and re.search(rb'(?:root@[^\r\n]*|bash-[0-9.]+)#', captured):
                            setup = ('mkdir -p /proc /sys /mnt/host; '
                                     'mount -t proc proc /proc 2>/dev/null; '
                                     'mount -t sysfs sysfs /sys 2>/dev/null; '
                                     'mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host && '
                                     'bash /mnt/host/samples/run-guest.sh' +
                                     (' --basic' if args.basic else '') + '; '
                                     'rc=$?; printf "\\nSAMPLE_CASE_EXIT=%d\\n" "$rc"\n')
                            process.stdin.write(setup.encode())
                            process.stdin.flush()
                            sent = True
                        match = re.search(rb'SAMPLE_CASE_EXIT=(\d+)\r*\n', captured)
                        if match:
                            record['guest_exit'] = int(match[1])
                            record['pass'] = int(match[1]) == 0 and b'PASS SAMPLE SUITE:' in captured
                            return record
                    raise TimeoutError('Timeout waiting for guest verification')
            finally:
                selector.close()
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    except (OSError, RuntimeError, TimeoutError, subprocess.SubprocessError) as exc:
        record['error'] = str(exc)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=ROOT / 'install/keystone')
    parser.add_argument('--rootfs', type=Path)
    parser.add_argument('--basic', action='store_true', help='Run only examples without crypto; explicit SKIP for attestation/sealing')
    parser.add_argument('--crypto', choices=['qemu-dev', 'unavailable'])
    parser.add_argument('--logs', type=Path, default=ROOT / 'samples/results')
    parser.add_argument('--sstc', choices=['on', 'off', 'both'], default='both')
    parser.add_argument('--timeout', type=int, default=300)
    args = parser.parse_args()
    args.crypto = args.crypto or ('unavailable' if args.basic else 'qemu-dev')
    args.prefix = args.prefix.resolve()
    args.rootfs = args.rootfs or args.prefix / 'rootfs/keystone-rootfs.img'
    args.logs.mkdir(parents=True, exist_ok=True)
    records = []
    for sstc in (['on', 'off'] if args.sstc == 'both' else [args.sstc]):
        result = run(args, sstc)
        records.append(result)
        print(('PASS' if result['pass'] else 'FAIL'), 'SAMPLES', args.crypto, sstc, result.get('error', ''), flush=True)
        print('Log:', result['log'], flush=True)
        (args.logs / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')
    return 0 if all(r['pass'] for r in records) else 1


if __name__ == '__main__':
    raise SystemExit(main())
