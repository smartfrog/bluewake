import subprocess
import unittest

import test_host


class BatContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def test_boot_bats_match_the_pinned_gamecube_setup(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'boot'], check=True)

    def test_actual_failing_mtdbat_instruction_executes_and_advances_normally(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'fallback'], check=True)

    def test_committed_bat_trace_retains_registers_range_and_actual_instruction_pc(self):
        result = subprocess.run([str(test_host.OUT / 'bat_probe'), 'fallback'], check=True,
                                capture_output=True, text=True)
        self.assertIn('[bat] spr=536 upper=800001FF lower=00000002 bytes=16777216 pc=80305C54', result.stderr)

    def test_sdk_split_mem1_bats_are_retained_without_creating_time(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'readback'], check=True)

    def test_real_rfi_enables_the_validated_mem1_view_with_unchanged_backing_bytes(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'rfi'], check=True)

    def test_user_mode_bat_write_is_privileged_and_cannot_change_the_mapping(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'user-write'], check=True)

    def test_user_mode_bat_read_is_privileged(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'user-read'], check=True)

    def test_active_translation_cannot_be_invalidated_without_a_real_mmu(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'active-write'], check=True)

    def test_lower_write_requires_prior_upper_invalidation(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'lower-active'], check=True)

    def test_unknown_effective_base_is_refused_with_the_actual_spr_value(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'unknown-base'], check=True)

    def test_unknown_block_length_is_not_treated_as_the_supported_mem1_view(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'unknown-size'], check=True)

    def test_unknown_physical_base_cannot_mutate_a_prepared_pair(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'unknown-physical'], check=True)

    def test_enabling_an_upper_without_a_matching_lower_is_refused(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'upper-before-lower'], check=True)

    def test_enabling_translation_with_a_mem1_hole_stops_at_the_edge(self):
        subprocess.run([str(test_host.OUT / 'bat_probe'), 'incomplete'], check=True)


if __name__ == '__main__':
    unittest.main()
