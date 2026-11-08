#!/usr/bin/env python3
"""Check the actual SDK archive for unintended libc calls in string.c.o."""
import re
import subprocess
import sys


def main():
    if len(sys.argv) != 3:
        raise SystemExit('Usage: check-keystone-eapp-string.py OBJDUMP ARCHIVE_OR_STRING_OBJECT')
    text = subprocess.check_output([sys.argv[1], '-dr', sys.argv[2]], text=True)
    if 'In archive ' in text:
        member = re.search(r'^string\.c\.o:\s+file format[^\n]*\n(.*?)(?=^\S[^\n]*:\s+file format|\Z)',
                           text, re.MULTILINE | re.DOTALL)
        if not member:
            raise SystemExit('Cannot find string.c.o in the SDK archive')
        text = member[1]
    calls = re.findall(r'R_RISCV_(?:CALL_PLT|CALL|JAL)\s+(memset|memcpy|memmove|memcmp|strlen)\b', text)
    if calls:
        raise SystemExit('Unsafe libc call(s) in SDK string primitives: ' + ', '.join(calls))
    if not all(f'<{name}>:' in text for name in ('memset', 'memcpy', 'memmove', 'memcmp', 'strlen')):
        raise SystemExit('Missing SDK string primitive disassembly')
    print('PASS SDK string object: no recursive/libc calls in memory/string primitives')


if __name__ == '__main__':
    main()
