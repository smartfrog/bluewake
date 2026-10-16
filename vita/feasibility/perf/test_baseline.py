"""Tests for scripts/vita/perf/baseline.py: the baseline document format.

A baseline is one JSON document plus a human summary. A run with no presented
frames must say so plainly instead of carrying numbers it does not have; a run
with frames carries the windowed metrics and a verdict against the 60 fps
target and the 30 fps floor.
"""

import json
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/perf"))

from baseline import build_baseline, fps_verdict, render_summary, write_baseline  # noqa: E402
from frame_events import parse_frame_events  # noqa: E402

OUT = ROOT / 'build-f12/perf-tests'


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

    def test_a_slow_window_cannot_hide_behind_the_median(self):
        for median, minimum, verdict in [
                (30, 10, 'below the 30 fps floor'),
                (60, 30, 'meets the 30 fps floor, below the 60 fps target')]:
            with self.subTest(median=median, minimum=minimum):
                self.assertEqual(fps_verdict({'median_window_fps': median,
                                              'min_window_fps': minimum}, 60, 30), verdict)

    def test_five_thirty_fps_windows_cannot_hide_one_ten_fps_window(self):
        stamps = [window * 10000000 + frame * 1000000 // rate
                  for window, rate in enumerate([30, 30, 30, 30, 30, 10])
                  for frame in range(rate * 10)]
        baseline = build_baseline(label='slow tail', source='a' * 40, window_s=10,
                                  events=events(*stamps), end_us=60000000)
        self.assertEqual(baseline['metrics']['median_window_fps'], 30)
        self.assertEqual(baseline['metrics']['min_window_fps'], 10)
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


