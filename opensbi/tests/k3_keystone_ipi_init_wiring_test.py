#!/usr/bin/env python3
"""Static wiring regression: does not claim real-Hart PMP or IPI completion."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
init = (ROOT / 'lib/sbi/sbi_init.c').read_text()
objects = (ROOT / 'platform/generic/objects.mk').read_text()
platform = (ROOT / 'platform/generic/spacemit/spacemit_k3.c').read_text()
sm = (ROOT.parent / 'keystone/sm/src/ipi.c').read_text()
transport = (ROOT.parent / 'keystone/sm/src/k3_pmp_ipi_transport.c').read_text()
checks = {
    'Cold boot registers after OpenSBI IPI init': bool(re.search(
        r'rc = sbi_ipi_init\(scratch, true\);.*?'
        r'#ifdef CONFIG_K3_KEYSTONE_SM(?:(?!#endif).)*?rc = k3_pmp_sync_initialize\(\);'
        r'.*?#endif', init, flags=re.S)),
    'Single cold registration in OpenSBI': init.count('rc = k3_pmp_sync_initialize();') == 1,
    'Warm IPI init does not reregister': 'k3_pmp_sync_initialize' not in
       init[init.index('static void __noreturn init_warm_startup'):],
    'Existing event binds verified local PMP apply': bool(re.search(
        r'k3_pmp_ipi_transport_bind_apply\(k3_pmp_apply_verified\)', sm)),
    'Transport uses ordinary returning process handler': bool(re.search(
        r'\.process\s*=\s*k3_pmp_ipi_event_process', transport)),
    'Transport checks real mailbox completion': 'k3_pmp_mailbox_check(' in transport,
    'Diagnostic opt-in requires SM linker input': 'KEYSTONE_K3_SM_LINK_PROBE=<SM link-only object>' in objects,
    'Diagnostic is not claimed boot-ready':
        '#if defined(CONFIG_K3_KEYSTONE_SM) && !defined(K3_KEYSTONE_DIAGNOSTIC_LINK)' in platform
        and 'Runtime S-mode admission' in platform,
    'Cold pre-S-mode requires verified layout and SM readiness':
        'k3_pmp_boot_partition_admit(' in init
        and 'sm_init unavailable' in init and 'sbi_hart_hang();' in init,
}
for name, ok in checks.items():
    print(f'{"PASS" if ok else "FAIL"} {name}')
assert all(checks.values()), 'K3 integration wiring failed static regression'
print(f'RESULT: {len(checks)}/{len(checks)} PASS (source-level only)')
