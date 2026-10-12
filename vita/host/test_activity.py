from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))
sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))


class CpuIdleWindowTest(unittest.TestCase):
    def window(self, seconds=30, threshold=0.05):
        from activity import CpuIdleWindow
        return CpuIdleWindow(seconds, threshold)

    def test_idle_cpu_reaches_the_short_deadline(self):
        window = self.window()
        self.assertFalse(window.sample(0, 0))
        self.assertFalse(window.sample(29, 0))
        self.assertTrue(window.sample(30, 0))

    def test_busy_cpu_does_not_reach_the_idle_deadline(self):
        window = self.window()
        window.sample(0, 0)
        self.assertFalse(window.sample(30, 3))
        self.assertFalse(window.sample(60, 6))

    def test_a_cpu_burst_resets_the_idle_window(self):
        window = self.window()
        window.sample(0, 0)
        window.sample(10, 0)
        self.assertFalse(window.sample(20, 2))
        self.assertFalse(window.sample(49, 2))
        self.assertTrue(window.sample(50, 2))

    def test_log_growth_resets_the_idle_window_even_at_two_percent_cpu(self):
        window = self.window()
        window.sample(0, 0)
        self.assertFalse(window.sample(29, .58))
        self.assertFalse(window.sample(30, .6, output_active=True))
        self.assertFalse(window.sample(59, 1.18))
        self.assertTrue(window.sample(60, 1.2))

    def test_backwards_cpu_accounting_is_refused(self):
        window = self.window()
        window.sample(0, 3)
        with self.assertRaisesRegex(ValueError, 'CPU'):
            window.sample(1, 2)

    def test_invalid_idle_limits_are_refused(self):
        for seconds, threshold in ((0, 0.05), (-1, 0.05), (float('inf'), 0.05),
                                   (30, 0), (30, 1.1), (30, float('nan'))):
            with self.subTest(seconds=seconds, threshold=threshold), self.assertRaises(ValueError):
                self.window(seconds, threshold)


class ProcessOutputActivityTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-activity-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def observer(self, paths):
        from activity import ProcessIdleObserver
        return ProcessIdleObserver(output_paths=paths)

    def sample(self, observer, now):
        with patch('activity.time.monotonic', return_value=now), \
             patch('activity.process_cpu_ticks', return_value={(123, 1): 0}):
            return observer(123)

    def test_only_growth_of_each_explicit_guest_output_keeps_zero_cpu_alive(self):
        for name in ('process.log', 'ww05.log', 'ww05.partial', 'ww05.dispatch', 'cache.log'):
            with self.subTest(name=name):
                path = self.root / name
                observer = self.observer((path,))
                self.assertIsNone(self.sample(observer, 0))
                path.write_bytes(b'progress')
                self.assertIsNone(self.sample(observer, 30))
                self.assertIsNone(self.sample(observer, 59))
                result = self.sample(observer, 60)
                self.assertEqual(result['stop_reason'], 'inactive')
                self.assertEqual(result['output_growth_bytes'], 8)

    def test_our_own_watch_journal_does_not_keep_a_hung_guest_alive(self):
        observer = self.observer((self.root / 'process.log',))
        self.sample(observer, 0)
        (self.root / 'run-watch.jsonl').write_text('our observer is still sampling')
        self.assertEqual(self.sample(observer, 30)['stop_reason'], 'inactive')

    def test_equal_size_overwrites_are_not_log_growth(self):
        path = self.root / 'process.log'
        path.write_text('before')
        observer = self.observer((path,))
        self.sample(observer, 0)
        path.write_text('after!')
        self.assertEqual(self.sample(observer, 30)['stop_reason'], 'inactive')

    def test_a_rotated_regular_log_can_report_new_output(self):
        path = self.root / 'process.log'
        path.write_text('before')
        observer = self.observer((path,))
        self.sample(observer, 0)
        path.rename(self.root / 'previous.log')
        path.write_text('new')
        self.assertIsNone(self.sample(observer, 30))

    def test_output_symlink_cannot_read_outside_the_owned_profile(self):
        outside = self.root / 'outside'
        outside.write_text('private')
        link = self.root / 'process.log'
        link.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'owned regular'):
            self.sample(self.observer((link,)), 0)

    def test_output_ancestor_symlink_is_not_followed(self):
        (self.root / 'outside').mkdir()
        (self.root / 'linked').symlink_to(self.root / 'outside', target_is_directory=True)
        with self.assertRaises(OSError):
            self.sample(self.observer((self.root / 'linked/process.log',)), 0)


if __name__ == '__main__':
    unittest.main()
