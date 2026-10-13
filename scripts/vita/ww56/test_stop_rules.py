"""Tests for the early-stop decision rules.

WW56 stops at the first useful frontier instead of waiting out the ceiling:
an unsupported/fatal marker stops the run, the first requested REL settles
briefly and stops, and a first presentation that never reaches REL stops
after its grace. Every stop keeps the INCOMPLETE disposition.
"""

import unittest

from stop_rules import StopState, evaluate_stop, is_failure


class FailureMarkerTests(unittest.TestCase):
    def test_known_failure_markers_are_detected(self):
        for line in ("[gx-unsupported] opcode=1", "[fatal] boom", "STOP reason=unsupported",
                     "STOP reason=unhandled", "[renderer-error] x"):
            with self.subTest(line=line):
                self.assertTrue(is_failure(line))

    def test_ordinary_progress_is_not_a_failure(self):
        for line in ("[progress] turns=1 edges=1", "[exception-vector-return] input=00000510",
                     "STOP reason=planned"):
            with self.subTest(line=line):
                self.assertFalse(is_failure(line))


class EvaluateStopTests(unittest.TestCase):
    def state(self, **overrides):
        values = dict(ceiling_seconds=180.0, present_grace_seconds=10.0, rel_settle_seconds=5.0, started=100.0)
        values.update(overrides)
        return StopState(**values)

    def test_nothing_stops_before_any_frontier(self):
        self.assertIsNone(evaluate_stop(self.state(), now=101.0, failure_lines=[]))

    def test_a_failure_line_stops_immediately(self):
        stop = evaluate_stop(self.state(), now=101.0, failure_lines=["[fatal] boom"])
        self.assertEqual(stop['stop_reason'], 'unsupported-or-context-error')
        self.assertEqual(stop['failure_lines'], ["[fatal] boom"])

    def test_the_rel_settle_window_stops_after_the_first_request(self):
        state = self.state()
        state.note_rel(110.0)
        self.assertIsNone(evaluate_stop(state, now=114.0, failure_lines=[]))
        stop = evaluate_stop(state, now=115.0, failure_lines=[])
        self.assertEqual(stop['stop_reason'], 'first-requested-rel-settle')
        self.assertEqual(stop['first_rel_at'], 110.0)

    def test_the_present_grace_stops_when_no_rel_follows(self):
        state = self.state()
        state.note_present(110.0)
        self.assertIsNone(evaluate_stop(state, now=119.0, failure_lines=[]))
        stop = evaluate_stop(state, now=120.0, failure_lines=[])
        self.assertEqual(stop['stop_reason'], 'first-present-grace')

    def test_the_rel_rule_takes_over_from_the_present_grace(self):
        state = self.state()
        state.note_present(110.0)
        state.note_rel(112.0)
        stop = evaluate_stop(state, now=120.0, failure_lines=[])
        self.assertEqual(stop['stop_reason'], 'first-requested-rel-settle')

    def test_a_disabled_grace_never_fires(self):
        state = self.state(present_grace_seconds=None, rel_settle_seconds=None)
        state.note_present(110.0)
        state.note_rel(112.0)
        self.assertIsNone(evaluate_stop(state, now=150.0, failure_lines=[]))

    def test_the_ceiling_stops_last(self):
        state = self.state()
        self.assertIsNone(evaluate_stop(state, now=279.0, failure_lines=[]))
        stop = evaluate_stop(state, now=280.0, failure_lines=[])
        self.assertEqual(stop['stop_reason'], 'bounded-phase-planned-time-stop')

    def test_failure_outranks_every_other_rule(self):
        state = self.state()
        state.note_rel(101.0)
        stop = evaluate_stop(state, now=280.0, failure_lines=["[fatal] boom"])
        self.assertEqual(stop['stop_reason'], 'unsupported-or-context-error')

    def test_invalid_configuration_is_refused(self):
        for overrides in (dict(ceiling_seconds=0), dict(ceiling_seconds=float('nan')),
                          dict(rel_settle_seconds=0), dict(present_grace_seconds=-2)):
            with self.subTest(overrides=overrides):
                with self.assertRaisesRegex(ValueError, 'finite'):
                    self.state(**overrides)


if __name__ == '__main__':
    unittest.main()
