import subprocess
import unittest

import test_host


class CpClearTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def run_probe(self, scenario, metrics=False):
        subprocess.run([str(test_host.OUT / 'cp_clear_probe'), scenario, *(['4'] if metrics else [])], check=True)

    def test_sourced_cp_clear_halfword_read_is_zero(self):
        self.run_probe('read')

    def test_observed_idle_overflow_underflow_clear_preserves_fifo_and_other_irq_state(self):
        for scenario in ('write', 'store', 'repeat'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_unobserved_clear_masks_stop_with_the_actual_write_context(self):
        for value in (0, 1, 2, 7, 65535):
            with self.subTest(value=value):
                self.run_probe(f'value-{value}')

    def test_observed_metrics_reset_preserves_fifo_and_pending_cp_irq(self):
        for scenario in ('write', 'store', 'repeat', 'queued-gather', 'queued-produced',
                         'queued-checkpoint', 'queued-gpread', 'queued-cp-irq'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario, metrics=True)

    def test_metrics_reset_requires_the_observed_halfword_width(self):
        for width in (1, 4, 8):
            with self.subTest(width=width):
                self.run_probe(f'write-width-{width}', metrics=True)

    def test_metrics_reset_credits_real_cycles_without_renewing_the_spent_quantum(self):
        self.run_probe('credit-write', metrics=True)

    def test_cp_clear_read_requires_the_sourced_halfword_width(self):
        for width in (1, 4, 8):
            with self.subTest(width=width):
                self.run_probe(f'read-width-{width}')

    def test_cp_clear_write_requires_the_observed_halfword_width(self):
        for width in (1, 4, 8):
            with self.subTest(width=width):
                self.run_probe(f'write-width-{width}')

    def test_cp_clear_cannot_silently_acknowledge_input_active_reads_or_pending_cp_irq(self):
        for state in ('gather', 'produced', 'checkpoint', 'gpread', 'cp-irq'):
            with self.subTest(state=state):
                self.run_probe(f'queued-{state}')

    def test_cp_clear_access_credits_real_cycles_without_renewing_the_spent_quantum(self):
        for action in ('read', 'write'):
            with self.subTest(action=action):
                self.run_probe(f'credit-{action}')


if __name__ == '__main__':
    unittest.main()
