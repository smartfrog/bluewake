"""End-to-end tests for scripts/vita/relset/run.py.

The runner reads an optional REL-event log and the static catalog logs, then
writes baseline.json and summary.txt under --out. A run with no events records
the static picture and says the active set is not measured; a broken stream
fails with exit 1. Outputs go to the ignored build-f14 tree.
"""

from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
RUN = ROOT / "scripts/vita/relset/run.py"
OUT = ROOT / 'build-f14/relset-tests'

SYNTHETIC_EVENTS = """\
[aurora:info] renderer ready
[rel-load] module=2 name=d_a_agbsw0 bytes=25380 us=1000
[rel-load] module=181 name=d_a_acorn_leaf bytes=9164 us=2000
[rel-unload] module=2 us=3000
"""

SYNTHETIC_COMPOSITE = """\
DOL: 206 chunks, 2 ranges
RELs: 415 modules
Composite: 748 chunks, 417 code ranges
PASS: 748 chunks, 415 rel modules, 417 code ranges
"""


def run_cli(*args):
    return subprocess.run([sys.executable, str(RUN), *args],
                          capture_output=True, text=True)


class RunTest(unittest.TestCase):
    def test_writes_a_not_measured_baseline_without_events(self):
        out = OUT / "no-events"
        result = run_cli("--out", str(out), "--label", "no-events",
                         "--source", "0" * 40)
        self.assertEqual(result.returncode, 0, result.stderr)
        baseline = json.loads((out / "baseline.json").read_text())
        self.assertFalse(baseline["measured"])
        self.assertIn("not measured", (out / "summary.txt").read_text())

    def test_writes_a_measured_baseline_from_a_synthetic_log(self):
        out = OUT / "measured"
        with tempfile.TemporaryDirectory() as tmp:
            events = Path(tmp) / "run.log"
            events.write_text(SYNTHETIC_EVENTS)
            result = run_cli("--out", str(out), "--label", "measured",
                             "--source", "1" * 40, "--scene", "boot",
                             "--events", str(events))
        self.assertEqual(result.returncode, 0, result.stderr)
        baseline = json.loads((out / "baseline.json").read_text())
        self.assertTrue(baseline["measured"])
        self.assertEqual(baseline["metrics"]["peak_bytes"], 34544)
        self.assertEqual(baseline["metrics"]["peak_modules"], 2)
        self.assertEqual(baseline["scene"], "boot")

    def test_records_static_evidence_from_the_catalog_logs(self):
        out = OUT / "static"
        with tempfile.TemporaryDirectory() as tmp:
            composite = Path(tmp) / "generate-composite.log"
            composite.write_text(SYNTHETIC_COMPOSITE)
            result = run_cli("--out", str(out), "--label", "static",
                             "--source", "2" * 40,
                             "--composite-log", str(composite))
        self.assertEqual(result.returncode, 0, result.stderr)
        baseline = json.loads((out / "baseline.json").read_text())
        self.assertEqual(baseline["static"]["evidence"]["rel_chunks"], 542)

    def test_refuses_a_broken_stream(self):
        with tempfile.TemporaryDirectory() as tmp:
            events = Path(tmp) / "broken.log"
            events.write_text("[rel-load] module=2 name=d_a_agbsw0 bytes=1 us=one\n")
            result = run_cli("--out", str(OUT / "broken"), "--label", "broken",
                             "--source", "3" * 40, "--events", str(events))
        self.assertEqual(result.returncode, 1)
        self.assertIn("rel-load", result.stderr)


if __name__ == "__main__":
    unittest.main()
