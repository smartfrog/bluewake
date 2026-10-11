import subprocess
import unittest

import test_host


class AramDmaTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def test_masks_halfword_and_combined_word_registers_without_starting_dma(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'registers'], check=True)

    def test_real_main_to_aram_copy_has_separate_busy_and_completion_states(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'to-aram'], check=True)

    def test_transfer_and_completion_trace_retains_the_real_addresses_length_and_cycles(self):
        result = subprocess.run([str(test_host.OUT / 'aram_probe'), 'to-aram'], check=True,
                                capture_output=True, text=True)
        self.assertIn('[aram-dma] transfer=1 main=80001000 aram=00000400 bytes=64 direction=0 completion_in=492 pc=80302EC8', result.stderr)
        self.assertIn('[aram-complete] transfer=1 cycles=492', result.stderr)

    def test_real_aram_to_main_copy_preserves_surrounding_memory(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'from-aram'], check=True)

    def test_completion_uses_credited_cycles_deadlines_and_masked_pi_source(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'timing'], check=True)

    def test_acknowledging_before_completion_cannot_suppress_the_future_interrupt(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'ack-busy'], check=True)

    def test_unmapped_main_range_refuses_copy_with_actual_write_context(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'unmapped-main'], check=True)

    def test_hsp_expansion_address_is_not_wrapped_into_real_aram(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'hsp'], check=True)

    def test_observed_absent_hsp_read_preserves_buffers_and_uses_normal_busy_completion_and_acknowledgement(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'absent-hsp-read'], check=True)

    def test_accepted_hsp_request_and_completion_records_share_the_aram_sampling_schedule(self):
        result = subprocess.run([str(test_host.OUT / 'aram_probe'), 'hsp-trace-sampling'], check=True,
                                capture_output=True, text=True)
        lines = result.stderr.splitlines()
        absent = [line for line in lines if line.startswith('[aram-hsp-absent]')]
        requests = [line for line in lines if line.startswith('[aram-expansion-request]')]
        counts = [int(line.split('transfer=')[1].split()[0]) for line in lines if line.startswith('[aram-dma]')]
        self.assertEqual(len(absent), 9)
        self.assertEqual(len(requests), 9)
        self.assertEqual(sum(line.startswith('[aram-complete]') for line in lines), 9)
        self.assertEqual(counts, [*range(1, 9), 1000])
        self.assertIn('direction=0 buffer=preserved pc=80317758', absent[-1])

    def test_refused_hsp_shape_is_still_logged_after_the_initial_sampling_window(self):
        result = subprocess.run([str(test_host.OUT / 'aram_probe'), 'hsp-trace-refused'], check=True,
                                capture_output=True, text=True)
        self.assertIn('[aram-expansion-request] main=00001000 aram=01000000 count=80000040', result.stderr)
        self.assertNotIn('[aram-hsp-absent]', result.stderr)

    def test_transfer_counter_exhaustion_cannot_wrap_the_sampling_sequence_or_commit_dma(self):
        for device in ('aram', 'hsp'):
            with self.subTest(device=device):
                subprocess.run([str(test_host.OUT / 'aram_probe'), f'{device}-counter-exhausted'], check=True)

    def test_observed_absent_hsp_write_preserves_source_and_aram_with_normal_dma_bookkeeping(self):
        for access in ('write', 'write-word'):
            with self.subTest(access=access):
                subprocess.run([str(test_host.OUT / 'aram_probe'), 'absent-hsp-' + access], check=True)

    def test_absent_hsp_write_record_identifies_the_actual_direction_and_guest_pc(self):
        result = subprocess.run([str(test_host.OUT / 'aram_probe'), 'absent-hsp-write'], check=True,
                                capture_output=True, text=True)
        self.assertIn('[aram-hsp-absent] main=0040CDE0 aram=01000000 bytes=32 direction=0 buffer=preserved pc=80317758', result.stderr)

    def test_unobserved_expansion_shapes_still_fail_before_changing_memory(self):
        for shape in ('long', 'long-write', 'address', 'main'):
            with self.subTest(shape=shape):
                subprocess.run([str(test_host.OUT / 'aram_probe'), f'absent-hsp-{shape}'], check=True)

    def test_refused_expansion_dma_retains_the_actual_register_request_without_copying(self):
        for scenario, count in (('hsp', '00000020'), ('hsp-halfword', '80000020')):
            with self.subTest(scenario=scenario):
                result = subprocess.run([str(test_host.OUT / 'aram_probe'), scenario], check=True,
                                        capture_output=True, text=True)
                self.assertIn(f'[aram-expansion-request] main=00001000 aram=01000000 count={count} '
                              'ar_info=0043 control=0804 pc=80302EC8', result.stderr)

    def test_zero_length_dma_is_an_explicit_support_boundary(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'zero'], check=True)

    def test_unimplemented_mirror_layout_is_not_treated_as_a_flat_copy(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'mirror-mode'], check=True)

    def test_wrong_access_width_does_not_mutate_dma_registers(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'width'], check=True)

    def test_busy_dma_register_write_cannot_replace_an_owned_transfer(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'busy-write'], check=True)

    def test_existing_aram_owner_is_preserved_and_refused(self):
        subprocess.run([str(test_host.OUT / 'aram_probe'), 'existing-owner'], check=True)


if __name__ == '__main__':
    unittest.main()
