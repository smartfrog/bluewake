"""Host contract tests for the Vita disc I/O seam (VITA-WW-13).

Builds vita/host/dvd in tests-only mode and runs the C fake-backend cases.
No real file, no pinned runtime and no game data are involved.
"""

import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts/vita'))
from vita_workspace import CMAKE, NINJA  # noqa: E402
OUT = ROOT / 'build-f13/ww13-seam/dvd-io-tests'

CASES = (
    'open-close',
    'read-sequential',
    'short-read-eof',
    'read-past-eof',
    'read-failure',
    'seek-set-cur-end',
    'seek-invalid',
    'chunk-bound',
    'open-failure',
)


class DvdSeamTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CC', 'cc')
        subprocess.run(
            [str(CMAKE), '-S', str(ROOT / 'vita/host/dvd'), '-B', str(OUT),
             '-G', 'Ninja', f'-DCMAKE_C_COMPILER={compiler}',
             f'-DCMAKE_MAKE_PROGRAM={NINJA}', '-DWW13_TESTS_ONLY=ON'],
            check=True, capture_output=True)
        subprocess.run([str(CMAKE), '--build', str(OUT), '-j', '2'],
                       check=True, capture_output=True)

    def run_case(self, name):
        return subprocess.run([str(OUT / 'dvd_io_test'), name],
                              capture_output=True, text=True, timeout=30)

    def test_every_case_passes(self):
        for name in CASES:
            with self.subTest(case=name):
                result = self.run_case(name)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f'PASS {name}', result.stdout)

    def test_all_case_selector_passes(self):
        result = self.run_case('all')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for name in CASES:
            self.assertIn(f'PASS {name}', result.stdout)

    def test_read_failure_is_loud(self):
        result = self.run_case('read-failure')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('[bw-dvd] read failed', result.stderr)

    def test_open_failure_is_loud(self):
        result = self.run_case('open-failure')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('[bw-dvd] open failed', result.stderr)

    def test_write_mode_rejection_is_loud(self):
        result = self.run_case('open-close')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('[bw-dvd] unsupported open mode', result.stderr)

    def test_unknown_case_is_rejected(self):
        result = self.run_case('no-such-case')
        self.assertEqual(result.returncode, 2)
        self.assertIn('unknown case', result.stderr)


if __name__ == '__main__':
    unittest.main()
