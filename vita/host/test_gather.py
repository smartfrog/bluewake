import subprocess
import unittest

import test_host


class GatherPipeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def run_probe(self, scenario):
        return subprocess.run([str(test_host.OUT / 'gather_probe'), scenario], check=True,
                              capture_output=True, text=True)

    def test_observed_gplink_control_retains_readback_without_producing_data(self):
        self.run_probe('control')

    def test_partial_writes_retain_only_real_big_endian_bytes_without_moving_pointers(self):
        for width in (1, 2, 4, 8):
            with self.subTest(width=width):
                self.run_probe(f'partial-{width}')

    def test_real_full_gather_block_copies_mem1_and_produces_linked_cp_distance(self):
        for width in (1, 2, 4, 8):
            with self.subTest(width=width):
                self.run_probe(f'burst-{width}')

    def test_unlinked_burst_copies_memory_and_advances_only_the_pi_pointer(self):
        for width in (1, 2, 4, 8):
            with self.subTest(width=width):
                self.run_probe(f'unlinked-burst-{width}')

    def test_burst_keeps_the_real_spill_bytes_for_the_next_block(self):
        self.run_probe('spill')

    def test_end_pointer_is_inclusive_and_wraps_after_copying_its_32_byte_block(self):
        self.run_probe('wrap')

    def test_complete_command_cannot_reach_the_parser_without_gpread(self):
        for linked in (True, False):
            with self.subTest(linked=linked):
                self.run_probe('no-parse' if linked else 'unlinked-no-parse')

    def test_gather_store_credits_cycles_without_renewing_the_spent_quantum(self):
        self.run_probe('credit')

    def test_memory_copy_marks_dirty_memory_and_invalidates_the_matching_reservation(self):
        self.run_probe('dirty-reservation')

    def test_invalid_write_width_preserves_pending_bytes_memory_and_pointers(self):
        for width in (0, 3, 16):
            with self.subTest(width=width):
                self.run_probe(f'width-{width}')

    def test_invalid_pi_ring_refuses_burst_before_committing_any_state(self):
        for shape in ('range', 'pointer', 'alignment', 'mem1'):
            with self.subTest(shape=shape):
                self.run_probe(f'pi-{shape}')

    def test_linked_ring_mismatch_refuses_burst_before_copying(self):
        for field in ('base', 'end', 'pointer'):
            with self.subTest(field=field):
                self.run_probe(f'cp-{field}')

    def test_exhausted_ring_capacity_cannot_overwrite_unconsumed_data_or_wrap_distance(self):
        for shape in ('capacity', 'distance-overflow'):
            with self.subTest(shape=shape):
                self.run_probe(shape)

    def test_full_and_partial_linked_alias_overlap_cannot_receive_a_gather_copy(self):
        for shape in ('alias', 'partial-alias'):
            with self.subTest(shape=shape):
                self.run_probe(shape)

    def test_unobserved_fifo_reconfiguration_cannot_discard_pending_input(self):
        for register in ('distance-low', 'distance-high', 'rptr-high', 'pi-wptr'):
            with self.subTest(register=register):
                self.run_probe(f'freeze-{register}')

    def test_trace_failure_prevents_unrecorded_memory_and_pointer_commits(self):
        self.run_probe('trace-failure')

    def test_burst_counter_exhaustion_cannot_wrap_or_commit_a_copy(self):
        self.run_probe('counter-exhausted')

    def test_existing_host_failure_cannot_commit_another_gather_store(self):
        self.run_probe('prior-failure')

    def test_burst_records_sample_real_transfers_without_creating_irq_or_gpu_activity(self):
        result = self.run_probe('sampling')
        lines = result.stderr.splitlines()
        counts = [int(line.split('transfer=')[1].split()[0]) for line in lines]
        self.assertEqual(counts, [*range(1, 9), 1000])
        self.assertIn('bytes=32 linked=1 distance=32000 pc=803212CC', lines[-1])


if __name__ == '__main__':
    unittest.main()
