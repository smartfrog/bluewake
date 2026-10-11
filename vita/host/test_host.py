import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
STACK = Path('/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack')
OUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f5/services-tests')


class StartupHostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run([str(STACK / 'bin/cmake'), '-S', str(ROOT / 'vita/host'), '-B', str(OUT),
                        '-G', 'Ninja', f'-DCMAKE_C_COMPILER={os.environ["CC"]}',
                        f'-DCMAKE_MAKE_PROGRAM={STACK}/bin/ninja',
                        f'-DRECOMPCORE_DIR={ROOT}/ref/vita-ww-01/RecompCore',
                        '-DWW05_TESTS_ONLY=ON'], check=True, capture_output=True)
        with (OUT / 'build.log').open('w') as log:
            subprocess.run([str(STACK / 'bin/cmake'), '--build', str(OUT), '-j', '2'],
                           check=True, stdout=log, stderr=subprocess.STDOUT)

    def test_unknown_mmio_stops_with_context(self):
        subprocess.run([str(OUT / 'host_probe'), 'unknown-mmio'], check=True)

    def test_runtime_pvr_is_preserved(self):
        subprocess.run([str(OUT / 'host_probe'), 'pvr'], check=True)

    def test_unknown_spr_fails(self):
        subprocess.run([str(OUT / 'host_probe'), 'unknown-spr'], check=True)

    def test_l2_control_resets_disabled_without_an_instruction_skip(self):
        subprocess.run([str(OUT / 'host_probe'), 'l2-reset'], check=True)

    def test_l2_enable_control_is_retained(self):
        subprocess.run([str(OUT / 'host_probe'), 'l2-enable'], check=True)

    def test_empty_coherent_l2_invalidation_preserves_guest_memory(self):
        subprocess.run([str(OUT / 'host_probe'), 'l2-invalidate-empty'], check=True)

    def test_unimplemented_l2_control_flags_fail(self):
        subprocess.run([str(OUT / 'host_probe'), 'l2-unsupported'], check=True)

    def test_l2_write_requires_supervisor_privilege(self):
        subprocess.run([str(OUT / 'host_probe'), 'l2-privilege'], check=True)

    def test_timebase_uses_shared_cycle_domain(self):
        subprocess.run([str(OUT / 'host_probe'), 'clock'], check=True)

    def test_aram_configuration_registers_follow_the_pinned_reset_image(self):
        subprocess.run([str(OUT / 'host_probe'), 'aram-config-defaults'], check=True)

    def test_aram_info_masks_reserved_bits_without_changing_dsp_control(self):
        subprocess.run([str(OUT / 'host_probe'), 'aram-info-mask'], check=True)

    def test_aram_refresh_retains_only_the_pinned_register_mask(self):
        subprocess.run([str(OUT / 'host_probe'), 'aram-refresh-mask'], check=True)

    def test_aram_mode_is_read_only(self):
        subprocess.run([str(OUT / 'host_probe'), 'aram-mode-read-only'], check=True)

    def test_unsupported_aram_configuration_width_preserves_state_and_error_context(self):
        subprocess.run([str(OUT / 'host_probe'), 'aram-config-width'], check=True)

    def test_decrementer_starts_at_the_reference_reset_value_and_counts(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-reset'], check=True)

    def test_decrementer_spr_instruction_fallbacks_read_and_write_the_counter(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-fallback'], check=True)

    def test_decrementer_and_timebase_share_fractional_clock_ticks(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-fractional'], check=True)

    def test_decrementer_expires_on_zero_to_negative_not_arrival_at_zero(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-threshold'], check=True)

    def test_decrementer_wraps_and_latches_across_more_than_one_counter_period(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-wrap'], check=True)

    def test_decrementer_positive_to_negative_write_latches_an_exception(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-negative-write'], check=True)

    def test_decrementer_negative_to_negative_write_does_not_latch_an_exception(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-negative-rewrite'], check=True)

    def test_rewriting_decrementer_does_not_acknowledge_an_existing_exception(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-positive-rewrite'], check=True)

    def test_decrementer_deadline_limits_dispatch_without_changing_the_clock_rate(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-deadline'], check=True)

    def test_decrementer_exception_is_deferred_until_msr_ee_is_enabled(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-disabled'], check=True)

    def test_completed_edge_credits_all_executed_cycles_without_a_stale_observation_suffix(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-edge-charge'], check=True)

    def test_edge_clock_observation_preserves_the_remaining_dispatch_quantum(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-turn-credit'], check=True)

    def test_single_cycle_mmio_poll_cannot_renew_an_exhausted_dispatch_quantum(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-mmio-quantum'], check=True)

    def test_exhausted_mmio_quantum_allows_a_fresh_turn_without_duplicate_cycle_credit(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-mmio-next-turn'], check=True)

    def test_edge_progress_exposes_timer_state_without_an_outer_dispatch_return(self):
        result = subprocess.run([str(OUT / 'host_probe'), 'dec-disabled'], check=True, capture_output=True, text=True)
        self.assertIn('[edge-progress]', result.stderr)
        self.assertIn('dec=FFFFFFFF pending=1 ee=0', result.stderr)

    def test_decrementer_vector_and_rfi_do_not_depend_on_a_pi_cause_or_mask(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-rfi'], check=True)

    def test_external_interrupt_has_priority_over_a_pending_decrementer(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-external-priority'], check=True)

    def test_decrementer_does_not_overwrite_an_existing_guest_exception(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-prior-fault'], check=True)

    def test_zero_charge_yields_do_not_invent_timer_ticks(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-zero-charge'], check=True)

    def test_decrementer_read_requires_supervisor_privilege(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-read-privilege'], check=True)

    def test_decrementer_write_requires_supervisor_privilege(self):
        subprocess.run([str(OUT / 'host_probe'), 'dec-write-privilege'], check=True)

    def test_fifo_frontier_stops_at_edge(self):
        subprocess.run([str(OUT / 'host_probe'), 'frontier'], check=True)

    def test_unknown_instruction_is_not_skipped(self):
        subprocess.run([str(OUT / 'host_probe'), 'instruction'], check=True)

    def test_compiled_guest_offers_are_not_host_services(self):
        for alias in ('cached', 'physical', 'uncached'):
            with self.subTest(alias=alias):
                subprocess.run([str(OUT / 'host_probe'), 'guest-offer-' + alias], cwd=OUT, check=True)

    def test_unknown_host_service_still_fails(self):
        subprocess.run([str(OUT / 'host_probe'), 'unknown-host-call'], cwd=OUT, check=True)

    def test_instruction_cache_invalidation_accepts_unchanged_compiled_text(self):
        for alias in ('cached', 'physical', 'uncached'):
            with self.subTest(alias=alias):
                subprocess.run([str(OUT / 'host_probe'), 'icbi-text-' + alias], cwd=OUT, check=True)

    def test_instruction_cache_invalidation_accepts_data(self):
        subprocess.run([str(OUT / 'host_probe'), 'icbi-data'], cwd=OUT, check=True)

    def test_instruction_cache_invalidation_refuses_changed_compiled_code(self):
        subprocess.run([str(OUT / 'host_probe'), 'icbi-modified-text'], cwd=OUT, check=True)

    def test_data_cache_operations_preserve_coherent_memory(self):
        for operation in (54, 86, 470):
            for addressing in ('zero-base', 'indexed'):
                with self.subTest(operation=operation, addressing=addressing):
                    subprocess.run([str(OUT / 'host_probe'), f'data-cache-{operation}-{addressing}'], check=True)

    def test_data_cache_invalidation_requires_supervisor_privilege(self):
        subprocess.run([str(OUT / 'host_probe'), 'dcbi-privilege'], check=True)

    def test_unknown_mmio_write_fails(self):
        subprocess.run([str(OUT / 'host_probe'), 'unknown-write'], check=True)

    def test_mi_interrupt_mask_is_retained(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-mask'], check=True)

    def test_mi_init_registers_have_the_pinned_zero_reset_image(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-init-reset'], check=True)

    def test_mi_unknown1_zero_init_is_retained_without_invented_events_or_time(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-init-zero'], check=True)

    def test_mi_unrestricted_protection_configuration_is_retained_without_interrupts(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-init-unrestricted'], check=True)

    def test_nonzero_mi_unknown1_requires_documented_behavior_instead_of_a_guess(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-init-bad-unknown'], check=True)

    def test_restrictive_mi_protection_cannot_succeed_without_a_protection_backend(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-init-bad-protection'], check=True)

    def test_mi_unknown1_wrong_width_preserves_state_and_actual_failure_context(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-init-bad-unknown-width'], check=True)

    def test_mi_protection_wrong_width_preserves_state_and_actual_failure_context(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-init-bad-protection-width'], check=True)

    def test_unknown_mi_mask_bits_fail(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-invalid-mask'], check=True)

    def test_mi_interrupt_mask_requires_halfword_access(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-width'], check=True)

    def test_dsp_is_stopped_and_waiting_for_initialization(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-stopped'], check=True)

    def test_dsp_irq_masks_preserve_stopped_state(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-mask'], check=True)

    def test_dsp_acknowledgements_do_not_create_interrupts(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-ack'], check=True)

    def test_dsp_reset_requires_a_real_backend(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-reset'], check=True)

    def test_dsp_initialization_release_requires_a_real_backend(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-release'], check=True)

    def test_dsp_control_requires_halfword_access(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-width'], check=True)

    def test_ai_control_starts_with_the_pinned_stopped_clock(self):
        subprocess.run([str(OUT / 'host_probe'), 'ai-stopped'], check=True)

    def test_ai_interrupt_mask_is_retained(self):
        subprocess.run([str(OUT / 'host_probe'), 'ai-mask'], check=True)

    def test_ai_acknowledgements_do_not_create_interrupts(self):
        subprocess.run([str(OUT / 'host_probe'), 'ai-ack'], check=True)

    def test_ai_stopped_counter_reset_clears_its_command_bit(self):
        subprocess.run([str(OUT / 'host_probe'), 'ai-reset-counter'], check=True)

    def test_ai_stream_start_and_rate_changes_require_an_audio_backend(self):
        for operation in ('start', 'rate'):
            with self.subTest(operation=operation):
                subprocess.run([str(OUT / 'host_probe'), 'ai-' + operation], check=True)

    def test_ai_control_requires_word_access(self):
        subprocess.run([str(OUT / 'host_probe'), 'ai-width'], check=True)

    def test_empty_si_bus_completes_with_no_response_and_no_pad_data(self):
        for channel in range(4):
            with self.subTest(channel=channel):
                subprocess.run([str(OUT / 'host_probe'), f'si-empty-{channel}'], check=True)

    def test_verified_init_allows_a_halted_dsp_reset_without_creating_time_or_audio(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-fixed-reset'], check=True)

    def test_each_changed_init_byte_refuses_reset_before_any_mail(self):
        for index in range(128):
            with self.subTest(index=index):
                subprocess.run([str(OUT / 'host_probe'), f'dsp-fixed-altered-{index}'], check=True)

    def test_rom_mail_is_visible_only_after_halt_release_and_low_read_clears_its_latch(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-fixed-rom-mail'], check=True)

    def test_halted_cpu_mailbox_uses_documented_high_low_latch_semantics(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-fixed-halted-cpu-mail'], check=True)

    def test_unimplemented_running_rom_mail_is_not_consumed_or_answered(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-fixed-running-cpu-mail'], check=True)

    def test_queued_unimplemented_rom_mail_prevents_release_instead_of_stalling_silently(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-fixed-queued-cpu-mail'], check=True)

    def test_changed_init_after_reset_cannot_release_the_dsp_or_obtain_mail(self):
        subprocess.run([str(OUT / 'host_probe'), 'dsp-fixed-changed-after-reset'], check=True)

    def test_empty_si_bus_errors_and_interrupts_can_be_acknowledged(self):
        subprocess.run([str(OUT / 'host_probe'), 'si-empty-ack'], check=True)

    def test_si_transfer_requires_word_access(self):
        subprocess.run([str(OUT / 'host_probe'), 'si-width'], check=True)

    def test_exi_transfer_retains_transaction_context(self):
        result = subprocess.run([str(OUT / 'host_probe'), 'exi-context'], check=True, capture_output=True, text=True)
        self.assertIn('channel=0 chip_select=2 frequency_code=3 direction=1 length=4 dma=0', result.stderr)
        self.assertIn('dma_address=00000000 immediate=20000100 pc=80309E1C', result.stderr)

    def test_exi_sram_dma_reads_shared_defaults_and_publishes_completion(self):
        subprocess.run([str(OUT / 'host_probe'), 'exi-sram-dma'], check=True)

    def test_exi_sram_immediate_read_uses_the_same_shared_device(self):
        subprocess.run([str(OUT / 'host_probe'), 'exi-sram-immediate'], check=True)

    def test_exi_deselection_resets_the_shared_sram_command(self):
        subprocess.run([str(OUT / 'host_probe'), 'exi-sram-reselect'], check=True)

    def test_exi_sram_writes_are_retained_only_in_the_private_session(self):
        subprocess.run([str(OUT / 'host_probe'), 'exi-sram-write'], check=True)

    def test_exi_unsupported_ipl_regions_do_not_return_fabricated_zero_bytes(self):
        subprocess.run([str(OUT / 'host_probe'), 'exi-rom'], check=True)

    def test_exi_sram_dma_rejects_unmapped_guest_memory(self):
        subprocess.run([str(OUT / 'host_probe'), 'exi-bad-dma'], check=True)

    def test_exi_sram_rejects_transfers_beyond_its_shared_image(self):
        subprocess.run([str(OUT / 'host_probe'), 'exi-bad-range'], check=True)

    def test_si_completion_delivers_external_exception_at_next_edge(self):
        subprocess.run([str(OUT / 'host_probe'), 'irq-si'], check=True)

    def test_di_completion_uses_the_same_external_exception_gate(self):
        subprocess.run([str(OUT / 'host_probe'), 'irq-di'], check=True)

    def test_pi_mask_defers_external_exception(self):
        subprocess.run([str(OUT / 'host_probe'), 'irq-masked'], check=True)

    def test_msr_ee_defers_external_exception_until_reenabled(self):
        subprocess.run([str(OUT / 'host_probe'), 'irq-disabled'], check=True)

    def test_acknowledged_si_interrupt_does_not_recur_after_rfi(self):
        subprocess.run([str(OUT / 'host_probe'), 'irq-ack-rfi'], check=True)

    def test_external_interrupt_does_not_replace_a_guest_fault(self):
        subprocess.run([str(OUT / 'host_probe'), 'irq-prior-exception'], check=True)

    def test_invalid_module_is_rejected_before_dol_io(self):
        subprocess.run([str(OUT / 'host_probe'), 'module'], check=True)

    def test_allocation_failures_release_earlier_backings(self):
        for allocation in (1, 2, 3, 4):
            with self.subTest(allocation=allocation):
                subprocess.run([str(OUT / 'host_probe'), f'allocation-{allocation}'], check=True)

    def test_relocated_data_and_bss_have_one_owner_and_cleanup(self):
        subprocess.run([str(OUT / 'host_probe'), 'aliases'], cwd=OUT, check=True)

    def test_alias_allocation_failure_is_propagated(self):
        subprocess.run([str(OUT / 'host_probe'), 'alias-failure'], cwd=OUT, check=True)

    def test_partial_code_snapshot_allocation_is_released_on_failure(self):
        subprocess.run([str(OUT / 'host_probe'), 'code-copy-failure'], cwd=OUT, check=True)


if __name__ == '__main__':
    unittest.main()
