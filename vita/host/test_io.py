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

    def run_fixture(self, failure='', stale=False, interrupt=None, exception=False, syscall=None, progress=False):
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
            environment = dict(os.environ, WW05_IO_FAILURE=failure)
            if interrupt is not None:
                environment['WW05_IO_IRQ'] = interrupt
            if exception:
                environment['WW05_IO_EXCEPTION'] = '1'
            if syscall is not None:
                environment['WW05_IO_SYSCALL'] = syscall
            result = subprocess.run([str(OUT / 'io_probe')], cwd=root,
                                    env=environment, capture_output=True, timeout=10)
            report = (root / 'ww05.report').read_text() if (root / 'ww05.report').exists() else None
            if progress:
                self.assertIn('[progress] turns=1', result.stderr.decode())
            if syscall == 'valid':
                self.assertIn('[exception-handoff] stage=guard', result.stderr.decode())
                self.assertIn('[exception-handoff] stage=guard-result matched=1', result.stderr.decode())
            if syscall == 'trace-window':
                self.assertIn('[dispatch] stage=enter turn=2 pc=803077D0', result.stderr.decode())
                self.assertIn('[dispatch] stage=return turn=2 pc=80001004', result.stderr.decode())
            if syscall == 'yielding-handler':
                for stage in ('returned', 'checked', 'edge-enter', 'edge-return', 'turn-enter',
                              'turn-begun', 'dispatch-ready'):
                    self.assertIn(f'[handler-boundary] stage={stage} pc=803077D4', result.stderr.decode())
            if exception:
                log = result.stderr.decode()
                self.assertIn('[exception] bits=00000008 pc=00000C00 srr0=80001004', log)
                self.assertIn('[exception-vector] address=00000C00', log)
            return result.returncode, report

    def test_synthetic_host_fixture_publishes_closed_report(self):
        status, report = self.run_fixture()
        self.assertEqual(status, 0)
        self.assertIn('platform=HOST', report)
        self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_synthetic_interrupt_yields_resume_the_main_dispatch_loop(self):
        for boundary in ('inside', 'outside'):
            with self.subTest(boundary=boundary):
                status, report = self.run_fixture(interrupt=boundary)
                self.assertEqual(status, 0)
                self.assertIn('pc=80001004', report)
                self.assertTrue(report.endswith('END frontier=PASS live=0 rendered=0\n'))

    def test_unhandled_system_call_is_not_mislabelled_as_an_fp_defect(self):
        status, report = self.run_fixture(exception=True)
        self.assertNotEqual(status, 0)
        self.assertIn('STOP reason=unhandled guest exception address=00000008', report)
        self.assertNotIn('FP defect', report)

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
