"""Memory-bound planning and acceptance of actual HPL-style results."""
import importlib.util
import math
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('hpl', Path(__file__).parents[1] / 'hpl.py')
hpl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hpl)


class Sizing(unittest.TestCase):
    def test_distribution_matches_explicit_blocks(self):
        for n in [1, 191, 192, 193, 12000, 61440]:
            for ranks in [1, 2, 4, 8]:
                counts = [0] * ranks
                for i in range(n):
                    counts[(i // 192) % ranks] += 1
                self.assertEqual([hpl.numroc(n, 192, r, ranks) for r in range(ranks)], counts)

    def test_maximum_fits_and_next_balanced_size_does_not(self):
        for nb in [192, 256]:
            for p, q in [(2, 4), (1, 8), (4, 4)]:
                for gib in [2, 7, 29, 30]:
                    budget = gib * hpl.GIB
                    n = hpl.maximum_n(budget, nb, p, q)
                    self.assertEqual(n % (nb * math.lcm(p, q)), 0)
                    self.assertLessEqual(hpl.estimate(n, nb, p, q), budget)
                    self.assertGreater(hpl.estimate(n + nb * math.lcm(p, q), nb, p, q), budget)
                    self.assertGreater(hpl.estimate(n, nb, p, q), 8*n*(n+1))
        self.assertEqual(hpl.maximum_n(30*hpl.GIB, 192, 2, 4), 61440)

    def test_invalid_and_duplicate_cpu_lists(self):
        for value in ['0-7,7', '7-0', '0;rm', '', 'a', '0-999999']:
            with self.assertRaises(ValueError):
                hpl.cpu_numbers(value)


class Acceptance(unittest.TestCase):
    def setUp(self):
        self.cfg = {'n': 1536, 'nb': 192, 'p': 1, 'q': 2, 'cpus': [0, 1]}
        self.log = '''HPL_BIND local_rank=0 cpu=0
HPL_BIND local_rank=1 cpu=1
WR11C2R4 1536 192 1 2 0.80 3.0000e+00
||Ax-b||_oo/(eps*(||A||_oo*||x||_oo+||b||_oo)*N)= 3.03925351e-03 ...... PASSED
1 tests completed and passed residual checks,
0 tests completed and failed residual checks,
0 tests skipped because of illegal input values.
'''

    def test_valid_result(self):
        result = hpl.validate_output(self.log, self.cfg)
        self.assertTrue(result['passed'])
        self.assertEqual(result['gflops'], 3.0)

    def test_plausible_gflops_with_bad_residual_is_not_pass(self):
        bad = [self.log.replace('3.03925351e-03', 'nan'),
               self.log.replace('3.03925351e-03', '17.0'),
               self.log.replace('PASSED', 'FAILED'),
               self.log.replace('0 tests completed and failed', '10 tests completed and failed'),
               self.log.replace('0 tests skipped', '1 tests skipped'),
               self.log.replace('1536 192 1 2', '1536 256 1 2'),
               self.log.replace('HPL_BIND local_rank=1 cpu=1\n', ''),
               self.log + 'WR11C2R4 1536 192 1 2 0.80 3.0\n']
        for text in bad:
            with self.assertRaises(RuntimeError):
                hpl.validate_output(text, self.cfg)


if __name__ == '__main__':
    unittest.main()
