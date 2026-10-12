import subprocess
import unittest

import test_host


class GxStreamTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def run_probe(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'gx_stream_probe'), scenario], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def test_opt_in_continues_after_the_first_command_and_dispatches_following_cp_bp_xf(self):
        result = self.run_probe('pipeline')
        self.assertIn('[gx-command] index=1 opcode=61 bytes=5 pc=80001000 completion_pc=80001010 sp=817FFF00 hex=6141123456', result.stderr)
        self.assertIn('[gx-command] index=2 opcode=08 bytes=6 pc=80001014 completion_pc=80001028', result.stderr)

    def test_later_real_read_blocks_do_not_overwrite_the_accepted_first_frontier(self):
        self.run_probe('second-block')

    def test_command_journal_limit_does_not_stop_decoding_or_guest_execution(self):
        result = self.run_probe('log-limit')
        self.assertEqual(result.stderr.count('[gx-command]'), 2)

    def test_writer_provenance_survives_a_split_command_and_ring_wrap_after_first_acceptance(self):
        self.run_probe('split-wrap')

    def test_consumed_provenance_is_reused_without_a_33_block_execution_limit(self):
        self.run_probe('many-blocks')

    def test_full_journal_logs_2048_commands_and_keeps_decoding_after_its_bound(self):
        result = self.run_probe('long-journal')
        self.assertEqual(result.stderr.count('[gx-command]'), 2048)
        self.assertIn('[gx-command] index=2048 ', result.stderr)
        self.assertNotIn('[gx-command] index=2049 ', result.stderr)

    def test_unsupported_later_command_stops_honestly_without_erasing_first_acceptance(self):
        self.run_probe('unknown')

    def test_journal_failure_cannot_certify_an_unlogged_command(self):
        for scenario in ('trace-first', 'trace-second'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_decoded_command_counter_cannot_wrap(self):
        self.run_probe('counter')
