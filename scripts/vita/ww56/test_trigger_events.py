"""Tests for the one-shot snapshot trigger scanner.

The WW50 lesson: the first-present snapshot was missed because presents and the
kill happened between one-second log observations. WW56 snapshots are
event-driven instead: the journal tail fires exactly once on the GPU-created
line and exactly once on the first frame-present line, and never fires those
kinds again. These tests pin the pure line-to-trigger logic.
"""

import unittest

from trigger_events import (
    GPU_TRIGGER,
    PRESENT_TRIGGER,
    OneShotTriggerScanner,
    TriggerError,
)

GPU_LINE = "[00:08:23.777] |I| [create]: GPU = Apple M1 Pro (G13S C0)"
CONTEXT_LINE = "[create_gl_context]: Created OpenGL 4.6 context"
PRESENT_ONE = "[frame-present] frame=1 us=1791929310581478"
PRESENT_TWO = "[frame-present] frame=2 us=1791929310583479"
NOISE = "[progress] turns=1 edges=1 cycles=1 pc=80000000"


class TriggerScannerTests(unittest.TestCase):
    def test_gpu_trigger_fires_once_and_never_again(self):
        scanner = OneShotTriggerScanner()
        first = scanner.feed([CONTEXT_LINE, GPU_LINE, NOISE])
        self.assertEqual([event.kind for event in first], [GPU_TRIGGER])
        self.assertEqual(first[0].line, GPU_LINE)
        second = scanner.feed([GPU_LINE, GPU_LINE])
        self.assertEqual(second, [])

    def test_present_trigger_fires_once_on_the_first_present(self):
        scanner = OneShotTriggerScanner()
        first = scanner.feed([PRESENT_ONE, PRESENT_TWO])
        self.assertEqual(len(first), 1)
        self.assertEqual(first[0].kind, PRESENT_TRIGGER)
        self.assertEqual((first[0].frame, first[0].us), (1, 1791929310581478))
        self.assertEqual(scanner.feed([PRESENT_TWO]), [])

    def test_both_kinds_fire_in_occurrence_order_in_one_batch(self):
        scanner = OneShotTriggerScanner()
        events = scanner.feed([GPU_LINE, PRESENT_ONE])
        self.assertEqual([event.kind for event in events], [GPU_TRIGGER, PRESENT_TRIGGER])

    def test_a_line_without_the_complete_marker_does_not_fire(self):
        scanner = OneShotTriggerScanner()
        self.assertEqual(scanner.feed(["[create]: GPU"]), [])
        events = scanner.feed(["[create]: GPU = Apple M1 Pro"])
        self.assertEqual([event.kind for event in events], [GPU_TRIGGER])

    def test_malformed_present_stamp_is_an_error(self):
        scanner = OneShotTriggerScanner()
        with self.assertRaisesRegex(TriggerError, "malformed .frame-present. stamp"):
            scanner.feed(["[frame-present] frame=1"])

    def test_unknown_kind_is_refused(self):
        with self.assertRaisesRegex(ValueError, "unknown trigger kind"):
            OneShotTriggerScanner(kinds=("gpu-context-created", "not-a-kind"))

    def test_unrequested_kinds_are_ignored(self):
        scanner = OneShotTriggerScanner(kinds=(PRESENT_TRIGGER,))
        self.assertEqual(scanner.feed([GPU_LINE]), [])
        self.assertEqual([event.kind for event in scanner.feed([PRESENT_ONE])], [PRESENT_TRIGGER])

    def test_empty_feed_is_empty(self):
        self.assertEqual(OneShotTriggerScanner().feed([]), [])


if __name__ == '__main__':
    unittest.main()
