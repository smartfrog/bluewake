"""Tests for scripts/vita/relset/active_set.py: the active-set tracker.

The tracker replays a REL-event stream (loads minus unloads) and reports the
active set at every step, the high-water mark, per-module residency and the
top modules by bytes. It refuses sequences that cannot describe a real active
set: a load of an already-loaded module, or an unload of one that is not
loaded.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/relset"))

from active_set import ActiveSetError, track_active_set  # noqa: E402
from rel_events import RelLoad, RelUnload  # noqa: E402


def load(module_id, name, size, us):
    return RelLoad(module_id=module_id, name=name, bytes=size, us=us)


class TrackActiveSetTest(unittest.TestCase):
    def test_active_set_is_loads_minus_unloads(self):
        report = track_active_set([
            load(2, "d_a_agbsw0", 100, 10),
            load(3, "d_a_andsw0", 50, 20),
            RelUnload(module_id=2),
        ])
        self.assertEqual(report.active_modules, 1)
        self.assertEqual(report.active_bytes, 50)
        self.assertEqual(list(report.active_at_end), [3])

    def test_high_water_keeps_the_largest_byte_total(self):
        report = track_active_set([
            load(2, "d_a_agbsw0", 100, 10),
            load(3, "d_a_andsw0", 50, 20),
            load(4, "d_a_andsw2", 10, 30),
            RelUnload(module_id=3),
        ])
        self.assertEqual(report.peak_bytes, 160)
        self.assertEqual(report.peak_bytes_seq, 3)
        self.assertEqual(report.peak_modules, 3)
        self.assertEqual(report.peak_modules_seq, 3)

    def test_byte_peak_and_module_peak_can_differ(self):
        report = track_active_set([
            load(2, "big", 1000, 10),
            load(3, "small", 10, 20),
            RelUnload(module_id=2),
            load(4, "a", 5, 30),
            load(5, "b", 5, 40),
        ])
        self.assertEqual(report.peak_bytes, 1010)
        self.assertEqual(report.peak_bytes_seq, 2)
        self.assertEqual(report.peak_modules, 3)
        self.assertEqual(report.peak_modules_seq, 5)

    def test_timeline_records_the_active_size_after_every_event(self):
        report = track_active_set([
            load(2, "d_a_agbsw0", 100, 10),
            RelUnload(module_id=2),
            load(2, "d_a_agbsw0", 100, 30),
        ])
        self.assertEqual(
            [(step.seq, step.kind, step.us, step.active_modules, step.active_bytes)
             for step in report.timeline],
            [(1, "load", 10, 1, 100),
             (2, "unload", None, 0, 0),
             (3, "load", 30, 1, 100)])

    def test_residency_span_carries_the_first_load_and_last_unload(self):
        report = track_active_set([
            load(2, "d_a_agbsw0", 100, 10),
            RelUnload(module_id=2, us=20),
            load(2, "d_a_agbsw0", 100, 30),
            RelUnload(module_id=2, us=40),
        ])
        residency = report.modules[2]
        self.assertEqual(residency.loads, 2)
        self.assertEqual(residency.unloads, 2)
        self.assertEqual(residency.first_load_us, 10)
        self.assertEqual(residency.last_unload_us, 40)
        self.assertFalse(residency.open_at_end)

    def test_an_unload_without_a_wall_time_leaves_the_span_end_unknown(self):
        report = track_active_set([
            load(2, "d_a_agbsw0", 100, 10),
            RelUnload(module_id=2),
        ])
        residency = report.modules[2]
        self.assertIsNone(residency.last_unload_us)
        self.assertFalse(residency.open_at_end)

    def test_a_module_never_unloaded_is_open_at_end(self):
        report = track_active_set([load(7, "d_a_att", 40, 10)])
        residency = report.modules[7]
        self.assertTrue(residency.open_at_end)
        self.assertIsNone(residency.last_unload_us)

    def test_top_modules_are_ranked_by_bytes(self):
        report = track_active_set([
            load(2, "d_a_agbsw0", 100, 10),
            load(3, "d_a_andsw0", 400, 20),
            load(4, "d_a_andsw2", 200, 30),
        ])
        self.assertEqual([entry.module_id for entry in report.top_modules],
                         [3, 4, 2])
        self.assertEqual([entry.bytes for entry in report.top_modules],
                         [400, 200, 100])

    def test_totals_count_loads_unloads_and_unique_modules(self):
        report = track_active_set([
            load(2, "d_a_agbsw0", 100, 10),
            load(3, "d_a_andsw0", 50, 20),
            RelUnload(module_id=2),
            load(2, "d_a_agbsw0", 100, 30),
        ])
        self.assertEqual(report.total_loads, 3)
        self.assertEqual(report.total_unloads, 1)
        self.assertEqual(report.unique_modules, 2)
        self.assertEqual(report.cumulative_bytes_loaded, 250)

    def test_an_empty_stream_reports_an_empty_set(self):
        report = track_active_set([])
        self.assertEqual(report.timeline, [])
        self.assertEqual(report.peak_bytes, 0)
        self.assertEqual(report.peak_modules, 0)
        self.assertEqual(report.active_modules, 0)
        self.assertEqual(report.active_bytes, 0)

    def test_refuses_a_load_of_an_already_loaded_module(self):
        with self.assertRaises(ActiveSetError):
            track_active_set([
                load(2, "d_a_agbsw0", 100, 10),
                load(2, "d_a_agbsw0", 100, 20),
            ])

    def test_refuses_an_unload_of_a_module_that_is_not_loaded(self):
        with self.assertRaises(ActiveSetError):
            track_active_set([RelUnload(module_id=2)])


if __name__ == '__main__':
    unittest.main()
