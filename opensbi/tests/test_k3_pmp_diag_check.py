import importlib.util
from pathlib import Path
import unittest

p = Path(__file__).resolve().parents[1] / 'scripts/k3_pmp_diag_check.py'
spec = importlib.util.spec_from_file_location('inventory', p)
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)


def sample(h, count=2, smepmp=0):
    lines = [f'K3_PMP_DIAG hart={h} count={count} gran=2 smepmp={smepmp}']
    if smepmp:
        lines.append(f'K3_PMP_DIAG hart={h} mseccfg=0x0')
    for i in range(count):
        lines.append(f'K3_PMP_DIAG hart={h} idx={i} prot=0x0 base=0x0 log2len=0')
    lines.append(f'K3_PMP_DIAG hart={h} end=1')
    return '\n'.join(lines)


class InventoryTest(unittest.TestCase):
    def test_full_16_harts(self):
        report = inventory.inspect('\n'.join(sample(i, smepmp=(i & 1)) for i in range(16)))
        self.assertTrue(report['inventory_complete'])
        self.assertTrue(report['NOT_SECURITY_PROOF'])
    def test_only_boot_hart_not_complete(self):
        self.assertFalse(inventory.inspect(sample(0))['inventory_complete'])
    def test_duplicate_entry_rejected(self):
        log = sample(0).replace('K3_PMP_DIAG hart=0 end=1',
                'K3_PMP_DIAG hart=0 idx=0 prot=0x0 base=0x0 log2len=0\nK3_PMP_DIAG hart=0 end=1')
        self.assertFalse(inventory.inspect(log, 1)['inventory_complete'])
    def test_resume_last_snapshot(self):
        self.assertTrue(inventory.inspect(sample(7) + '\n' + sample(7), 1)['inventory_complete'])
    def test_missing_end(self):
        self.assertFalse(inventory.inspect(sample(2).replace('K3_PMP_DIAG hart=2 end=1',''), 1)['inventory_complete'])
    def test_missing_entry(self):
        self.assertFalse(inventory.inspect(sample(2).replace('K3_PMP_DIAG hart=2 idx=1 prot=0x0 base=0x0 log2len=0',''), 1)['inventory_complete'])
    def test_read_failure(self):
        self.assertFalse(inventory.inspect(sample(2).replace('idx=1 prot=0x0 base=0x0 log2len=0','idx=1 error=-1'), 1)['inventory_complete'])
    def test_mseccfg_unknown(self):
        self.assertFalse(inventory.inspect(sample(1, smepmp=1).replace('K3_PMP_DIAG hart=1 mseccfg=0x0\n',''), 1)['inventory_complete'])

if __name__ == '__main__': unittest.main()
