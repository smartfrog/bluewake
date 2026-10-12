from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))


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


if __name__ == '__main__':
    unittest.main()
