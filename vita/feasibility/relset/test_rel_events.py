"""Tests for scripts/vita/relset/rel_events.py: the REL-event stream parser.

The parser reads a log's lines and keeps only the two lifecycle markers,
searched anywhere in a line so interleaved logger output cannot hide a stamp
(the frame-event harness reads [frame-timing]/[frame-present] the same way).
A malformed marker or a load wall time that does not advance is an error: a
broken stream is not a measurement. Load/unload consistency (a module loaded
twice without an unload) is the tracker's concern, not the parser's.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/relset"))

from rel_events import RelEventError, RelLoad, RelUnload, parse_rel_events  # noqa: E402


def log(*lines):
    return "\n".join(lines) + "\n"


class RelEventTest(unittest.TestCase):
    def test_parses_both_markers_and_ignores_everything_else(self):
        events = parse_rel_events(log(
            "[aurora:info] renderer ready",
            "[rel-load] module=2 name=d_a_agbsw0 bytes=25380 us=1000",
            "[rel] archived resource materialized name=\"d_a_agbsw0\"",
            "[rel-unload] module=2",
            "[rel-load] module=181 name=d_a_acorn_leaf bytes=9164 us=5000",
        ))
        self.assertEqual(events, [
            RelLoad(module_id=2, name="d_a_agbsw0", bytes=25380, us=1000),
            RelUnload(module_id=2),
            RelLoad(module_id=181, name="d_a_acorn_leaf", bytes=9164, us=5000),
        ])

    def test_recovers_a_marker_interleaved_with_logger_output(self):
        events = parse_rel_events(log(
            "[vita:info:ww14[rel-load] module=7 name=d_a_att bytes=20000 us=42",
        ))
        self.assertEqual(events, [RelLoad(module_id=7, name="d_a_att", bytes=20000, us=42)])

    def test_accepts_a_reload_of_the_same_module_after_an_unload(self):
        events = parse_rel_events(log(
            "[rel-load] module=2 name=d_a_agbsw0 bytes=25380 us=1000",
            "[rel-unload] module=2",
            "[rel-load] module=2 name=d_a_agbsw0 bytes=25380 us=9000",
        ))
        self.assertEqual([type(event) for event in events], [RelLoad, RelUnload, RelLoad])

    def test_refuses_a_marker_with_an_unparsable_number(self):
        with self.assertRaises(RelEventError):
            parse_rel_events(log("[rel-load] module=two name=d_a_att bytes=1 us=1"))

    def test_refuses_a_load_with_a_missing_name(self):
        with self.assertRaises(RelEventError):
            parse_rel_events(log("[rel-load] module=2 bytes=25380 us=1000"))

    def test_refuses_a_load_with_zero_bytes(self):
        with self.assertRaises(RelEventError):
            parse_rel_events(log("[rel-load] module=2 name=d_a_agbsw0 bytes=0 us=1000"))

    def test_refuses_a_load_wall_time_that_does_not_advance(self):
        with self.assertRaises(RelEventError):
            parse_rel_events(log(
                "[rel-load] module=2 name=d_a_agbsw0 bytes=25380 us=5000",
                "[rel-load] module=181 name=d_a_acorn_leaf bytes=9164 us=5000",
            ))

    def test_refuses_a_load_wall_time_that_goes_backwards(self):
        with self.assertRaises(RelEventError):
            parse_rel_events(log(
                "[rel-load] module=2 name=d_a_agbsw0 bytes=25380 us=5000",
                "[rel-load] module=181 name=d_a_acorn_leaf bytes=9164 us=4000",
            ))

    def test_an_empty_stream_has_no_events(self):
        self.assertEqual(parse_rel_events(log("[boot] nothing here")), [])


if __name__ == '__main__':
    unittest.main()
