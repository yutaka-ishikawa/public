#!/usr/bin/env python3
"""Test serial-log interpretation only; these are not enclave execution tests."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('rng_matrix', ROOT / 'verifications/random/run-qemu-matrix.py')
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)


class OutputTest(unittest.TestCase):
    def output(self, newline=b'\r\n', backend=b'counter', status=b'0', exitcode=b'0'):
        return newline.join([
            b'[SM/RNG] selected=' + backend + b' quality=development status=' + status,
            b'PASS GUEST SBI 3001: mode=native',
            b'PASS GUEST SBI 3001: AT_RANDOM startup/resume error=0',
            b'RNG_CASE_EXIT=' + exitcode, b''])

    def test_serial_newline_forms(self):
        for newline in (b'\n', b'\r\n', b'\r\r\n'):
            self.assertTrue(matrix.evaluate_output(self.output(newline), 'counter', 0)['pass'])

    def test_guest_failure_is_reported(self):
        result = matrix.evaluate_output(self.output(exitcode=b'1'), 'counter', 0)
        self.assertFalse(result['pass'])
        self.assertIn('status 1', result['error'])

    def test_wrong_firmware_is_reported(self):
        result = matrix.evaluate_output(self.output(backend=b'zkr'), 'counter', 0)
        self.assertFalse(result['pass'])
        self.assertIn('expected counter', result['error'])

    def test_missing_env_success_is_not_pass(self):
        data = self.output().replace(b'PASS GUEST SBI 3001: AT_RANDOM', b'FAIL AT_RANDOM')
        self.assertIn('AT_RANDOM PASS message missing', matrix.evaluate_output(data, 'counter', 0)['error'])

    def test_incomplete_marker_waits(self):
        self.assertIsNone(matrix.evaluate_output(self.output()[:-2], 'counter', 0))


if __name__ == '__main__':
    unittest.main()
