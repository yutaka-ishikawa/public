#!/usr/bin/env python3
"""Use production functions verbatim; allocation and kernel state are mocked."""
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'common'))
from extract_sources import ROOT, block

out = Path('build')
out.mkdir(exist_ok=True)
selected = {
    'keystone/linux-keystone-driver/keystone-ioctl.c': [
        'int __keystone_destroy_enclave(unsigned int ueid)\n'],
    'keystone/linux-keystone-driver/keystone-page.c': [
        'int epm_init(', 'int epm_destroy('],
    'keystone/linux-keystone-driver/keystone.c': [
        'static int __init keystone_dev_init('],
}
chunks = []
for name, signatures in selected.items():
    source = (ROOT / name).read_text()
    if name.endswith("keystone-page.c"):
        chunks.append(source[source.index("int epm_destroy("):source.index("int utm_destroy(")])
    else:
        for signature in signatures:
            chunks.append(block(source, signature))
header = 'keystone/linux-keystone-driver/keystone.h'
(out / 'types.inc').write_text(
    block((ROOT / header).read_text(), 'struct epm {') + ';\n')
selected[header] = []
(out / 'production.inc').write_text('\n\n'.join(chunks) + '\n')
(out / 'source-manifest.json').write_text(json.dumps({
    name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
    for name in selected
}, indent=2) + '\n')
