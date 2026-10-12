import subprocess
import unittest

import test_host


class CpPerfTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def run_probe(self, scenario):
        subprocess.run([str(test_host.OUT / 'cp_perf_probe'), scenario], check=True)

    def test_perf_selector_retains_only_the_pinned_three_bit_mask(self):
        for value in (0, 1, 2, 3, 4, 5, 6, 7, 8, 65535):
            with self.subTest(value=value):
                self.run_probe(f'value-{value}')

    def test_perf_selection_does_not_consume_fifo_or_acknowledge_interrupts(self):
        self.run_probe('active')

    def test_perf_selector_read_requires_the_sourced_halfword_width(self):
        for width in (1, 4, 8):
            with self.subTest(width=width):
                self.run_probe(f'read-width-{width}')

    def test_perf_selector_write_requires_the_sourced_halfword_width(self):
        for width in (1, 4, 8):
            with self.subTest(width=width):
                self.run_probe(f'write-width-{width}')

    def test_perf_selection_credits_cycles_without_renewing_a_spent_quantum(self):
        self.run_probe('credit')


if __name__ == '__main__':
    unittest.main()
