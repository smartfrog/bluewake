"""Tests for scripts/vita/perf/frame_events.py: the frame-event stream parser.

The parser reads a log's lines and keeps only the two measurement markers,
searched anywhere in a line so interleaved logger output cannot hide a stamp.
A malformed marker, a repeated index or a wall time that goes backwards is an
error: a broken stream is not a measurement.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/perf"))

from frame_events import FrameEventError, parse_frame_events  # noqa: E402


def log(*lines):
    return "\n".join(lines) + "\n"


class FrameEventTest(unittest.TestCase):
    def test_parses_both_markers_and_ignores_everything_else(self):
        events = parse_frame_events(log(
            "[aurora:info] renderer ready",
            "[frame-timing] retrace=1 us=1000",
            "[frame-present] frame=1 us=2000",
            "[gx] worker drained 40 commands",
            "[frame-timing] retrace=2 us=17667",
            "[frame-present] frame=2 us=18000",
        ))
        self.assertEqual([(e.index, e.us) for e in events.retraces],
                         [(1, 1000), (2, 17667)])
        self.assertEqual([(e.index, e.us) for e in events.presents],
                         [(1, 2000), (2, 18000)])

    def test_recovers_a_stamp_interleaved_with_logger_output(self):
        events = parse_frame_events(log(
            "[aurora:info:aurora[frame-timing] retrace=7 us=99000",
            "[frame-present] frame=3 us=99100",
        ))
        self.assertEqual([(e.index, e.us) for e in events.retraces], [(7, 99000)])
        self.assertEqual([(e.index, e.us) for e in events.presents], [(3, 99100)])

    def test_refuses_a_marker_with_an_unparsable_number(self):
        with self.assertRaises(FrameEventError):
            parse_frame_events(log("[frame-timing] retrace=one us=1000"))

    def test_refuses_a_repeated_retrace_index(self):
        with self.assertRaises(FrameEventError):
            parse_frame_events(log(
                "[frame-timing] retrace=4 us=1000",
                "[frame-timing] retrace=4 us=2000",
            ))

    def test_refuses_wall_time_that_goes_backwards(self):
        with self.assertRaises(FrameEventError):
            parse_frame_events(log(
                "[frame-present] frame=1 us=5000",
                "[frame-present] frame=2 us=4000",
            ))


if __name__ == '__main__':
    unittest.main()
