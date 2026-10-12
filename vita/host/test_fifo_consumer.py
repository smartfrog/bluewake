import subprocess
import unittest

import test_host


class FifoConsumerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def run_probe(self, scenario):
        subprocess.run([str(test_host.OUT / 'fifo_consumer_probe'), scenario], check=True)

    def test_gpread_enable_retains_control_without_consuming_absent_data(self):
        self.run_probe('empty')

    def test_first_cp_bp_xf_command_uses_real_gather_memory_and_exact_writer_provenance(self):
        for kind in ('cp', 'bp', 'xf'):
            for width in (1, 2, 4, 8):
                with self.subTest(kind=kind, width=width):
                    self.run_probe(f'pipeline-{kind}-{width}')

    def test_partial_gather_data_cannot_be_consumed_even_with_gpread_enabled(self):
        self.run_probe('partial')

    def test_completion_pc_survives_an_eight_byte_store_spilling_into_the_next_burst(self):
        self.run_probe('spill')

    def test_gpread_activation_consumes_actual_queued_bytes_and_preserves_their_old_writer_pc(self):
        self.run_probe('queued')

    def test_first_command_frontier_leaves_later_queued_blocks_unconsumed(self):
        self.run_probe('queued-two')

    def test_command_spanning_real_read_blocks_survives_inclusive_ring_wrap(self):
        self.run_probe('wrap')

    def test_disabling_reads_keeps_real_queued_data_for_reactivation(self):
        self.run_probe('pause')

    def test_maximum_supported_xf_frame_retains_provenance_across_all_33_real_blocks(self):
        self.run_probe('pipeline-xf-max-1')

    def test_real_memory_changes_after_gather_cannot_reuse_stale_writer_provenance(self):
        self.run_probe('memory-changed')

    def test_read_refuses_full_and_partial_linked_alias_overlap(self):
        for shape in ('alias', 'partial-alias'):
            with self.subTest(shape=shape):
                self.run_probe(shape)

    def test_inconsistent_read_state_or_missing_provenance_cannot_advance_rptr(self):
        for shape in ('read-pointer', 'distance', 'missing-origin'):
            with self.subTest(shape=shape):
                self.run_probe(shape)

    def test_consumer_trace_failure_cannot_advance_pointers_or_feed_the_parser(self):
        self.run_probe('trace-failure')

    def test_consumer_counter_exhaustion_cannot_wrap_or_consume_input(self):
        self.run_probe('counter-exhausted')

    def test_unknown_opcode_and_oversized_xf_still_fail_after_only_the_real_read_block(self):
        for shape in ('unknown', 'xf-too-large'):
            with self.subTest(shape=shape):
                self.run_probe(shape)

    def test_complete_bp_copy_trigger_without_required_context_is_still_rejected_by_the_pinned_parser(self):
        self.run_probe('bp-rejected')

    def test_unimplemented_pending_cp_irq_cannot_be_hidden_by_consumption(self):
        for shape in ('overflow-irq', 'cp-irq'):
            with self.subTest(shape=shape):
                self.run_probe(shape)

    def test_gpread_activation_credits_real_cycles_without_renewing_the_quantum(self):
        self.run_probe('credit')

    def test_consume_record_binds_the_actual_read_address_and_before_after_pointers_and_distance(self):
        result = subprocess.run([str(test_host.OUT / 'fifo_consumer_probe'), 'pipeline-bp-1'],
                                check=True, capture_output=True, text=True)
        self.assertIn('[cp-consume] transfer=1 main=80001000 bytes=32 rptr=00001000 next_rptr=00001020 wptr=00001020 distance_before=32 distance_after=0', result.stderr)


if __name__ == '__main__':
    unittest.main()
