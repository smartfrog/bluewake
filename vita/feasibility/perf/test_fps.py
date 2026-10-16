"""Tests for scripts/vita/perf/fps.py: windowed frame-rate metrics.

FPS is computed from presented-frame wall stamps in microseconds. Windows are
non-overlapping and anchored at the first stamp; a window reports the frames
inside it divided by its length. The summary carries the median, p95 and
minimum window FPS plus the p95 frame time, so one slow second cannot hide in
an average. An observation end extends the windows past the last stamp, so a
stream that stalls while the process stays alive reports zero-FPS windows
instead of no measurement.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/perf"))

from fps import FpsError, frame_rate_summary, percentile, windowed_fps  # noqa: E402


def evenly_spaced(start_us, frames, step_us):
    return [start_us + index * step_us for index in range(frames)]


class FpsTest(unittest.TestCase):
    def test_evenly_spaced_frames_give_the_exact_rate(self):
        stamps = evenly_spaced(0, 61, 16667)
        summary = frame_rate_summary(stamps, window_s=1.0)
        self.assertEqual(summary["frames"], 61)
        self.assertAlmostEqual(summary["mean_fps"], 60.0, delta=0.1)
        self.assertAlmostEqual(summary["median_window_fps"], 60.0, delta=1.0)
        self.assertAlmostEqual(summary["min_window_fps"], 60.0, delta=1.0)

    def test_a_slow_window_shows_in_the_series_minimum(self):
        fast = evenly_spaced(0, 60, 16667)
        slow = evenly_spaced(60 * 16667, 30, 33333)
        fast_again = evenly_spaced(60 * 16667 + 30 * 33333, 61, 16667)
        series = windowed_fps(fast + slow + fast_again, window_s=1.0)
        self.assertEqual([round(window.fps) for window in series], [60, 30, 60])
        summary = frame_rate_summary(fast + slow + fast_again, window_s=1.0)
        self.assertAlmostEqual(summary["min_window_fps"], 30.0, delta=1.0)

    def test_window_longer_than_the_span_is_refused(self):
        with self.assertRaises(FpsError):
            windowed_fps([0, 1000000], window_s=5.0)

    def test_a_zero_interval_between_frames_is_refused(self):
        with self.assertRaises(FpsError):
            frame_rate_summary([0, 1000, 1000], window_s=1.0)

    def test_percentile_interpolates_between_neighbours(self):
        self.assertEqual(percentile([1.0], 0.95), 1.0)
        self.assertEqual(percentile([1.0, 2.0, 3.0, 4.0], 0.5), 2.5)
        self.assertAlmostEqual(percentile([10.0, 10.0, 10.0, 100.0], 0.95), 86.5)

    def test_p95_frame_time_reads_the_tail(self):
        stamps = evenly_spaced(0, 96, 10000) + [95 * 10000 + 50000 * (index + 1)
                                                for index in range(5)]
        summary = frame_rate_summary(stamps, window_s=1.0)
        self.assertAlmostEqual(summary["p95_frame_ms"], 12.0, delta=0.01)

    def test_a_stalled_tail_is_measured_as_zero_fps_windows(self):
        stamps = evenly_spaced(0, 7, 60833)
        series = windowed_fps(stamps, window_s=1.0, end_us=8052000)
        self.assertEqual([round(window.fps) for window in series], [7, 0, 0, 0, 0, 0, 0, 0])
        summary = frame_rate_summary(stamps, window_s=1.0, end_us=8052000)
        self.assertEqual(summary["windows"], 8)
        self.assertEqual(summary["median_window_fps"], 0.0)
        self.assertEqual(summary["min_window_fps"], 0.0)

    def test_observation_end_must_follow_the_last_frame(self):
        for end_us in [500000, 1000000]:
            with self.subTest(end_us=end_us):
                with self.assertRaisesRegex(FpsError, 'observation end must follow'):
                    frame_rate_summary([0, 1000000], window_s=1, end_us=end_us)


if __name__ == '__main__':
    unittest.main()
