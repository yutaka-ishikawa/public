#!/usr/bin/env python3
"""Fail if Keystone includes Smepmp-only configuration helpers."""
import pathlib, subprocess, sys, re
root = pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else pathlib.Path(__file__).resolve().parent.parent
source = (root / 'lib/sbi/sbi_hart.c').read_text()
start = source.find('static unsigned int sbi_hart_get_smepmp_flags')
end = source.find('static int sbi_hart_oldpmp_configure', start)
if start < 0 or end < 0:
    sys.exit('FAIL: expected source boundaries missing')
segment = source[source.rfind('#ifndef CONFIG_K3_KEYSTONE_SM', 0, start):end]
if not segment.startswith('#ifndef CONFIG_K3_KEYSTONE_SM') or '#endif /* !CONFIG_K3_KEYSTONE_SM */' not in segment:
    sys.exit('FAIL: Smepmp helpers not guarded for Keystone')
# Preprocess only to verify that guards eliminate all definitions, without target headers.
filtered = []
for line in source.splitlines():
    if re.match(r'^static (?:unsigned int|void|int) sbi_hart_(?:get_smepmp_flags|smepmp_set|smepmp_configure)\(', line):
        filtered.append(line)
assert len(filtered) == 3, filtered
print('PASS: three vendor Smepmp helpers excluded from Keystone via compile-time guard')
