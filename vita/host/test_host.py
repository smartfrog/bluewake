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

    def test_unknown_mmio_write_fails(self):
        subprocess.run([str(OUT / 'host_probe'), 'unknown-write'], check=True)

    def test_mi_interrupt_mask_is_retained(self):
        subprocess.run([str(OUT / 'host_probe'), 'mi-mask'], check=True)

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

    def test_si_transfer_is_reported_without_fabricating_completion(self):
        subprocess.run([str(OUT / 'host_probe'), 'si-transfer'], check=True)

    def test_invalid_module_is_rejected_before_dol_io(self):
        subprocess.run([str(OUT / 'host_probe'), 'module'], check=True)

    def test_allocation_failures_release_earlier_backings(self):
        for allocation in (1, 2, 3):
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
