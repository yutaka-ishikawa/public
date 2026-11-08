#!/usr/bin/env python3
"""Run actual system guests twice with the same identity, then without keys."""
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


def run(args, name, provider):
    record = {'case': name, 'provider': provider, 'pass': False}
    log = args.logs / (name + '.log')
    record['log'] = str(log)
    prefix = args.prefix.resolve()
    bios = prefix / ('firmware/k3/crypto-qemu-dev/fw_dynamic.bin' if provider == 'qemu-dev'
                     else 'firmware/k3/fw_dynamic.bin')
    paths = [bios, prefix / 'bin/qemu-system-riscv64', prefix / 'boot/k3/Image',
             ROOT / 'verifications/crypto/build/crypto.ke']
    try:
        record['sha256'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        with tempfile.TemporaryDirectory(prefix='crypto-qemu-') as tmp:
            overlay = Path(tmp) / 'rootfs.qcow2'
            subprocess.run([str(prefix / 'bin/qemu-img'), 'create', '-f', 'qcow2', '-F', 'raw',
                            '-b', str(args.rootfs.resolve()), str(overlay)], check=True)
            command = ['bash', str(ROOT / 'scripts/run-k3.sh'), '--prefix', str(prefix),
                       '--crypto', provider, '--rootfs', str(overlay), '--rootfs-format', 'qcow2']
            record['command'] = command
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT)
            selector = selectors.DefaultSelector()
            selector.register(process.stdout, selectors.EVENT_READ)
            captured = b''
            sent = False
            deadline = time.monotonic() + args.timeout
            try:
                with log.open('wb') as stream:
                    while time.monotonic() < deadline:
                        for key, _ in selector.select(1):
                            data = os.read(key.fd, 65536)
                            if not data:
                                raise RuntimeError('QEMU exited before completion')
                            stream.write(data)
                            stream.flush()
                            captured += data
                        if not sent and re.search(rb'(?:root@[^\r\n]*|bash-[0-9.]+)#', captured):
                            if args.all:
                                test = f'KEYSTONE_CRYPTO_EXPECT={provider} bash /mnt/host/verifications/run-all.sh'
                            else:
                                test = f'bash /mnt/host/verifications/crypto/run-guest.sh {provider}'
                            # run-guest's metadata default is relative to the shared repository.
                            setup = ('mkdir -p /proc /sys /mnt/host; mount -t proc proc /proc 2>/dev/null; '
                                     'mount -t sysfs sysfs /sys 2>/dev/null; '
                                     'mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host && '
                                     + test + '; rc=$?; printf "\\nCRYPTO_CASE_EXIT=%d\\n" "$rc"\n')
                            process.stdin.write(setup.encode())
                            process.stdin.flush()
                            sent = True
                        match = re.search(rb'CRYPTO_CASE_EXIT=(\d+)\r*\n', captured)
                        if match:
                            banner = f'[SM/CRYPTO] provider={provider} '.encode()
                            minimum = 3 if provider == 'qemu-dev' else 1
                            record['guest_exit'] = int(match[1])
                            record['pass'] = (int(match[1]) == 0 and banner in captured and
                                              captured.count(b'PASS GUEST CRYPTO:') >= minimum)
                            if not record['pass']:
                                record['error'] = 'Guest failure, provider mismatch or incomplete crypto test'
                            return record
                    raise TimeoutError('Timed out waiting for ' + ('test result' if sent else 'root prompt'))
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
    parser.add_argument('--logs', type=Path, default=ROOT / 'verifications/crypto/results')
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--all', action='store_true', help='Run the existing guest suite each boot and the seven-case RNG matrix')
    args = parser.parse_args()
    # Guest metadata paths use the repository's standard installation.
    if args.prefix.resolve() != (ROOT / 'install/keystone').resolve():
        parser.error('This runner currently requires the standard install/keystone prefix; custom boot can be tested manually')
    args.rootfs = args.rootfs or args.prefix / 'rootfs/keystone-rootfs.img'
    args.logs.mkdir(parents=True, exist_ok=True)
    records = []
    for name, provider in [('qemu-dev-boot1', 'qemu-dev'), ('qemu-dev-boot2', 'qemu-dev'),
                           ('unavailable', 'unavailable')]:
        result = run(args, name, provider)
        records.append(result)
        scope = ('SBI 2001/2002/2003/2004(undefined)/2005/3001/3002/3003/3004/3006'
                 if args.all else 'SBI 3002/3003')
        print(('PASS' if result['pass'] else 'FAIL'), scope, name,
              result.get('error', ''), flush=True)
        # Show actual guest assertions, rather than treating a boot summary as
        # a separate assertion for every SBI in the lifecycle.
        serial = Path(result['log'])
        if serial.is_file():
            for line in serial.read_text(errors='replace').splitlines():
                if line.startswith(('PASS ', 'FAIL ', 'SKIP ')):
                    print('  ' + line, flush=True)
        if not result['pass']:
            print('Log:', result['log'], flush=True)
            path = Path(result['log'])
            if path.exists():
                print('\n'.join(path.read_text(errors='replace').splitlines()[-25:]), flush=True)
        (args.logs / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')
    if args.all:
        rng_logs = args.logs / 'rng'
        result = subprocess.run(['python3', str(ROOT / 'verifications/random/run-qemu-matrix.py'),
                                 '--prefix', str(args.prefix), '--rootfs', str(args.rootfs),
                                 '--logs', str(rng_logs), '--timeout', str(args.timeout)])
        records.append({'case': 'rng-matrix-regression', 'pass': result.returncode == 0,
                        'log': str(rng_logs / 'manifest.json')})
        (args.logs / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')
    return 0 if all(record['pass'] for record in records) else 1


if __name__ == '__main__':
    raise SystemExit(main())
