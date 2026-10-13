import subprocess
import unittest

import test_host


class DspRomTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'dsp_init_probe'), scenario], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stderr

    def test_running_reset_selected_ROM_consumes_the_actual_command_and_masked_parameters_without_reply_or_time(self):
        trace = self.probe('rom-config')
        self.assertEqual(trace.count('[dsp-rom-mail]'), 8)
        self.assertIn('source=00399420 bytes=7456 dmem_bytes=0 imem=0100', trace)

    def test_task_launch_refuses_before_code_execution_or_a_ready_mail_is_fabricated(self):
        self.assertIn('[dsp-rom-task-request]', self.probe('rom-launch'))

    def test_reset_discards_the_actual_partial_ROM_upload_configuration(self):
        self.probe('rom-reset')

    def test_unconsumed_ROM_ready_mail_is_not_overwritten_by_an_incoming_command(self):
        self.probe('rom-unconsumed-ready')

    def test_running_non_ROM_execution_does_not_silently_accept_a_ROM_command(self):
        self.probe('rom-unselected')

    def test_unknown_ROM_command_is_an_explicit_refusal_not_a_synthetic_error_mail(self):
        self.probe('rom-unknown-command')

    def test_failed_private_trace_write_does_not_consume_the_mail(self):
        self.probe('rom-trace-failure')

    def test_failed_parameter_trace_does_not_latch_data_or_clear_the_expected_command(self):
        self.probe('rom-parameter-trace-failure')

    def test_refused_init_transfer_preserves_the_real_ROM_upload_configuration(self):
        self.probe('rom-invalid-init')

    def test_explicit_deferred_bootstrap_produces_only_the_two_declared_synthetic_words_and_one_DSP_interrupt(self):
        self.assertIn('synthetic_mail=DCD10000,F3551111 synthetic_irq=0080', self.probe('rom-deferred'))

    def test_guest_interrupt_acknowledgement_before_mail_consumption_does_not_lose_or_reissue_the_handshake(self):
        self.probe('rom-deferred-ack-first')

    def test_deferred_bootstrap_trace_failure_does_not_publish_synthetic_state(self):
        self.probe('rom-deferred-trace')

    def test_post_bootstrap_audio_task_commands_remain_explicit_refusals_not_silent_completions(self):
        self.probe('rom-deferred-command')

    def test_deferred_stub_cannot_acknowledge_an_incomplete_or_unbounded_task_configuration(self):
        for invalid in range(7):
            with self.subTest(invalid=invalid):
                self.probe(f'rom-deferred-invalid-{invalid}')

    def test_original_registered_nonwaiting_probe_returns_the_retained_busy_clear_mail_without_new_state(self):
        self.probe('rom-deferred-empty-original')

    def test_empty_status_query_cannot_bypass_the_guard_with_changed_or_unregistered_caller_code(self):
        for scenario in ('rom-deferred-empty-changed', 'rom-deferred-empty-unregistered'):
            with self.subTest(scenario=scenario):
                self.probe(scenario)

    def test_a_genuine_mail_waiter_is_not_misclassified_as_the_nonwaiting_post_consumption_query(self):
        self.probe('rom-deferred-empty-waiter')

    def test_deferred_setup_emits_a_declared_metadata_ack_only_after_five_discarded_words_without_touching_audio_memory(self):
        trace = self.probe('rom-deferred-setup')
        self.assertIn('mail=81000040', trace)
        self.assertEqual(trace.count('event=setup-word'), 5)
        self.assertIn('synthetic_mail=DCD10004,F3558100', trace)

    def test_deferred_setup_ack_tag_is_derived_from_the_received_command_not_a_fixed_game_reply(self):
        self.assertIn('synthetic_mail=DCD10004,F35581AB', self.probe('rom-deferred-setup-ack-tag'))

    def test_setup_irq_ack_before_mail_consumption_does_not_drop_the_second_word_or_retrigger_an_irq(self):
        self.probe('rom-deferred-setup-ack-first')

    def test_setup_irq_is_delivered_only_when_the_guest_enables_its_DSP_mask(self):
        self.probe('rom-deferred-setup-ack-masked')

    def test_failed_setup_ack_trace_does_not_consume_the_last_word_or_publish_a_synthetic_reply(self):
        self.probe('rom-deferred-setup-ack-trace')

    def test_setup_ack_cannot_overwrite_an_unconsumed_outgoing_mail(self):
        self.probe('rom-deferred-setup-ack-busy')

    def test_reset_discards_the_pending_setup_ack_without_replaying_it_over_the_ROM_ready_mail(self):
        self.probe('rom-deferred-setup-ack-reset')

    def test_deferred_input_assertion_is_a_declared_no_op_not_a_new_CPU_interrupt(self):
        self.assertIn('event=assert-to-no-op', self.probe('rom-deferred-setup-assert'))

    def test_audio_processing_packet_types_are_not_silently_discarded_as_setup(self):
        self.probe('rom-deferred-setup-rejected-kind')

    def test_setup_input_trace_failures_preserve_the_partial_intake_without_a_processing_ack(self):
        for scenario in ('rom-deferred-setup-count-trace', 'rom-deferred-setup-word-trace'):
            with self.subTest(scenario=scenario):
                self.probe(scenario)

    def test_a_second_setup_list_is_not_implicitly_accepted_by_the_one_bootstrap_no_op(self):
        self.probe('rom-deferred-setup-repeat')
