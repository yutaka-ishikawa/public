#!/usr/bin/env python3
"""Validate K3 read-only OpenSBI PMP inventory LOG COMPLETENESS only.

A PASS is not PMP partition approval, an IOPMP/DMA check, or attestation.
K3 Harts that remain HSM STOPPED cannot emit a CSR inventory; do not infer
an ACK or PMP correctness for them. Hart IDs need not equal Hart indices.
"""
import argparse
import json
import re
from pathlib import Path

LINE = re.compile(r'^K3_PMP_DIAG hart=(\d+) (.*)$')
FIELD = re.compile(r'([a-zA-Z0-9_]+)=([^ ]+)')


def inspect(log: str, expected: int = 16) -> dict:
    records = {}
    errors = []
    for lineno, line in enumerate(log.splitlines(), 1):
        match = LINE.search(line.strip())
        if not match:
            continue
        hart = int(match.group(1))
        data = dict(FIELD.findall(match.group(2)))
        if not data:
            errors.append(f'line {lineno}: unrecognized diagnostic record')
            continue
        # Last full inventory per Hart can be used after resume. Do not merge
        # different boot/resume snapshots with each other.
        if 'count' in data:
            if hart in records and not records[hart]['end']:
                errors.append(f'line {lineno}: incomplete previous snapshot hart {hart}')
            try:
                count = int(data['count'], 0)
                gran = int(data['gran'], 0)
                smepmp = int(data['smepmp'], 0)
            except (ValueError, KeyError):
                errors.append(f'line {lineno}: invalid header')
                continue
            if count < 1 or count > 64 or gran > 64 or smepmp not in (0, 1):
                errors.append(f'line {lineno}: impossible PMP header')
            records[hart] = {'count': count, 'gran': gran,
                             'smepmp': smepmp, 'entries': set(),
                             'end': False, 'mseccfg': False}
            continue
        item = records.get(hart)
        if item is None:
            errors.append(f'line {lineno}: data before header for hart {hart}')
            continue
        if 'end' in data:
            if data['end'] != '1' or item['end']:
                errors.append(f'line {lineno}: duplicate/invalid end hart {hart}')
            item['end'] = True
        elif 'mseccfg' in data:
            if item['mseccfg'] or item['end']:
                errors.append(f'line {lineno}: duplicate/out-of-order mseccfg')
            item['mseccfg'] = True
        elif 'idx' in data:
            try:
                index = int(data['idx'], 0)
                if 'error' in data or not all(k in data for k in ('prot', 'base', 'log2len')):
                    raise ValueError('CSR read error')
                int(data['prot'], 0)
                int(data['base'], 0)
                int(data['log2len'], 0)
            except ValueError:
                errors.append(f'line {lineno}: invalid CSR entry hart {hart}')
                continue
            if index < 0 or index >= item['count'] or index in item['entries'] or item['end']:
                errors.append(f'line {lineno}: duplicate/unexpected index hart {hart}')
            item['entries'].add(index)
        else:
            errors.append(f'line {lineno}: unknown record')
    good = []
    for hart, item in sorted(records.items()):
        if (not item['end'] or item['entries'] != set(range(item['count'])) or
                (item['smepmp'] and not item['mseccfg'])):
            errors.append(f'incomplete Hart {hart} snapshot: {len(item["entries"])}/{item["count"]} entries')
        else:
            good.append(hart)
    if len(records) != expected:
        errors.append(f'expected {expected} unique Hart snapshots, got {len(records)}')
    return {'inventory_complete': not errors, 'NOT_SECURITY_PROOF': True,
            'expected_harts': expected, 'complete_hartids': good,
            'observed_hartids': sorted(records), 'errors': errors}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('serial_log', type=Path)
    ap.add_argument('--expected-harts', type=int, default=16)
    args = ap.parse_args()
    if args.expected_harts < 1 or args.expected_harts > 16:
        ap.error('expected Hart count must be in 1..16')
    report = inspect(args.serial_log.read_text(errors='replace'), args.expected_harts)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report['inventory_complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
