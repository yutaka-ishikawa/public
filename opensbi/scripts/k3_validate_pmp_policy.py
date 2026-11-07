#!/usr/bin/env python3
"""Reject contradictory explicit K3 PMP modes before Kconfig normalizes them."""
import pathlib
import re
import sys

MODES = ('CONFIG_K3_PMP_VENDOR', 'CONFIG_K3_PMP_SMEPMP', 'CONFIG_K3_KEYSTONE_SM')


def validate(path):
    contents = pathlib.Path(path).read_text(encoding='utf-8')
    selected = [key for key in MODES if re.search(r'^\s*' + key + r'\s*=\s*y\s*$', contents, re.M)]
    if len(selected) > 1:
        return False, 'conflicting K3 PMP selections: ' + ', '.join(selected)
    return True, selected[0] if selected else 'vendor default (implicit)'


def main():
    if len(sys.argv) != 2:
        print('usage: k3_validate_pmp_policy.py <defconfig>', file=sys.stderr)
        return 2
    try:
        ok, message = validate(sys.argv[1])
    except (OSError, UnicodeError) as exc:
        print(f'K3 policy validation failed: {exc}', file=sys.stderr)
        return 2
    print(f'K3 policy: {message}' if ok else f'K3 policy ERROR: {message}', file=sys.stderr)
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
