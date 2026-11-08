#!/usr/bin/env python3
"""Check the built native crypto archive, never execute a substitute for the SM."""
import re
import subprocess
import sys

if len(sys.argv) != 3:
    raise SystemExit('Usage: check-keystone-native-crypto.py NM ARCHIVE')
text = subprocess.check_output([sys.argv[1], '-u', sys.argv[2]], text=True)
forbidden = re.findall(r'\b(?:__stack_chk_\w+|__cxa_\w+|_Unwind_\w+)\b', text)
if forbidden:
    raise SystemExit('Hosted runtime dependencies in native crypto: ' + ', '.join(sorted(set(forbidden))))
print('PASS native crypto archive: no stack-canary/C++ exception runtime dependencies')
