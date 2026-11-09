#!/usr/bin/env python3
"""Run C abnormal/concurrency integration through Linux, driver, OpenSBI, SM and Eyrie."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import selectors
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def run(args, sstc):
    log = args.logs / f'sstc-{sstc}.log'
    record = {'sstc': sstc, 'pass': False, 'log': str(log)}
    try:
        paths = [args.bios or args.prefix / 'firmware/k3/fw_dynamic.bin',
                 args.prefix / 'boot/k3/Image', args.prefix / 'bin/qemu-system-riscv64',
                 *[ROOT / 'verifications/robustness/build' / name for name in
                   ['coordinator', 'eapp', 'eyrie-rt', 'loader.bin']]]
        if args.raw_only or args.faults or args.pmp_fault or args.ipi_fault:
            paths.append(ROOT / 'verifications/robustness/kernel/probe.ko')
        if args.faults:
            paths += sorted((ROOT / 'verifications/robustness/build').glob('fault-*'))
        if args.crypto_pins:
            paths += [args.crypto_pins / name for name in ['device-public-key.bin', 'sm-hash.bin']]
        record['source_revision'] = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        record['source_dirty'] = bool(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True))
        record['sha256'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
        with tempfile.TemporaryDirectory(prefix='robustness-qemu-') as temporary:
            overlay = Path(temporary) / 'rootfs.qcow2'
            subprocess.run([str(args.prefix / 'bin/qemu-img'), 'create', '-f', 'qcow2',
                            '-F', 'raw', '-b', str(args.rootfs.resolve()), str(overlay)], check=True)
            command = ['bash', str(ROOT / 'scripts/run-k3.sh'), '--prefix', str(args.prefix),
                       '--sstc', sstc, '--smp', str(args.smp), '--rootfs', str(overlay), '--rootfs-format', 'qcow2']
            if args.bios:
                command += ['--bios', str(args.bios.resolve())]
            record['rootfs'] = str(args.rootfs.resolve())
            record['raw_only'] = args.raw_only
            record['faults'] = args.faults
            record['pmp_fault'] = args.pmp_fault
            record['ipi_fault'] = args.ipi_fault
            record['group'] = args.group
            record['workers'] = args.workers
            record['cycles'] = args.cycles
            record['rng_expected'] = args.rng
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
                        if b'Kernel panic - not syncing' in captured:
                            raise RuntimeError('Guest kernel panic')
                        if not sent and re.search(rb'(?:root@[^\r\n]*|bash-[0-9.]+)#', captured):
                            setup = ('mkdir -p /proc /sys /mnt/host; '
                                     'mount -t proc proc /proc 2>/dev/null; '
                                     'mount -t sysfs sysfs /sys 2>/dev/null; '
                                     'mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host && '
                                     'bash /mnt/host/verifications/robustness/run-guest.sh' +
                                     f' --workers {args.workers} --cycles {args.cycles} --rng {args.rng} --timeout {args.guest_timeout}' +
                                     (' --raw-only' if args.raw_only else '') +
                                     (' --faults' if args.faults else '') +
                                     (' --pmp-fault' if args.pmp_fault else '') +
                                     (' --ipi-fault' if args.ipi_fault else '') +
                                     (f' --{args.group}-only' if args.group else '') +
                                     (f' --crypto {shlex.quote(args.guest_device_key)} {shlex.quote(args.guest_sm_hash)}' if args.crypto_pins else '') + '; '
                                     'rc=$?; printf "\\nROBUSTNESS_EXIT=%d\\n" "$rc"\n')
                            process.stdin.write(setup.encode())
                            process.stdin.flush()
                            sent = True
                        match = re.search(rb'ROBUSTNESS_EXIT=(\d+)\r*\n', captured)
                        if match:
                            record['guest_exit'] = int(match[1])
                            record['pass'] = int(match[1]) == 0 and (b'PASS ROBUSTNESS quarantine C integration' if args.pmp_fault or args.ipi_fault else b'PASS ROBUSTNESS fault C integration' if args.faults else b'PASS ROBUSTNESS raw C integration' if args.raw_only else b'PASS ROBUSTNESS finite C integration') in captured
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
    parser.add_argument('--bios', type=Path)
    parser.add_argument('--pmp-fault', action='store_true', help='Separate disposable guest: fail before PMP unset')
    parser.add_argument('--ipi-fault', action='store_true', help='Separate disposable guest: fail ACK observation after actual IPI send')
    parser.add_argument('--group', choices=['lifecycle', 'lifetime', 'stress'])
    parser.add_argument('--faults', action='store_true')
    parser.add_argument('--crypto-pins', type=Path, help='Directory containing device-public-key.bin and sm-hash.bin for the chosen BIOS')
    parser.add_argument('--raw-only', action='store_true')
    parser.add_argument('--workers', type=int, choices=range(2, 17), default=2)
    parser.add_argument('--smp', type=int, choices=[16], default=16)
    parser.add_argument('--cycles', type=int, default=4)
    parser.add_argument('--rng', type=int, choices=[32, -38, -11, -5], default=32)
    parser.add_argument('--logs', type=Path, default=ROOT / 'verifications/robustness/results')
    parser.add_argument('--sstc', choices=['on', 'off', 'both'], default='both')
    parser.add_argument('--guest-timeout', type=int, default=120)
    parser.add_argument('--timeout', type=int, default=300)
    args = parser.parse_args()
    if args.cycles < 1 or args.cycles > 3600 or args.guest_timeout < 1 or args.guest_timeout > 3600:
        parser.error('cycles and guest-timeout must be in 1..3600')
    if sum([args.raw_only, args.faults, args.pmp_fault, args.ipi_fault, bool(args.group)]) > 1:
        parser.error('raw-only, faults and group are mutually exclusive')
    args.prefix = args.prefix.resolve()
    args.rootfs = args.rootfs or args.prefix / 'rootfs/keystone-rootfs.img'
    args.logs.mkdir(parents=True, exist_ok=True)
    if args.crypto_pins:
        # Materialize ONLY public pins beneath the shared checkout.
        pins = ROOT / 'verifications/robustness/build/pins'
        pins.mkdir(parents=True, exist_ok=True)
        for binary, hexdigest in [('device-public-key.bin', 'device.hex'),
                                   ('sm-hash.bin', 'sm.hex')]:
            data = (args.crypto_pins / binary).read_bytes()
            if len(data) != (32 if binary.startswith('device') else 64):
                parser.error('Invalid public pin length')
            (pins / hexdigest).write_text(data.hex() + '\n')
        args.guest_device_key = '/mnt/host/' + str((pins / 'device.hex').relative_to(ROOT))
        args.guest_sm_hash = '/mnt/host/' + str((pins / 'sm.hex').relative_to(ROOT))
    records = []
    for sstc in (['on', 'off'] if args.sstc == 'both' else [args.sstc]):
        result = run(args, sstc)
        records.append(result)
        print(('PASS' if result['pass'] else 'FAIL'), 'ROBUSTNESS', sstc, result.get('error', ''), flush=True)
        print('Log:', result['log'], flush=True)
        (args.logs / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')
    return 0 if all(r['pass'] for r in records) else 1


if __name__ == '__main__':
    raise SystemExit(main())
