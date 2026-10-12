import subprocess
import unittest

import test_host


class WritePipeAddressTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def run_probe(self, action, value=0x0C008000):
        subprocess.run([str(test_host.OUT / 'wpar_probe'), action, str(value)], check=True)

    def test_write_pipe_address_has_the_sourced_zero_reset_and_empty_bne(self):
        self.run_probe('reset')

    def test_observed_wpar_configuration_runs_through_the_spr_hook_and_instruction_fallback(self):
        for action in ('write', 'instruction', 'repeat'):
            with self.subTest(action=action):
                self.run_probe(action)

    def test_bne_reports_actual_untransferred_gather_bytes(self):
        self.run_probe('bne-partial')

    def test_completed_gather_burst_clears_bne_without_discarding_queued_cp_memory(self):
        self.run_probe('bne-queued')

    def test_reconfiguring_after_partial_or_transferred_input_refuses_without_loss(self):
        for state in ('late-partial', 'late-burst'):
            with self.subTest(state=state):
                self.run_probe(state)

    def test_unobserved_addresses_reserved_bits_and_written_bne_are_not_accepted(self):
        for value in (0, 0xCC008000, 0x0C008001, 0x0C008004, 0x0C008020, 0xFFFFFFFF):
            with self.subTest(value=value):
                self.run_probe('reject', value)

    def test_wpar_trace_failure_cannot_commit_configuration(self):
        self.run_probe('trace')

    def test_pmc1_is_not_confused_with_the_reported_hexadecimal_wpar_spr(self):
        self.run_probe('pmc-unsupported', 0)

    def test_wpar_reads_and_writes_credit_real_cycles_without_renewing_the_spent_quantum(self):
        for action in ('credit-read', 'credit-write'):
            with self.subTest(action=action):
                self.run_probe(action)


if __name__ == '__main__':
    unittest.main()
