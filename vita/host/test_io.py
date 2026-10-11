from pathlib import Path
import os
import struct
import subprocess
import tempfile
import unittest

import test_host

OUT = test_host.OUT


class MainIoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def run_fixture(self, failure='', stale=False, interrupt=None, exception=False, syscall=None, progress=False,
                    dispatch_trace=None, stale_dispatch=False, mmio_poll=False, aram_dma=False, dsp_init=False,
                    cpu_boundary=None):
        with tempfile.TemporaryDirectory(dir=OUT, prefix='io-') as directory:
            root = Path(directory)
            data = bytearray(260)
            for offset, value in ((0, 256), (0x48, 0x80001000), (0x90, 4),
                                  (0xD8, 0x80002000), (0xDC, 256), (0xE0, 0x80001000)):
                struct.pack_into('>I', data, offset, value)
            if syscall is not None:
                data.extend(bytes(range(1, 29)))
                for offset, value in ((4, 260), (0x4C, 0x803077D0), (0x94, 28)):
                    struct.pack_into('>I', data, offset, value)
            (root / 'synthetic.dol').write_bytes(data)
            (root / 'ww05.request').write_text(f'{"c" * 32} {"a" * 40} {"b" * 64}\n')
            if stale:
                (root / 'ww05.report').write_text('preserve me')
            if stale_dispatch:
                (root / 'ww05.dispatch').write_text('preserve trace')
            environment = dict(os.environ, WW05_IO_FAILURE=failure)
            if interrupt is not None:
                environment['WW05_IO_IRQ'] = interrupt
            if exception:
                environment['WW05_IO_EXCEPTION'] = '1'
            if syscall is not None:
                environment['WW05_IO_SYSCALL'] = syscall
            if mmio_poll:
                environment['WW05_IO_MMIO_POLL'] = '1'
            if aram_dma:
                environment['WW05_IO_ARAM_DMA'] = aram_dma if isinstance(aram_dma, str) else '1'
            if dsp_init:
                environment['WW05_IO_DSP_INIT'] = '1'
            if cpu_boundary is not None:
                environment['WW05_IO_CPU_BOUNDARY'] = cpu_boundary
            result = subprocess.run([str(OUT / 'io_probe')], cwd=root,
                                    env=environment, capture_output=True, timeout=10)
            report = (root / 'ww05.report').read_text() if (root / 'ww05.report').exists() else None
            if progress:
                self.assertIn('[progress] turns=1', result.stderr.decode())
            if cpu_boundary is not None:
                pc = '80328F84' if cpu_boundary == 'restore' else '80303A50'
                self.assertIn(f'[dispatch] stage=enter turn=2 pc={pc}', result.stderr.decode())
                self.assertIn('[dispatch] stage=return turn=2 pc=80001004', result.stderr.decode())
            if syscall == 'valid':
                self.assertIn('[exception-handoff] stage=guard', result.stderr.decode())
                self.assertIn('[exception-handoff] stage=guard-result matched=1', result.stderr.decode())
            if syscall == 'trace-window':
                self.assertIn('[dispatch] stage=enter turn=2 pc=803077D0', result.stderr.decode())
                self.assertIn('[dispatch] stage=return turn=2 pc=80001004', result.stderr.decode())
            if syscall == 'yielding-handler' and not failure and not stale_dispatch:
                for stage in ('returned', 'checked', 'edge-enter', 'edge-return', 'turn-enter',
                              'turn-begun', 'dispatch-ready'):
                    self.assertIn(f'[handler-boundary] stage={stage} pc=803077D4', result.stderr.decode())
            if exception:
                log = result.stderr.decode()
                self.assertIn('[exception] bits=00000008 pc=00000C00 srr0=80001004', log)
                self.assertIn('[exception-vector] address=00000C00', log)
            if interrupt == 'decrementer':
                self.assertIn('[decrementer] stage=delivered vector=00000900', result.stderr.decode())
            if interrupt == 'vector' and not failure:
                self.assertIn('[exception-vector-dispatch] vector=00000500 pc=00000500 bytes=152 matched=1', result.stderr.decode())
                self.assertIn('[exception-vector-return] input=00000510 pc=80001004', result.stderr.decode())
            if interrupt == 'fp-vector' and not failure:
                self.assertIn('[fp-exception-resume] vector=00000800 matched=1 srr0=80001004', result.stderr.decode())
                self.assertIn('[exception-vector-return] input=00000810 pc=80001004', result.stderr.decode())
            if interrupt == 'unsupported' and not failure:
                log = result.stderr.decode()
                self.assertIn('[exception] bits=00000001 pc=00000500 srr0=80001004', log)
                self.assertIn('pi_cause=00010008 pi_mask=00000008', log)
                self.assertIn('[exception-vector] address=00000500 bytes=256 hex=' + bytes(range(256)).hex().upper(), log)
            if dispatch_trace is not None:
                path = root / 'ww05.dispatch'
                self.assertEqual(path.read_text() if path.exists() else None, dispatch_trace)
            if syscall == 'yielding-handler' and not failure and not stale_dispatch:
                self.assertIn('[edge] pc=803077DC', result.stderr.decode())
                self.assertIn('[spr-fallback] stage=enter pc=803077D8 spr=1008', result.stderr.decode())
                self.assertIn('[spr-fallback] stage=return pc=803077DC spr=1008', result.stderr.decode())
            if syscall == 'repeated-handlers':
                log = result.stderr.decode()
                self.assertEqual(log.count('[exception-resume]'), 2)
                self.assertIn('resume=1 srr0=80001004', log)
                self.assertIn('resume=1000 srr0=80001004', log)
                self.assertLess(log.count('[dispatch] stage=enter'), 12)
            return result.returncode, report

    def test_synthetic_host_fixture_publishes_closed_report(self):
        status, report = self.run_fixture()
        self.assertEqual(status, 0)
        self.assertIn('platform=HOST', report)
        self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_register_restore_boundary_reaches_the_module_dispatcher_without_eager_register_or_pc_changes(self):
        status, report = self.run_fixture(cpu_boundary='restore')
        self.assertEqual(status, 0)
        self.assertIn('cycles=20\n', report)
        self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_context_load_boundary_reaches_the_module_dispatcher_and_uses_its_actual_ram_loads_and_rfi(self):
        status, report = self.run_fixture(cpu_boundary='context')
        self.assertEqual(status, 0)
        self.assertIn('cycles=19\n', report)
        self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_expansion_request_log_failure_remains_an_explicit_failed_guest_verdict(self):
        status, report = self.run_fixture(aram_dma='hsp', failure='aram-expansion-log')
        self.assertNotEqual(status, 0)
        self.assertIn('STOP reason=ARAM expansion request trace write failed', report)
        self.assertTrue(report.endswith('END frontier=FAIL live=0 rendered=0\n'))

    def test_absent_hsp_trace_failure_cannot_complete_an_unrecorded_transfer(self):
        status, report = self.run_fixture(aram_dma='absent-hsp', failure='aram-absent-log')
        self.assertNotEqual(status, 0)
        self.assertIn('STOP reason=ARAM absent HSP trace write failed', report)
        self.assertTrue(report.endswith('END frontier=FAIL live=0 rendered=0\n'))

    def test_mmio_quantum_returns_through_the_real_chassis_before_a_fresh_dispatch(self):
        status, report = self.run_fixture(mmio_poll=True)
        self.assertEqual(status, 0)
        self.assertIn('cycles=268\n', report)

    def test_aram_poll_continues_only_after_the_credited_dma_deadline(self):
        status, report = self.run_fixture(aram_dma=True)
        self.assertEqual(status, 0)
        self.assertIn('cycles=258\n', report)
        self.assertIn('aram=16777216', report)
        self.assertIn('END frontier=PASS live=0', report)

    def test_dsp_init_mail_poll_yields_through_the_real_chassis_until_credited_execution_completes(self):
        status, report = self.run_fixture(dsp_init=True)
        self.assertEqual(status, 0)
        self.assertIn('cycles=98430\n', report)
        self.assertIn('END frontier=PASS live=0', report)
        self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_synthetic_interrupt_yields_resume_the_main_dispatch_loop(self):
        for boundary in ('inside', 'outside'):
            with self.subTest(boundary=boundary):
                status, report = self.run_fixture(interrupt=boundary)
                self.assertEqual(status, 0)
                self.assertIn('pc=80001004', report)
                self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_verified_generated_vector_runs_through_the_real_main_loop_before_the_host_only_fifo_fixture(self):
        status, report = self.run_fixture(interrupt='vector')
        self.assertEqual(status, 0)
        self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_generated_vector_dispatch_log_failure_prevents_execution_and_closed_verdict(self):
        status, report = self.run_fixture(interrupt='vector', failure='external-dispatch-log')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_generated_vector_return_log_failure_cannot_publish_a_closed_verdict(self):
        status, report = self.run_fixture(interrupt='vector', failure='external-return-log')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_real_lazy_fp_exception_runs_the_verified_fp_vector_without_eager_fp_or_numeric_changes(self):
        status, report = self.run_fixture(interrupt='fp-vector')
        self.assertEqual(status, 0)
        self.assertIn('cycles=41\n', report)
        self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_fp_exception_handoff_log_failure_cannot_publish_a_successful_guest_verdict(self):
        status, report = self.run_fixture(interrupt='fp-vector', failure='fp-resume-log')
        self.assertNotEqual(status, 0)
        self.assertIn('STOP reason=FP exception handoff log write failed', report)
        self.assertTrue(report.endswith('END frontier=FAIL live=0 rendered=0\n'))

    def test_fp_vector_dispatch_log_failure_cannot_publish_a_closed_verdict(self):
        status, report = self.run_fixture(interrupt='fp-vector', failure='external-dispatch-log')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_unsupported_external_vector_logs_real_saved_state_and_bytes_without_replacing_the_first_failure(self):
        status, report = self.run_fixture(interrupt='unsupported')
        self.assertNotEqual(status, 0)
        self.assertIn('STOP reason=unsupported host call address=00000500 pc=00000500', report)
        self.assertIn('cycles=24\n', report)
        self.assertTrue(report.endswith('END frontier=FAIL live=0 rendered=0\n'))

    def test_external_failure_state_log_error_cannot_publish_a_closed_guest_verdict(self):
        status, report = self.run_fixture(interrupt='unsupported', failure='external-state-log')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_external_vector_log_error_cannot_publish_a_closed_guest_verdict(self):
        status, report = self.run_fixture(interrupt='unsupported', failure='external-vector-log')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_unhandled_system_call_is_not_mislabelled_as_an_fp_defect(self):
        status, report = self.run_fixture(exception=True)
        self.assertNotEqual(status, 0)
        self.assertIn('STOP reason=unhandled guest exception address=00000008', report)
        self.assertNotIn('FP defect', report)

    def test_synthetic_decrementer_handler_and_rfi_resume_the_main_dispatch_loop(self):
        status, report = self.run_fixture(interrupt='decrementer')
        self.assertEqual(status, 0)
        self.assertIn('pc=80001004', report)
        self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_byte_identical_copied_syscall_handler_executes_before_rfi(self):
        status, report = self.run_fixture(syscall='valid')
        self.assertEqual(status, 0)
        self.assertIn('pc=80001004', report)
        self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_changed_syscall_vector_is_not_replaced_with_compiled_code(self):
        status, report = self.run_fixture(syscall='vector-changed')
        self.assertNotEqual(status, 0)
        self.assertIn('END frontier=FAIL', report)

    def test_dispatch_window_identifies_the_compiled_handler_entry_and_return(self):
        status, report = self.run_fixture(syscall='trace-window')
        self.assertEqual(status, 0)
        self.assertIn('END frontier=PASS', report)

    def test_zero_cycle_spr_fallback_yield_continues_to_the_next_handler_instruction(self):
        status, report = self.run_fixture(syscall='yielding-handler')
        self.assertEqual(status, 0)
        self.assertIn('pc=80001004', report)
        self.assertIn('END frontier=PASS', report)

    def test_dispatch_discriminant_closes_markers_before_log_entry_and_after_return(self):
        status, report = self.run_fixture(syscall='yielding-handler', dispatch_trace='LER')
        self.assertEqual(status, 0)
        self.assertEqual(len(report.splitlines()), 8)
        self.assertIn('END frontier=PASS', report)

    def test_repeated_syscall_handlers_are_sampled_with_the_real_resume_address(self):
        status, report = self.run_fixture(syscall='repeated-handlers')
        self.assertEqual(status, 0)
        self.assertIn('END frontier=PASS', report)

    def test_dispatch_marker_open_failure_prevents_report_publication(self):
        status, report = self.run_fixture(failure='dispatch-open', syscall='yielding-handler', dispatch_trace=None)
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_dispatch_marker_write_failure_prevents_report_publication(self):
        status, report = self.run_fixture(failure='dispatch-write', syscall='yielding-handler', dispatch_trace='')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_dispatch_marker_flush_failure_prevents_report_publication(self):
        status, report = self.run_fixture(failure='dispatch-flush', syscall='yielding-handler', dispatch_trace='L')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_dispatch_marker_close_failure_prevents_report_publication(self):
        status, report = self.run_fixture(failure='dispatch-close', syscall='yielding-handler', dispatch_trace='L')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_dispatch_entry_log_failure_leaves_only_the_pre_log_marker(self):
        status, report = self.run_fixture(failure='dispatch-entry-log', syscall='yielding-handler', dispatch_trace='L')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_preexisting_dispatch_trace_is_preserved(self):
        status, report = self.run_fixture(stale_dispatch=True, syscall='yielding-handler', dispatch_trace='preserve trace')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_changed_syscall_source_is_not_executed_using_stale_compiled_code(self):
        status, report = self.run_fixture(syscall='source-changed')
        self.assertNotEqual(status, 0)
        self.assertIn('END frontier=FAIL', report)

    def test_dispatch_progress_is_visible_before_report_publication(self):
        status, report = self.run_fixture(progress=True)
        self.assertEqual(status, 0)
        self.assertIn('END frontier=PASS', report)

    def test_stderr_buffer_configuration_failure_is_not_silently_ignored(self):
        status, report = self.run_fixture(failure='stderr-buffer')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_write_failure_is_not_published(self):
        status, report = self.run_fixture('write')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_flush_failure_is_not_published(self):
        status, report = self.run_fixture('flush')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_close_failure_is_not_published(self):
        status, report = self.run_fixture('close')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_rename_failure_is_not_published(self):
        status, report = self.run_fixture('rename')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_log_close_failure_is_not_published(self):
        status, report = self.run_fixture('log-close')
        self.assertNotEqual(status, 0)
        self.assertIsNone(report)

    def test_preexisting_report_is_preserved(self):
        status, report = self.run_fixture(stale=True)
        self.assertNotEqual(status, 0)
        self.assertEqual(report, 'preserve me')


if __name__ == '__main__':
    unittest.main()
