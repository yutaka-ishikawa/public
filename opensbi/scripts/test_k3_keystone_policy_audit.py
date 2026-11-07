#!/usr/bin/env python3
import tempfile
import shutil
from pathlib import Path
from k3_keystone_policy_audit import audit

root=Path(__file__).resolve().parents[1]
assert not audit(root)[1], 'clean source audit failed'
with tempfile.TemporaryDirectory() as td:
    dst=Path(td)
    for item in ('lib/sbi/sbi_hart.c','lib/sbi/sbi_init.c','platform/generic/Kconfig','platform/generic/spacemit/spacemit_k3.c'):
        (dst/item).parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(root/item,dst/item)
    f=dst/'lib/sbi/sbi_hart.c'
    original=f.read_text()
    # Reintroduce the exact past mistake in Keystone branch.
    target='\trc = k3_keystone_root_pmp_configure(scratch, pmp_count,'
    assert target in original
    f.write_text(original.replace(target,'\tif (sbi_hart_has_extension(scratch, SBI_HART_EXT_SMEPMP))\n\t\treturn SBI_ENOTSUPP;\n'+target,1))
    assert any('capability-based rejection' in n for n in audit(dst)[1])
    f.write_text(original.replace('static void sbi_hart_smepmp_set(', 'static int sbi_hart_smepmp_set(',1))
    assert any('setter remains void' in n for n in audit(dst)[1])
print('PASS clean baseline + two mutation regression tests')
