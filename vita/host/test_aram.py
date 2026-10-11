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