class GameplayAcceptanceTest(unittest.TestCase):
    def passages(self):
        return [build_baseline(
            label='passage', source='a' * 40, window_s=10,
            events=events(*[index * 1000000 // 30 for index in range(1800)]),
            end_us=60000000, device={'name': 'test host', 'kind': 'emulator'},
            scene={'phase': 'gameplay', 'passage_id': str(index),
                   'context': 'synthetic controlled traversal',
                   'controlled': True, 'visually_correct': True,
                   'control_evidence': 'synthetic input observation reference',
                   'visual_evidence': 'synthetic visual observation reference',
                   'observation_evidence': 'synthetic supervisor log reference'})
                for index in range(3)]

    def acceptance(self, passages):
        from baseline import build_gameplay_acceptance
        return build_gameplay_acceptance(passages)

    def test_three_complete_passages_pass(self):
        report = self.acceptance(self.passages())
        self.assertEqual(report['status'], 'pass')
        self.assertEqual([p['fps_status'] for p in report['passages']], ['pass'] * 3)

    def test_missing_or_extra_passages_are_incomplete(self):
        for count in [0, 1, 2, 4]:
            with self.subTest(count=count):
                passages = self.passages()
                self.assertEqual(self.acceptance((passages + passages)[:count])['status'],
                                 'incomplete')

    def test_duplicate_passages_are_incomplete(self):
        passages = self.passages()
        passages[1]['scene']['passage_id'] = passages[0]['scene']['passage_id']
        self.assertEqual(self.acceptance(passages)['status'], 'incomplete')

    def test_missing_context_or_identity_is_incomplete(self):
        for section, key in [('scene', 'passage_id'), ('scene', 'context'),
                             ('scene', 'phase'), ('scene', 'control_evidence'),
                             ('scene', 'observation_evidence'), ('scene', 'visual_evidence'),
                             ('scene', 'controlled'), ('scene', 'visually_correct'),
                             ('device', 'name'), ('device', 'kind')]:
            with self.subTest(section=section, key=key):
                passages = self.passages()
                del passages[0][section][key]
                self.assertEqual(self.acceptance(passages)['status'], 'incomplete')

    def test_boot_and_old_baselines_are_not_gameplay_acceptance(self):
        for field, value in [('scene', None), ('source', ''), ('device', None),
                             ('observation_start_us', None), ('observation_end_us', None),
                             ('metrics', None), ('window_seconds', 1)]:
            with self.subTest(field=field):
                passages = self.passages()
                passages[0][field] = value
                self.assertEqual(self.acceptance(passages)['status'], 'incomplete')

    def test_invalid_or_missing_window_metrics_are_incomplete(self):
        for key, value in [('windows', 5), ('windows', None), ('window_seconds', 1),
                           ('median_window_fps', float('nan')),
                           ('min_window_fps', float('inf')),
                           ('min_window_fps', None), ('span_seconds', 60)]:
            with self.subTest(key=key, value=value):
                passages = self.passages()
                passages[0]['metrics'][key] = value
                self.assertEqual(self.acceptance(passages)['status'], 'incomplete')

    def test_non_object_evidence_sections_are_incomplete(self):
        for section in ['scene', 'device', 'metrics']:
            for value in ['malformed', ['malformed'], 1, True]:
                with self.subTest(section=section, value=value):
                    passages = self.passages()
                    passages[0][section] = value
                    report = self.acceptance(passages)
                    self.assertEqual(report['status'], 'incomplete')
                    self.assertIn(f'{section} must be an object', report['passages'][0]['missing'])

    def test_short_observation_is_incomplete(self):
        passages = self.passages()
        passages[0] = build_baseline(
            label='short', source=passages[0]['source'], window_s=10,
            events=events(*[index * 1000000 // 30 for index in range(1770)]),
            end_us=59000000, device=passages[0]['device'], scene=passages[0]['scene'])
        self.assertEqual(self.acceptance(passages)['status'], 'incomplete')

    def test_a_stalled_tail_fails_despite_a_passing_median(self):
        passages = self.passages()
        passages[0] = build_baseline(
            label='stalled', source=passages[0]['source'], window_s=10,
            events=events(*[index * 1000000 // 30 for index in range(1500)]),
            end_us=60000000, device=passages[0]['device'], scene=passages[0]['scene'])
        self.assertEqual(passages[0]['metrics']['median_window_fps'], 30)
        self.assertEqual(passages[0]['metrics']['min_window_fps'], 0)
        self.assertEqual(self.acceptance(passages)['status'], 'fail')

    def test_a_partial_stalled_tail_is_incomplete(self):
        passages = self.passages()
        passages[0] = build_baseline(
            label='partial stalled tail', source=passages[0]['source'], window_s=10,
            events=events(*[index * 1000000 // 30 for index in range(1800)]),
            end_us=65000000, device=passages[0]['device'], scene=passages[0]['scene'])
        self.assertEqual(passages[0]['metrics']['windows'], 6)
        self.assertEqual(passages[0]['metrics']['min_window_fps'], 30)
        self.assertEqual(self.acceptance(passages)['status'], 'incomplete')

    def test_healthy_partial_window_observation_is_also_incomplete(self):
        passages = self.passages()
        passages[0] = build_baseline(
            label='healthy partial tail', source=passages[0]['source'], window_s=10,
            events=events(*[index * 1000000 // 30 for index in range(1950)]),
            end_us=65000000, device=passages[0]['device'], scene=passages[0]['scene'])
        self.assertEqual(passages[0]['verdict'], 'meets the 30 fps floor, below the 60 fps target')
        self.assertEqual(self.acceptance(passages)['status'], 'incomplete')

    def test_failed_control_or_visual_observation_is_not_an_fps_failure(self):
        for key in ['controlled', 'visually_correct']:
            with self.subTest(key=key):
                passages = self.passages()
                passages[0]['scene'][key] = False
                report = self.acceptance(passages)
                self.assertEqual(report['status'], 'fail')
                self.assertEqual(report['passages'][0]['fps_status'], 'pass')

    def test_different_sources_or_devices_cannot_be_combined(self):
        for field, value in [('source', 'b' * 40),
                             ('device', {'name': 'another host', 'kind': 'emulator'})]:
            with self.subTest(field=field):
                passages = self.passages()
                passages[0][field] = value
                self.assertEqual(self.acceptance(passages)['status'], 'incomplete')

    def test_report_preserves_the_evidence_without_mutating_it(self):
        passages = self.passages()
        original = deepcopy(passages)
        report = self.acceptance(passages)
        self.assertEqual(report['baselines'], original)
        self.assertEqual(passages, original)


if __name__ == '__main__':
    unittest.main()
