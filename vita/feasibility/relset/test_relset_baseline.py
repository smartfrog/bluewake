"""Tests for scripts/vita/relset/relset_baseline.py.

A baseline says what was measured, over which scene, at which build source,
and what the static REL catalog is known to hold today. A run with no REL
events says so instead of carrying numbers it does not have; a measured run's
verdict compares its peak active set against the ticket's combined ceiling.
"""

from pathlib import Path
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/relset"))

from rel_events import RelLoad, RelUnload  # noqa: E402
from relset_baseline import (  # noqa: E402
    ACTIVE_SET_CEILING_MIB,
    KNOWN_PICTURE,
    build_baseline,
    render_summary,
    write_baseline,
)

MIB = 1024 * 1024


def build(events=None, static=None):
    return build_baseline(label="test-run", source="0" * 40, scene="boot",
                          events=events, static=static)


class BaselineTest(unittest.TestCase):
    def test_no_events_is_not_measured(self):
        baseline = build(events=None)
        self.assertFalse(baseline["measured"])
        self.assertIsNone(baseline["metrics"])
        self.assertIn("not measured", baseline["verdict"])

    def test_measured_run_reports_the_peak_active_set(self):
        baseline = build(events=[
            RelLoad(module_id=2, name="d_a_agbsw0", bytes=60 * MIB, us=10),
            RelLoad(module_id=3, name="d_a_andsw0", bytes=40 * MIB, us=20),
            RelUnload(module_id=2, us=30),
        ])
        self.assertTrue(baseline["measured"])
        self.assertEqual(baseline["metrics"]["peak_bytes"], 100 * MIB)
        self.assertEqual(baseline["metrics"]["peak_mib"], 100.0)
        self.assertEqual(baseline["metrics"]["peak_modules"], 2)
        self.assertIn("under", baseline["verdict"])

    def test_a_peak_at_or_above_the_ceiling_is_flagged(self):
        baseline = build(events=[
            RelLoad(module_id=2, name="d_a_agbsw0",
                    bytes=(ACTIVE_SET_CEILING_MIB + 1) * MIB, us=10),
        ])
        self.assertIn("ceiling", baseline["verdict"])
        self.assertNotIn("under", baseline["verdict"])

    def test_the_static_known_picture_is_recorded(self):
        baseline = build(events=None)
        self.assertEqual(baseline["static"]["known_picture"], KNOWN_PICTURE)
        self.assertEqual(KNOWN_PICTURE["modules"], 415)
        self.assertEqual(KNOWN_PICTURE["rel_chunks"], 542)
        self.assertEqual(KNOWN_PICTURE["rel_code_mib"], 183.1)

    def test_static_evidence_is_kept_when_given(self):
        static = {"dol_chunks": 206, "rel_modules": 415, "rel_chunks": 542,
                  "composite_chunks": 748, "code_ranges": 417, "module_ids": [1, 2]}
        baseline = build(events=None, static=static)
        self.assertEqual(baseline["static"]["evidence"], static)

    def test_summary_renders_the_not_measured_verdict_and_static_picture(self):
        summary = render_summary(build(events=None))
        self.assertIn("not measured", summary)
        self.assertIn("known picture", summary)
        self.assertIn("415", summary)
        self.assertIn("542", summary)

    def test_summary_renders_the_measured_peak_and_top_modules(self):
        summary = render_summary(build(events=[
            RelLoad(module_id=3, name="d_a_andsw0", bytes=400, us=10),
            RelLoad(module_id=2, name="d_a_agbsw0", bytes=100, us=20),
        ]))
        self.assertIn("peak active", summary)
        self.assertIn("d_a_andsw0", summary)

    def test_write_baseline_writes_json_and_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            json_path, summary_path = write_baseline(Path(tmp), build(events=None))
            baseline = json.loads(json_path.read_text())
            self.assertFalse(baseline["measured"])
            self.assertEqual(baseline["ticket"], "VITA-WW-14")
            self.assertIn("not measured", summary_path.read_text())


if __name__ == "__main__":
    unittest.main()
