import subprocess
import unittest

import test_host


class DspInitTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def test_each_altered_init_byte_refuses_the_actual_transfer_with_write_context(self):
        for index in range(128):
            with self.subTest(index=index):
                subprocess.run([str(test_host.OUT / 'dsp_init_probe'), f'altered-{index}'], check=True)

    def test_transfer_requires_the_entire_reference_dma_source_range(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'short-memory'], check=True)

    def test_transfer_copies_all_source_bytes_to_an_independent_loaded_image(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'snapshot'], check=True)

    def test_missing_reference_cannot_authorize_a_fixed_init_transfer(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'no-reference'], check=True)

    def test_transfer_during_an_owned_busy_aram_dma_refuses_without_loading_code(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'active-dma'], check=True)

    def test_changed_loaded_image_cannot_release_the_dsp(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'loaded-altered-release'], check=True)

    def test_changed_loaded_image_cannot_authorize_a_rom_reset_using_unchanged_mem1(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'loaded-altered-reset'], check=True)

    def test_changed_loaded_image_during_execution_cannot_emit_documented_mail(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'loaded-altered-running'], check=True)

    def test_transfer_cannot_overwrite_an_unconsumed_outgoing_mail(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'busy-mail'], check=True)

    def test_init_code_flag_uses_130_existing_timebase_ticks_not_mail_execution_time(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'flag'], check=True)

    def test_init_code_flag_respects_the_existing_fractional_timebase_phase(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'fractional-flag'], check=True)

    def test_loaded_init_cannot_execute_or_emit_mail_while_halted(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'halted'], check=True)

    def test_verified_loaded_init_emits_documented_mail_then_halts_without_audio_or_irq(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'mail'], check=True)

    def test_large_credit_crossing_the_halt_keeps_the_emitted_mail_readable_and_consumable(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'bulk-halt-mail'], check=True)

    def test_transfer_mail_and_halt_traces_report_real_pc_value_and_credited_cycles(self):
        result = subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'mail'], check=True,
                                capture_output=True, text=True)
        self.assertIn('[dsp-init-transfer] main=81000000 bytes=4096 verified=128 init_code_ticks=130 pc=80302F34', result.stderr)
        self.assertIn('[dsp-init-mail] value=80544348 cycles=99978', result.stderr)
        self.assertIn('[dsp-init-halt] cycles=99984', result.stderr)

    def test_halt_pauses_execution_and_resumes_without_recharging_completed_steps(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'pause'], check=True)

    def test_post_init_rom_reset_uses_loaded_identity_not_restored_cpu_scratch(self):
        subprocess.run([str(test_host.OUT / 'dsp_init_probe'), 'reset-restored-scratch'], check=True)


if __name__ == '__main__':
    unittest.main()
