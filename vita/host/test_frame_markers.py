from pathlib import Path
import subprocess
import sys
import unittest

import test_host

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/vita/perf'))
from frame_events import parse_frame_events


class FrameMarkersTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario, binary='gx_drain_probe'):
        result = subprocess.run([str(test_host.OUT / binary), scenario], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return parse_frame_events(result.stderr)

    def test_two_confirmed_display_copies_emit_the_exact_wall_microsecond_contract(self):
        events = self.probe('marker-present')
        self.assertEqual([(event.index, event.us) for event in events.presents], [(1, 1000000), (2, 2000000)])
        self.assertEqual(events.retraces, [])

    def test_failed_backend_presentation_does_not_emit_a_frame_marker(self):
        self.assertEqual(self.probe('marker-failed-present').presents, [])

    def test_draws_without_a_display_copy_do_not_emit_frame_markers(self):
        self.assertEqual(self.probe('state').presents, [])

    def test_clock_failure_stops_after_the_actual_swap_without_a_fabricated_stamp(self):
        self.assertEqual(self.probe('marker-clock-failure').presents, [])

    def test_invalid_or_overflowing_clock_values_do_not_emit_measurement_data(self):
        for scenario in ('marker-invalid-clock', 'marker-overflow'):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.probe(scenario).presents, [])

    def test_non_advancing_wall_time_stops_without_adjusting_or_repeating_the_stamp(self):
        for scenario in ('marker-backward', 'marker-repeated-time'):
            with self.subTest(scenario=scenario):
                self.assertEqual([(event.index, event.us) for event in self.probe(scenario).presents], [(1, 1000000)])

    def test_marker_write_failure_cannot_publish_measurement_data(self):
        self.assertEqual(self.probe('marker-write-failure').presents, [])

    def test_real_due_retraces_emit_only_the_secondary_retrace_series(self):
        events = self.probe('multiple', 'vi_clock_probe')
        self.assertEqual([event.index for event in events.retraces], [1, 2, 3])
        self.assertEqual(events.presents, [])

    def test_disabled_vi_clock_does_not_emit_retrace_markers(self):
        self.assertEqual(self.probe('off', 'vi_clock_probe').retraces, [])
