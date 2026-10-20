import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

from vector_fixture import external_vector_fixture

ROOT = Path(__file__).resolve().parents[2]
STACK = Path('/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack')
OUT = ROOT / 'build-f5/services-tests'
FIFO_REGISTERS = {'pi-fifo-base': 4, 'pi-fifo-end': 4, 'pi-fifo-wptr': 4,
                  'cp-fifo-base-low': 2, 'cp-fifo-end-low': 2, 'cp-fifo-wptr-low': 2,
                  'cp-fifo-rptr-low': 2, 'cp-fifo-high-watermark-low': 2,
                  'cp-fifo-low-watermark-low': 2, 'cp-fifo-base-high': 2, 'cp-fifo-end-high': 2,
                  'cp-fifo-wptr-high': 2, 'cp-fifo-rptr-high': 2,
                  'cp-fifo-high-watermark-high': 2, 'cp-fifo-low-watermark-high': 2}


def require_no_vita3k():
    for path in Path('/proc').glob('[0-9]*/comm'):
        try:
            name = path.read_text()
        except FileNotFoundError:
            continue
        if 'vita3k' in name.lower():
            raise RuntimeError(f'Host tests require Vita3K to be stopped: {path.parent.name}')


def without_option_regions(text, macros):
    """The same source with the named option-gated regions removed.

    The default build's output must equal this text's output: an option region
    contributes nothing while its option is off, so a source that differs only
    by such regions and the current default code compiles identically.
    """
    kept = []
    depth = 0
    active = True
    for line in text.splitlines(keepends=True):
        directive = line.strip()
        if depth == 0 and directive.startswith('#if') and any(macro in directive for macro in macros):
            depth = 1
            active = False
            continue
        if depth != 0:
            if directive.startswith('#if') or directive.startswith('#elif'):
                raise ValueError(f'unsupported nested option region: {directive}')
            if directive.startswith('#endif'):
                depth -= 1
                if depth == 0:
                    active = True
            elif directive.startswith('#else') and depth == 1:
                active = not active
            elif active:
                kept.append(line)
            continue
        kept.append(line)
    if depth != 0:
        raise ValueError('unterminated option region')
    return ''.join(kept)


class StartupHostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build()

    @classmethod
    def build(cls, output=None, cmake_options=(), targets=()):
        output = OUT if output is None else output
        generator = output / 'vector-generator'
        subprocess.run([str(STACK / 'bin/cmake'), '-S', str(ROOT / 'vita/feasibility/armv7/generator'),
                        '-B', str(generator), '-G', 'Ninja', f'-DCMAKE_C_COMPILER={os.environ["CC"]}',
                        f'-DCMAKE_MAKE_PROGRAM={STACK}/bin/ninja',
                        f'-DDOLRECOMP_DIR={ROOT}/ref/vita-ww-01/DolRecomp',
                        '-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON'], check=True, capture_output=True)
        require_no_vita3k()
        subprocess.run([str(STACK / 'bin/cmake'), '--build', str(generator),
                        '--target', 'ww_vector_emit', 'ww_ship_fixture_emit', '-j', '1'], check=True, capture_output=True)
        binary = output / 'synthetic-external-vector.bin'
        binary.write_bytes(external_vector_fixture())
        with (output / 'ww05_external_vector.c').open('w') as stream:
            subprocess.run([str(generator / 'ww_vector_emit'), str(binary)], check=True, stdout=stream)
        binary = output / 'synthetic-fp-vector.bin'
        binary.write_bytes(external_vector_fixture(7))
        with (output / 'ww05_fp_vector.c').open('w') as stream:
            subprocess.run([str(generator / 'ww_vector_emit'), str(binary), '00000800'], check=True, stdout=stream)
        binary = output / 'synthetic-decrementer-vector.bin'
        binary.write_bytes(external_vector_fixture(8))
        with (output / 'ww05_decrementer_vector.c').open('w') as stream:
            subprocess.run([str(generator / 'ww_vector_emit'), str(binary), '00000900'], check=True, stdout=stream)
        from test_ship_prolog import fixture
        fixture(output)
        from test_rel_contract import fixture as rel_contract_fixture
        rel_contract_fixture(output)
        with (output / 'ship_translated.c').open('w') as stream:
            subprocess.run([str(generator / 'ww_ship_fixture_emit'), str(output / 'ship.instructions')],
                           check=True, stdout=stream)
        from rel_lifecycle_fixture import fixture as lifecycle_fixture
        lifecycle_fixture(output)
        with (output / 'lifecycle_translated.c').open('w') as stream:
            subprocess.run([str(generator / 'ww_ship_fixture_emit'), str(output / 'lifecycle.instructions')],
                            check=True, stdout=stream)
        from rel_profile_block_fixture import fixture as profile_fixture
        profile_fixture(output)
        with (output / 'profile_translated.c').open('w') as stream:
            subprocess.run([str(generator / 'ww_ship_fixture_emit'), str(output / 'profile.instructions')],
                           check=True, stdout=stream)
        subprocess.run([str(STACK / 'bin/cmake'), '-S', str(ROOT / 'vita/host'), '-B', str(output),
                        '-G', 'Ninja', f'-DCMAKE_C_COMPILER={os.environ["CC"]}',
                        f'-DCMAKE_MAKE_PROGRAM={STACK}/bin/ninja',
                        f'-DRECOMPCORE_DIR={ROOT}/ref/vita-ww-01/RecompCore',
                        '-DWW05_TESTS_ONLY=ON', *cmake_options], check=True, capture_output=True)
        require_no_vita3k()
        with (output / 'build.log').open('w') as log:
            subprocess.run([str(STACK / 'bin/cmake'), '--build', str(output), '-j', '1',
                            *(['--target', *targets] if targets else [])],
                           check=True, stdout=log, stderr=subprocess.STDOUT)

    def test_unknown_mmio_stops_with_context(self):
        subprocess.run([str(OUT / 'host_probe'), 'unknown-mmio'], check=True)

    def test_runtime_pvr_is_preserved(self):
        subprocess.run([str(OUT / 'host_probe'), 'pvr'], check=True)

    def test_fifo_configuration_starts_at_the_sourced_zero_reset_without_events(self):
        for register in FIFO_REGISTERS:
            with self.subTest(register=register):
                subprocess.run([str(OUT / 'host_probe'), f'{register}-reset'], check=True)

    def test_observed_fifo_configuration_store_retains_the_masked_value_through_the_guest_memory_hook(self):
        for register in FIFO_REGISTERS:
            with self.subTest(register=register):
                subprocess.run([str(OUT / 'host_probe'), f'{register}-store'], check=True)

    def test_fifo_configuration_retains_only_the_sourced_register_bit_mask(self):
        for register in FIFO_REGISTERS:
            for value in (0, 0x00429F7F, 0xFFFFFFFF):
                with self.subTest(register=register, value=value):
                    subprocess.run([str(OUT / 'host_probe'), f'{register}-mask-{value}'], check=True)

    def test_fifo_configuration_write_rejects_widths_outside_the_observed_store(self):
        for register, supported_width in FIFO_REGISTERS.items():
            for width in (width for width in (1, 2, 4, 8) if width != supported_width):
                with self.subTest(register=register, width=width):
                    subprocess.run([str(OUT / 'host_probe'), f'{register}-write-width-{width}'], check=True)

    def test_fifo_configuration_read_requires_the_supported_width(self):
        for register, supported_width in FIFO_REGISTERS.items():
            for width in (width for width in (1, 2, 4, 8) if width != supported_width):
                with self.subTest(register=register, width=width):
                    subprocess.run([str(OUT / 'host_probe'), f'{register}-read-width-{width}'], check=True)

    def test_fifo_configuration_write_credits_cycles_without_manufacturing_fifo_or_quantum_allowance(self):
        for register in FIFO_REGISTERS:
            with self.subTest(register=register):
                subprocess.run([str(OUT / 'host_probe'), f'{register}-credit'], check=True)

    def test_fifo_configuration_values_have_independent_retained_storage(self):
        subprocess.run([str(OUT / 'host_probe'), 'pi-fifo-independent'], check=True)

    def test_cp_fifo_halfword_updates_preserve_the_other_half_in_either_write_order(self):
        for register in ('base', 'end', 'wptr', 'rptr', 'hiwater', 'lowater'):
            for order in ('low-first', 'high-first'):
                with self.subTest(register=register, order=order):
                    subprocess.run([str(OUT / 'host_probe'), f'cp-{register}-halves-{order}'], check=True)

    def test_cp_fifo_read_pointer_high_sync_refuses_already_observed_fifo_input(self):
        for action in ('read', 'write'):
            with self.subTest(action=action):
                subprocess.run([str(OUT / 'host_probe'), f'cp-rptr-high-sync-{action}'], check=True)

    def test_observed_disabled_cp_zero_init_does_not_create_fifo_or_interrupt_activity(self):
        for register in ('control', 'distance-low', 'distance-high'):
            with self.subTest(register=register):
                subprocess.run([str(OUT / 'host_probe'), f'cp-{register}-zero'], check=True)

    def test_disabled_cp_zero_store_runs_through_the_normal_guest_halfword_memory_hook(self):
        for register in ('control', 'distance-low', 'distance-high'):
            with self.subTest(register=register):
                subprocess.run([str(OUT / 'host_probe'), f'cp-{register}-store'], check=True)

    def test_cp_enables_and_unobserved_distances_fail_with_the_actual_write_context(self):
        for register in ('control', 'distance-low', 'distance-high'):
            values = (1, 2, 5, 8, 16, 32, 64, 68, 65535)
            if register != 'control':
                values = (*values, 4, 20, 21)
            for value in values:
                with self.subTest(register=register, value=value):
                    subprocess.run([str(OUT / 'host_probe'), f'cp-{register}-value-{value}'], check=True)

    def test_disabled_cp_init_rejects_widths_outside_the_observed_halfword_store(self):
        for register in ('control', 'distance-low', 'distance-high'):
            for width in (1, 4, 8):
                with self.subTest(register=register, width=width):
                    subprocess.run([str(OUT / 'host_probe'), f'cp-{register}-width-{width}'], check=True)

    def test_disabled_cp_zero_store_credits_real_cycles_without_renewing_a_spent_quantum(self):
        for register in ('control', 'distance-low', 'distance-high'):
            with self.subTest(register=register):
                subprocess.run([str(OUT / 'host_probe'), f'cp-{register}-credit'], check=True)

    def test_cp_control_reset_reads_as_zero_without_gp_or_irq_activity(self):
        subprocess.run([str(OUT / 'host_probe'), 'cp-control-reset'], check=True)

    def test_observed_cp_overflow_mask_retains_state_without_gp_fifo_or_irq_activity(self):
        for scenario in ('mask', 'mask-store', 'mask-credit', 'mask-clear'):
            with self.subTest(scenario=scenario):
                subprocess.run([str(OUT / 'host_probe'), f'cp-control-{scenario}'], check=True)

    def test_cp_control_read_rejects_widths_without_altering_the_overflow_mask(self):
        for width in (1, 4, 8):
            with self.subTest(width=width):
                subprocess.run([str(OUT / 'host_probe'), f'cp-control-read-width-{width}'], check=True)

    def test_cp_distance_zero_cannot_discard_already_observed_fifo_input(self):
        for part in ('low', 'high'):
            with self.subTest(part=part):
                subprocess.run([str(OUT / 'host_probe'), f'cp-distance-{part}-queued'], check=True)

    def test_cp_distance_read_is_refused_without_a_fifo_progress_backend(self):
        for part in ('low', 'high'):
            with self.subTest(part=part):
                subprocess.run([str(OUT / 'host_probe'), f'cp-distance-{part}-read'], check=True)

    def test_pi_cold_reset_code_is_stable_without_events_or_time(self):
        subprocess.run([str(OUT / 'host_probe'), 'pi-cold-reset'], check=True)

    def test_actual_sdk_reset_code_load_reads_through_the_memory_hook(self):
        subprocess.run([str(OUT / 'host_probe'), 'pi-reset-load'], check=True)

    def test_pi_reset_write_fails_with_context_without_changing_the_cold_reset_code(self):
        subprocess.run([str(OUT / 'host_probe'), 'pi-reset-write'], check=True)

    def test_pi_reset_read_rejects_widths_outside_the_supported_word_read(self):
        for size in (1, 2, 8):
            with self.subTest(size=size):
                subprocess.run([str(OUT / 'host_probe'), f'pi-reset-width-{size}'], check=True)

    def test_pi_reset_read_credits_executed_cycles_without_renewing_the_quantum(self):
        subprocess.run([str(OUT / 'host_probe'), 'pi-reset-credit'], check=True)

    def test_unknown_spr_fails(self):
        subprocess.run([str(OUT / 'host_probe'), 'unknown-spr'], check=True)

    def test_module_owned_register_restore_edge_does_not_skip_or_synthesize_the_epilogue(self):
        subprocess.run([str(OUT / 'host_probe'), 'epilogue-edge-compiled'], check=True)

    def test_unowned_register_restore_edge_is_still_refused(self):
        subprocess.run([str(OUT / 'host_probe'), 'epilogue-edge-unowned'], check=True)

    def test_module_owned_context_load_edge_preserves_cpu_state_until_the_guest_dispatches(self):
        subprocess.run([str(OUT / 'host_probe'), 'epilogue-edge-context'], check=True)

    def test_unowned_context_load_edge_is_still_refused(self):
        subprocess.run([str(OUT / 'host_probe'), 'epilogue-edge-context-unowned'], check=True)

    def test_unimplemented_desktop_service_edges_are_still_refused(self):
        subprocess.run([str(OUT / 'host_probe'), 'epilogue-edge-service'], check=True)

    def test_vrbox2_draw_edge_preserves_cpu_state_and_requires_explicit_admission(self):
        subprocess.run([str(OUT / 'host_probe'), 'vrbox2-edge-owned'], check=True)

    def test_vrbox2_draw_edge_refuses_invalid_ownership_alias_or_pc(self):
        for shape in ('unowned', 'alias', 'mismatch'):
            with self.subTest(shape=shape):
                subprocess.run([str(OUT / 'host_probe'), f'vrbox2-edge-{shape}'], check=True)

    def test_vrbox2_draw_admission_does_not_admit_other_desktop_edges(self):
        subprocess.run([str(OUT / 'host_probe'), 'vrbox2-edge-unsupported'], check=True)

    def test_owned_observed_retrace_wait_executes_guest_sleep_code_without_a_fake_wakeup(self):
        subprocess.run([str(OUT / 'host_probe'), 'epilogue-edge-retrace'], check=True)

    def test_unowned_retrace_sleep_code_is_not_accepted(self):
        subprocess.run([str(OUT / 'host_probe'), 'epilogue-edge-retrace-unowned'], check=True)

    def test_other_sleep_callers_queues_and_sda_bases_are_still_refused(self):
        for shape in ('bad-caller', 'bad-queue', 'bad-sda', 'alias'):
            with self.subTest(shape=shape):
                subprocess.run([str(OUT / 'host_probe'), 'epilogue-edge-retrace-' + shape], check=True)

    def test_owned_observed_receive_wait_executes_guest_sleep_code_without_a_fake_wakeup(self):
        subprocess.run([str(OUT / 'host_probe'), 'epilogue-edge-receive'], check=True)

    def test_other_receive_callers_queues_and_sda_bases_are_still_refused(self):
        for shape in ('bad-address', 'bad-caller', 'bad-queue', 'bad-sda', 'alias', 'unmapped',
                      'unbacked', 'waiting', 'head', 'tail', 'used', 'unowned', 'unowned-lr'):
            with self.subTest(shape=shape):
                subprocess.run([str(OUT / 'host_probe'), 'epilogue-edge-receive-' + shape], check=True)

    def test_fp_dispatch_credit_refunds_only_valid_prepaid_unexecuted_suffixes(self):
        for shape in ('prepaid', 'precise', 'invalid', 'other'):
            with self.subTest(shape=shape):
                subprocess.run([str(OUT / 'host_probe'), f'fp-credit-{shape}'], check=True)

    def test_observed_sprg0_retains_privileged_words_without_creating_time(self):
        subprocess.run([str(OUT / 'host_probe'), 'sprg-state-272'], check=True)

    def test_actual_vector_sprg_fallbacks_execute_and_advance_normally(self):
        subprocess.run([str(OUT / 'host_probe'), 'sprg-fallback'], check=True)

    def test_sprg_read_requires_supervisor_privilege(self):
        subprocess.run([str(OUT / 'host_probe'), 'sprg-priv-read'], check=True)

    def test_sprg_write_requires_supervisor_privilege(self):
        subprocess.run([str(OUT / 'host_probe'), 'sprg-priv-write'], check=True)

    def test_untranslated_unaliased_mem1_reads_use_real_big_endian_backing(self):
        for size in (1, 2, 4, 8):
            with self.subTest(size=size):
                subprocess.run([str(OUT / 'host_probe'), f'physical-read-{size}'], check=True)

    def test_untranslated_unaliased_mem1_stores_preserve_neighboring_bytes(self):
        for size in (1, 2, 4, 8):
            with self.subTest(size=size):
                subprocess.run([str(OUT / 'host_probe'), f'physical-write-{size}'], check=True)

    def test_physical_mem1_access_is_not_an_invented_mapping_with_data_translation_on(self):
        subprocess.run([str(OUT / 'host_probe'), 'physical-translated'], check=True)

    def test_physical_mem1_cannot_resolve_to_a_synthetic_linked_alias(self):
        subprocess.run([str(OUT / 'host_probe'), 'physical-alias'], check=True)

    def test_physical_mem1_read_cannot_cross_a_partial_linked_alias(self):
        subprocess.run([str(OUT / 'host_probe'), 'physical-partial-alias'], check=True)

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

    def test_synthetic_checkpoint_feed_stops_at_edge(self):
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

    def test_exi_transfer_trace_samples_only_the_first_eight_and_each_thousandth(self):
        result = subprocess.run([str(OUT / 'host_probe'), 'exi-trace-sample'], check=True,
                                capture_output=True, text=True)
        lines = [line for line in result.stderr.splitlines() if line.startswith('[exi]')]
        counts = [int(line.split('transfer=')[1].split()[0]) for line in lines]
        self.assertEqual(counts, [*range(1, 9), 1000])
        self.assertTrue(all('pc=80309E1C' in line for line in lines))

    def test_exi_sample_write_failure_stops_before_sram_or_completion_mutation(self):
        for sample in ('first', 'late', 'payload'):
            with self.subTest(sample=sample):
                subprocess.run([str(OUT / 'host_probe'), 'exi-trace-error-' + sample], check=True,
                               capture_output=True)

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


class TitleContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        StartupHostTest.setUpClass()
        cls.enabled_output = OUT.parent / 'title-enabled'
        StartupHostTest.build(cls.enabled_output, ('-DWW05_ENABLE_TITLE_CONTRACT=ON',),
                              ('rel_lifecycle_probe', 'io_probe'))
        import test_di_read
        test_di_read.DiscReadTest.setUpClass()

    def probe(self, scenario, enabled=True):
        import test_rel_lifecycle
        with patch('test_host.OUT', self.enabled_output if enabled else OUT):
            test_rel_lifecycle.RelLifecycleTest().run_probe(scenario)

    def test_default_off_keeps_the_original_title_refusal(self):
        self.assertIn('WW05_ENABLE_TITLE_CONTRACT:BOOL=OFF', (OUT / 'CMakeCache.txt').read_text())
        self.probe('registered-only', enabled=False)

    def test_enabled_title_uses_registered_certificate_without_test_admission(self):
        self.assertIn('WW05_ENABLE_TITLE_CONTRACT:BOOL=ON', (self.enabled_output / 'CMakeCache.txt').read_text())
        self.probe('title-enabled')

    def test_title_policy_does_not_admit_a_registered_module_401(self):
        self.probe('title-catalog-id')

    def test_title_policy_does_not_admit_a_copy_of_the_registered_certificate(self):
        self.probe('title-copy')

    def test_certificate_bearing_title_reports_disabled_policy(self):
        self.probe('title-disabled', enabled=False)

    def test_startup_registration_failure_stops_before_guest_execution(self):
        import test_io
        for invalid in (False, True):
            with self.subTest(invalid=invalid), patch.dict(os.environ, {'WW05_IO_TITLE_INVALID': '1'}):
                if not invalid:
                    os.environ.pop('WW05_IO_TITLE_INVALID')
                status, report, log = test_io.MainIoTest().run_fixture(output=self.enabled_output, return_log=True)
                self.assertEqual(status, 1)
                self.assertIn('STOP reason=REL certificate set schema, count or identity is invalid', report)
                self.assertIn('edges=0 exception=00000001 cycles=0', report)
                self.assertIn('[host] stage=load initialized=0', log)
                self.assertNotIn('[dispatch]', log)


