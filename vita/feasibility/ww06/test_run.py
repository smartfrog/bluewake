"""End-to-end tests for scripts/vita/ww06/run.py: the acceptance runner.

The runner turns session logs into an acceptance baseline document. Synthetic
marker logs must fill the requirement verdicts; a log with no markers must
record that the scenario is not measured; a malformed stream must fail loudly
before any document is written.
"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
RUN = ROOT / "scripts/vita/ww06/run.py"
OUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f6/ww06-tests')
SOURCE = '0' * 40


def save_session_log():
    lines = [
        "[scene-load] name=Outset us=100",
        "[audio-active] us=200 rate=48000",
    ]
    for index in range(60):
        lines.append(f"[input-frame] retrace={100 + index} channel=0 buttons=0x0000 "
                     f"stick=0,100 sub=0,0 us={1000 + index * 16000}")
        if index == 30:
            lines.append(f"[input-consumed] retrace=130 hold=0x0000 trig=0x0000 "
                         f"axes=0x00648000 us={1000 + 30 * 16000 + 8}")
    for index in range(60):
        lines.append(f"[input-frame] retrace={200 + index} channel=0 buttons=0x0000 "
                     f"stick=0,0 sub=100,0 us={2000000 + index * 16000}")
        if index == 30:
            lines.append(f"[input-consumed] retrace=230 hold=0x0000 trig=0x0000 "
                         f"axes=0x80640000 us={2000000 + 30 * 16000 + 8}")
    lines.append("[scene-load] name=Ojhous us=4000000")
    lines.append("[save-write] bytes=8192 us=5000000")
    return "\n".join(lines) + "\n"


def reload_session_log():
    return ("[save-read] bytes=8192 us=100\n"
            "[scene-load] name=Outset us=200\n")


class RunTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)

    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(RUN), *args],
                              capture_output=True, text=True, timeout=60)

    def test_two_synthetic_sessions_meet_the_scenario(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            save = root / 'save.log'
            save.write_text(save_session_log())
            reload = root / 'reload.log'
            reload.write_text(reload_session_log())
            out = root / 'evidence'
            result = self.run_cli('--out', str(out), '--label', 'synthetic',
                                  '--run', str(save), '--run', str(reload),
                                  '--source', SOURCE)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            baseline = json.loads((out / 'baseline.json').read_text())
            self.assertTrue(baseline['measured'])
            self.assertEqual(baseline['verdict'], 'acceptance met')
            for requirement in baseline['requirements'].values():
                self.assertTrue(requirement['observed'], requirement['detail'])
            self.assertIn('acceptance met', (out / 'summary.txt').read_text())

    def test_a_log_without_markers_is_not_measured(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            log = root / 'empty.log'
            log.write_text("[boot] entry=0x80003140\n[edge-progress] edges=1 cycles=2\n")
            out = root / 'evidence'
            result = self.run_cli('--out', str(out), '--label', 'empty',
                                  '--run', str(log), '--source', SOURCE)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            baseline = json.loads((out / 'baseline.json').read_text())
            self.assertFalse(baseline['measured'])
            self.assertEqual(baseline['verdict'], 'not measured')
            self.assertTrue(baseline['sessions'][0]['empty'])
            self.assertIn('not measured', (out / 'summary.txt').read_text())

    def test_a_malformed_stream_fails_before_any_document(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            log = root / 'broken.log'
            log.write_text("[input-frame] retrace=7 channel=0 buttons=0x0000 us=42\n")
            out = root / 'evidence'
            result = self.run_cli('--out', str(out), '--label', 'broken',
                                  '--run', str(log), '--source', SOURCE)
            self.assertEqual(result.returncode, 1)
            self.assertIn('malformed', result.stderr)
            self.assertFalse((out / 'baseline.json').exists())

    def test_the_baseline_records_the_source_and_sessions(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            save = root / 'save.log'
            save.write_text(save_session_log())
            out = root / 'evidence'
            result = self.run_cli('--out', str(out), '--label', 'one-session',
                                  '--run', str(save), '--source', SOURCE,
                                  '--scene', 'outset')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            baseline = json.loads((out / 'baseline.json').read_text())
            self.assertEqual(baseline['ticket'], 'VITA-WW-06')
            self.assertEqual(baseline['kind'], 'acceptance-baseline')
            self.assertEqual(baseline['source'], SOURCE)
            self.assertEqual(baseline['scene'], 'outset')
            self.assertEqual(len(baseline['sessions']), 1)
            self.assertEqual(baseline['sessions'][0]['input_frames'], 120)
            self.assertIsNone(baseline['requirements']['save_reload']['observed'])
            self.assertEqual(baseline['verdict'], 'not measured')


if __name__ == "__main__":
    unittest.main()
