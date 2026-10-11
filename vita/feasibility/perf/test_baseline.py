"""Tests for scripts/vita/perf/baseline.py: the baseline document format.

A baseline is one JSON document plus a human summary. A run with no presented
frames must say so plainly instead of carrying numbers it does not have; a run
with frames carries the windowed metrics and a verdict against the 60 fps
target and the 30 fps floor.
"""

import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/perf"))

from baseline import build_baseline, render_summary, write_baseline  # noqa: E402
from frame_events import parse_frame_events  # noqa: E402

OUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f12/perf-tests')


def stream_log(*stamps):
    return "\n".join(f"[frame-present] frame={index} us={us}"
                     for index, us in enumerate(stamps, start=1)) + "\n"


def events(*stamps):
    return parse_frame_events(stream_log(*stamps))


def sixty_fps(frames):
    return [index * 16667 for index in range(frames)]


class BaselineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)

    def test_unfilled_baseline_says_not_measured(self):
        baseline = build_baseline(label='boot-frontier', source='0' * 40, window_s=1.0)
        self.assertFalse(baseline['measured'])
        self.assertIsNone(baseline['metrics'])
        self.assertIn('not measured', baseline['verdict'])

    def test_measured_baseline_reports_the_target_verdict(self):
        baseline = build_baseline(label='title', source='0' * 40, window_s=1.0,
                                  events=events(*sixty_fps(121)))
        self.assertTrue(baseline['measured'])
        self.assertAlmostEqual(baseline['metrics']['mean_fps'], 60.0, delta=0.1)
        self.assertEqual(baseline['verdict'], 'meets the 60 fps target')

    def test_slow_baseline_reports_below_the_floor(self):
        slow = [index * 50000 for index in range(61)]
        baseline = build_baseline(label='title', source='0' * 40, window_s=1.0,
                                  events=events(*slow))
        self.assertEqual(baseline['verdict'], 'below the 30 fps floor')

    def test_retrace_stamps_become_the_secondary_metric(self):
        log = "\n".join(f"[frame-timing] retrace={index} us={index * 16667}"
                        for index in range(1, 122)) + "\n"
        baseline = build_baseline(label='title', source='0' * 40, window_s=1.0,
                                  events=parse_frame_events(log))
        self.assertIsNone(baseline['metrics'])
        self.assertAlmostEqual(baseline['retrace_metrics']['mean_fps'], 60.0, delta=0.1)

    def test_writes_json_and_summary(self):
        baseline = build_baseline(label='title', source='0' * 40, window_s=1.0,
                                  events=events(*sixty_fps(121)))
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            json_path, summary_path = write_baseline(Path(directory), baseline)
            stored = json.loads(json_path.read_text())
            self.assertEqual(stored, baseline)
            summary = summary_path.read_text()
            self.assertTrue(summary.endswith('\n'))
            self.assertIn('VITA-WW-12 baseline: title', summary)
            self.assertIn('mean fps', summary)
            self.assertIn('verdict', summary)

    def test_summary_carries_the_caveats(self):
        baseline = build_baseline(label='title', source='0' * 40, window_s=1.0)
        summary = render_summary(baseline)
        self.assertIn('Vita3K timing is not physical Vita timing', summary)


if __name__ == '__main__':
    unittest.main()
