#!/usr/bin/env python3
"""Policy regression gate for K3 Keystone; source-level guarantees only."""
import argparse
from pathlib import Path
import re
import sys


def function_body(src, function):
    m = re.search(r'\b(?:static\s+)?(?:int|void|bool)\s+' + re.escape(function) + r'\s*\([^;]*?\)\s*\{', src, re.S)
    if not m:
        raise ValueError('missing function '+function)
    start=m.end(); depth=1
    for j in range(start,len(src)):
        if src[j]=='{': depth+=1
        if src[j]=='}': depth-=1
        if depth==0: return src[start:j]
    raise ValueError('unterminated function '+function)


def audit(root):
    failures=[]; results=[]
    def check(label, ok):
        results.append((label,ok))
        if not ok: failures.append(label)
    hart=(root/'lib/sbi/sbi_hart.c').read_text()
    kconfig=(root/'platform/generic/Kconfig').read_text()
    pmp=function_body(hart,'sbi_hart_pmp_configure')
    kbranch=re.search(r'#ifdef CONFIG_K3_KEYSTONE_SM(.*?)#else(.*?)#endif',pmp,re.S)
    check('Keystone PMP configure branch exists',kbranch is not None)
    if kbranch:
        ks=kbranch.group(1)
        check('Keystone uses deterministic boot slot layout', 'k3_keystone_root_pmp_configure(' in ks and 'k3_boot_plan_slots' in hart)
        check('No Smepmp capability-based rejection in Keystone PMP init', not re.search(r'SBI_HART_EXT_SMEPMP|SBI_ENOTSUPP',ks))
        check('No Smepmp initializer in Keystone PMP init', 'sbi_hart_smepmp_configure(' not in ks)
        check('Vendor Smepmp path retained', 'sbi_hart_smepmp_configure(' in kbranch.group(2))
    check('Kconfig explicit PMP mode choice', bool(re.search(r'choice\s+prompt "K3 PMP policy selection".*?config K3_PMP_VENDOR.*?config K3_PMP_SMEPMP.*?config K3_KEYSTONE_SM.*?endchoice',kconfig,re.S)))
    init=(root/'lib/sbi/sbi_init.c').read_text()
    check('Keystone runtime admission remains fail-closed',
          'k3_keystone_a1_post_pmp_before_smode' in init and
          'k3_pmp_boot_partition_admit(' in init and
          'sm_init unavailable' in init and
          'sbi_hart_hang();' in init)
    check('Smepmp setter remains void', bool(re.search(r'static void sbi_hart_smepmp_set\(',hart)))
    check('Keystone buffer access helpers preserved', all('sbi_hart_'+n+'_saddr' in hart for n in ('map','unmap')))
    map_body=function_body(hart,'sbi_hart_map_saddr')
    unmap_body=function_body(hart,'sbi_hart_unmap_saddr')
    check('Legacy Smepmp capability-based helper removed', 'k3_keystone_sbi_buffer_access_permitted' not in hart)
    check('Keystone map has explicit no-mapping branch', bool(re.search(r'#ifdef CONFIG_K3_KEYSTONE_SM.*?return SBI_OK;.*?#else',map_body,re.S)))
    check('Keystone unmap has explicit no-mapping branch', bool(re.search(r'#else.*?return SBI_OK;.*?#endif',unmap_body,re.S)))
    check('Keystone buffer map not keyed to hardware extension', 'SBI_HART_EXT_SMEPMP' not in re.search(r'#ifdef CONFIG_K3_KEYSTONE_SM(.*?)#else',map_body,re.S).group(1))
    check('Keystone buffer unmap not keyed to hardware extension', 'SBI_HART_EXT_SMEPMP' not in re.search(r'#else(.*?)#endif',unmap_body,re.S).group(1))
    return results,failures

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('root',type=Path);args=parser.parse_args()
    try: results,failures=audit(args.root)
    except (OSError,ValueError) as e: print('AUDIT ERROR:',e);sys.exit(2)
    for name,ok in results:print(('PASS' if ok else 'FAIL')+' '+name)
    print(f'TOTAL {len(results)} tests, {len(failures)} failure(s)')
    sys.exit(1 if failures else 0)
