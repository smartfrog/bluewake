import subprocess
import unittest

import test_host


class AiVolumeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'retained_register_probe'), scenario, '0xCC006C04'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_stopped_volume_reset_is_the_sourced_zero_without_time_or_audio_progress(self):
        self.probe('reset')

    def test_stopped_volume_retains_the_exact_written_word_including_reserved_bits(self):
        for value in (0, 0xFF, 0xFF00, 0xA55AFF34, 0xFFFFFFFF):
            with self.subTest(value=value):
                self.probe(f'value-{value}')

    def test_volume_storage_is_independent_of_audio_control_rate_counter_and_interrupt_state(self):
        self.probe('independent')

    def test_volume_access_credits_only_actual_executed_cycles_without_new_budget(self):
        self.probe('credit')

    def test_volume_read_rejects_widths_other_than_the_observed_word(self):
        for width in (1, 2, 3, 8):
            with self.subTest(width=width):
                self.probe(f'read-width-{width}')

    def test_volume_write_rejects_widths_other_than_the_observed_word_without_mutation(self):
        for width in (1, 2, 3, 8):
            with self.subTest(width=width):
                self.probe(f'write-width-{width}')

    def test_active_stream_volume_change_requires_a_real_audio_backend(self):
        self.probe('audio-running')

    def test_active_audio_dma_volume_change_is_not_silently_accepted_without_a_backend(self):
        self.probe('audio-dma-running')