class IkadaConfigurationTest(unittest.TestCase):
    def test_requested_ikada_requires_each_explicit_prerequisite(self):
        for ship, title in ((False, False), (True, False), (False, True)):
            with self.subTest(ship=ship, title=title):
                output = OUT.parent / f'ikada-invalid-{int(ship)}-{int(title)}'
                result = subprocess.run([str(STACK / 'bin/cmake'), '-S', str(ROOT / 'vita/host'),
                    '-B', str(output), '-G', 'Ninja', f'-DCMAKE_C_COMPILER={os.environ["CC"]}',
                    f'-DCMAKE_MAKE_PROGRAM={STACK}/bin/ninja',
                    f'-DRECOMPCORE_DIR={ROOT}/ref/vita-ww-01/RecompCore', '-DWW05_TESTS_ONLY=ON',
                    '-DWW05_ENABLE_IKADA_CONTRACT=ON', f'-DWW05_ENABLE_SHIP_PROLOG={"ON" if ship else "OFF"}',
                    f'-DWW05_ENABLE_TITLE_CONTRACT={"ON" if title else "OFF"}'], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('Ikada admission requires explicit ship and title options', result.stderr)


class IkadaStartupTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output = OUT.parent / 'ikada-enabled'
        StartupHostTest.build(cls.output, ('-DWW05_ENABLE_SHIP_PROLOG=ON',
            '-DWW05_ENABLE_TITLE_CONTRACT=ON', '-DWW05_ENABLE_IKADA_CONTRACT=ON'), ('io_probe', 'rel_lifecycle_probe'))

    def test_valid_startup_admits_the_exact_registered_ikada_before_dispatch(self):
        import test_io
        with patch.dict(os.environ, {'WW05_IO_IKADA': 'valid'}):
            status, report, log = test_io.MainIoTest().run_fixture(output=self.output, return_log=True)
        self.assertEqual(status, 0, log)
        self.assertIn('[rel-admission] module=60', log)
        self.assertIn('[host] stage=load initialized=1', log)
        self.assertLess(log.index('[rel-admission]'), log.index('[dispatch]'))

    def test_requested_invalid_startup_stops_before_guest_dispatch(self):
        import test_io
        for mode in ('missing-set', 'missing-60', 'duplicate', 'invalid-certificate', 'bad-dol'):
            with self.subTest(mode=mode), patch.dict(os.environ, {'WW05_IO_IKADA': mode}):
                status, report, log = test_io.MainIoTest().run_fixture(output=self.output, return_log=True)
                self.assertEqual(status, 1, log)
                self.assertIn('edges=0 exception=00000001 cycles=0', report)
                self.assertIn('[host] stage=load initialized=0', log)
                self.assertNotIn('[rel-admission] module=60', log)
                self.assertNotIn('[dispatch]', log)

    def test_startup_admission_receipt_failure_is_fatal_before_guest_dispatch(self):
        import test_io
        with patch.dict(os.environ, {'WW05_IO_IKADA': 'valid'}):
            status, report, log = test_io.MainIoTest().run_fixture(
                failure='ikada-admission-log', output=self.output, return_log=True)
        self.assertEqual(status, 1, log)
        self.assertIn('STOP reason=ikada admission receipt write failed', report)
        self.assertNotIn('[dispatch]', log)


if __name__ == '__main__':
    unittest.main()
