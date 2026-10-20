"""End-to-end tests for scripts/vita/perf/run.py: the measurement runner.

The runner turns one run's evidence into a baseline document. A synthetic
frame-event log must fill the metrics; a set of boot reports must record that
no frame was presented; a malformed stream must fail loudly before any
document is written.
"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
RUN = ROOT / "scripts/vita/perf/run.py"
OUT = ROOT / 'build-f12/perf-tests'
SOURCE = '0' * 40

PASS_REPORT = f'''WW05 {'a' * 32} {'b' * 40} {'c' * 64} method=startup-gx-v1 phase=guest platform=VITA
ABI pointer=4 cpu=3496 version=6
ALLOC host=800000 mem1=25165824 vm=33554432 cache=16384 aliases=12 alias_count=2 owned_live=59536652
UNKNOWN residency native_stack_highwater pool_peak GPU
STATE entry=80003140 pc=80001008 sp=817FFF00 edges=2 exception=00000000 cycles=120
GX_FRONTIER complete=1 opcode=61 bytes=5 pc=80001000 completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 hex=61FEFFFFFF
STOP reason=first complete GX command; no renderer address=00000000 pc=00000000
END frontier=PASS live=0 rendered=0
'''


class RunTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)

    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(RUN), *args],
                              capture_output=True, text=True, timeout=60)

    def test_synthetic_event_log_fills_the_baseline(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            log = root / 'run.log'
            log.write_text("\n".join(f"[frame-present] frame={index} us={index * 16667}"
                                     for index in range(121)) + "\n")
            out = root / 'evidence'
            result = self.run_cli('--out', str(out), '--label', 'synthetic',
                                  '--events', str(log), '--window', '1', '--source', SOURCE)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            baseline = json.loads((out / 'baseline.json').read_text())
            self.assertTrue(baseline['measured'])
            self.assertAlmostEqual(baseline['metrics']['mean_fps'], 60.0, delta=0.1)
            self.assertIn('meets the 60 fps target', (out / 'summary.txt').read_text())

    def test_boot_reports_only_record_no_frames(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            report = root / 'ww05.report'
            report.write_text(PASS_REPORT)
            out = root / 'evidence'
            result = self.run_cli('--out', str(out), '--label', 'boot-frontier',
                                  '--ww05-report', str(report), '--source', SOURCE)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            baseline = json.loads((out / 'baseline.json').read_text())
            self.assertFalse(baseline['measured'])
            self.assertEqual(baseline['boot_evidence']['runs'], 1)
            self.assertIn('none presented', (out / 'summary.txt').read_text())

    def test_malformed_event_stream_fails_loudly(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            log = root / 'run.log'
            log.write_text("[frame-present] frame=one us=1000\n")
            out = root / 'evidence'
            result = self.run_cli('--out', str(out), '--label', 'broken',
                                  '--events', str(log), '--source', SOURCE)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('[frame-present]', result.stderr)
            self.assertFalse((out / 'baseline.json').exists())

    def test_a_stalled_stream_with_an_observation_end_is_measured_not_refused(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            log = root / 'run.log'
            log.write_text("\n".join(f"[frame-present] frame={index} us={index * 60833}"
                                     for index in range(7)) + "\n")
            out = root / 'evidence'
            result = self.run_cli('--out', str(out), '--label', 'stalled',
                                  '--events', str(log), '--window', '1',
                                  '--end-us', '8052000', '--source', SOURCE)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            baseline = json.loads((out / 'baseline.json').read_text())
            self.assertTrue(baseline['measured'])
            self.assertEqual(baseline['observation_end_us'], 8052000)
            self.assertEqual(baseline['metrics']['min_window_fps'], 0.0)
            self.assertIn('below the 30 fps floor', (out / 'summary.txt').read_text())


if __name__ == '__main__':
    unittest.main()
