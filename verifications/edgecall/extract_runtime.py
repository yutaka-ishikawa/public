#!/usr/bin/env python3
"""Keep the OCALL copy/stop/return path identical to production."""
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verifications/common'))
from extract_sources import block

output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=True)
path = ROOT / 'keystone/runtime/call/syscall.c'
source = path.read_text()
signature = 'uintptr_t dispatch_edgecall_ocall('
line = source[:source.index(signature)].count('\n') + 1
(output / 'ocall.inc').write_text(
    f'#line {line} "keystone/runtime/call/syscall.c"\n' + block(source, signature) + '\n')
paths = [path, ROOT / 'keystone/sdk/src/edge/edge_call.c',
         ROOT / 'keystone/sdk/src/edge/edge_dispatch.c',
         ROOT / 'keystone/sdk/include/edge/edge_call.h',
         ROOT / 'keystone/sdk/include/edge/edge_common.h']
(output / 'edge-source-manifest.json').write_text(json.dumps({
    str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths
}, indent=2) + '\n')
