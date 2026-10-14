import subprocess
import unittest
from unittest.mock import patch

import test_di_read
import test_host
import test_io
import test_rel_materialize
import test_ship_prolog


QUIET_OUT = test_host.ROOT / 'build-f5/services-tests-quiet'
TARGETS = ('io_probe', 'host_probe', 'external_vector_probe', 'gx_drain_probe', 'vi_clock_probe',
           'rel_materialize_probe', 'ship_prolog_probe')
HIGH_VOLUME = ('[progress]', '[edge-progress]', '[dispatch] stage=enter',
               '[exception-vector-dispatch]', '[exception-vector-return]')


class LoggingGateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.build(targets=TARGETS)
        test_host.StartupHostTest.build(QUIET_OUT, ('-DWW05_QUIET_LOGGING=ON',), TARGETS)
        with patch.object(test_host.StartupHostTest, 'setUpClass'):
            test_di_read.DiscReadTest.setUpClass()

    def fixture(self, quiet=False, **scenario):
        return test_io.MainIoTest().run_fixture(
            output=QUIET_OUT if quiet else test_host.OUT, quiet_logging=quiet, return_log=True, **scenario)

    def probe(self, binary, scenario, quiet=True):
        output = QUIET_OUT if quiet else test_host.OUT
        result = subprocess.run([str(output / binary), *scenario], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stderr

    def test_default_build_keeps_existing_high_volume_markers(self):
        self.assertIn('WW05_QUIET_LOGGING:BOOL=OFF', (test_host.OUT / 'CMakeCache.txt').read_text())
        status, report, log = self.fixture(interrupt='vector')
        self.assertEqual(status, 0)
        for marker in HIGH_VOLUME:
            self.assertIn(marker, log)
        self.assertIn('[dispatch] stage=return', log)

    def test_quiet_build_removes_high_volume_success_emission(self):
        self.assertIn('WW05_QUIET_LOGGING:BOOL=ON', (QUIET_OUT / 'CMakeCache.txt').read_text())
        status, report, log = self.fixture(quiet=True, interrupt='vector')
        self.assertEqual(status, 0)
        for marker in (*HIGH_VOLUME, '[dispatch] stage=return'):
            self.assertNotIn(marker, log)

    def test_both_builds_produce_identical_execution_reports(self):
        for scenario in ({}, {'interrupt': 'vector'}, {'interrupt': 'fp-vector'},
                         {'interrupt': 'decrementer'}, {'mmio_poll': True}, {'aram_dma': True},
                         {'dsp_init': True}, {'cpu_boundary': 'restore'}, {'cpu_boundary': 'context'},
                         {'syscall': 'yielding-handler', 'dispatch_trace': 'LER'},
                         {'consumer': True, 'continue_gx': 64}):
            with self.subTest(scenario=scenario):
                verbose = self.fixture(**scenario)
                quiet = self.fixture(quiet=True, **scenario)
                self.assertEqual(quiet[:2], verbose[:2])

    def test_quiet_build_keeps_failed_dispatch_exception_registers_and_stop(self):
        status, report, log = self.fixture(quiet=True, interrupt='unsupported')
        self.assertEqual(status, 1)
        self.assertIn('[dispatch] stage=return', log)
        self.assertIn('[exception]', log)
        self.assertIn('[exception-vector] address=00000500 bytes=256', log)
        self.assertIn('STOP reason=unsupported host call', report)

    def test_quiet_build_keeps_panic_and_halt_diagnostics(self):
        for stop in ('panic', 'halt'):
            with self.subTest(stop=stop):
                status, report, log = self.fixture(quiet=True, game_stop=stop)
                self.assertEqual(status, 1)
                self.assertIn('[halt]', log)
                self.assertIn('STOP reason=', report)
                if stop == 'panic':
                    self.assertIn('[panic]', log)

    def test_quiet_build_keeps_safety_log_write_failures_fatal(self):
        for failure in ('external-state-log', 'external-vector-log'):
            with self.subTest(failure=failure):
                status, report, log = self.fixture(quiet=True, interrupt='unsupported', failure=failure)
                self.assertNotEqual(status, 0)
                self.assertIsNone(report)

    def test_quiet_build_does_not_attempt_suppressed_vector_log_writes(self):
        for failure in ('external-dispatch-log', 'external-return-log'):
            with self.subTest(failure=failure):
                status, report, log = self.fixture(quiet=True, interrupt='vector', failure=failure)
                self.assertEqual(status, 0)
                self.assertIn('END frontier=PASS', report)

    def test_quiet_build_preserves_vector_state_memory_and_cycle_contracts(self):
        for vector in ((), ('fp',), ('decrementer',)):
            for scenario in ('execute', 'single-cycle', 'zero-budget', 'unaligned', 'translated',
                             'pending', 'short-ram', *(f'corrupt-{offset}' for offset in range(152))):
                with self.subTest(vector=vector, scenario=scenario):
                    log = self.probe('external_vector_probe', (scenario, *vector))
                    for marker in HIGH_VOLUME:
                        self.assertNotIn(marker, log)

    def test_quiet_build_keeps_exception_vector_failure_returns(self):
        for vector in ((), ('fp',), ('decrementer',)):
            with self.subTest(vector=vector):
                log = self.probe('external_vector_probe', ('failed-store', *vector))
                self.assertIn('[exception-vector-return]', log)
                self.assertNotIn('[exception-vector-dispatch]', log)

    def test_quiet_build_keeps_edge_accounting_timer_and_ownership_contracts(self):
        for scenario in ('dec-disabled', 'dec-edge-charge', 'dec-turn-credit', 'dec-mmio-quantum',
                         'dec-mmio-next-turn', 'dec-rfi', 'dec-external-priority',
                         'epilogue-edge-compiled', 'epilogue-edge-unowned', 'unknown-mmio'):
            with self.subTest(scenario=scenario):
                self.assertNotIn('[edge-progress]', self.probe('host_probe', (scenario,)))

    def test_quiet_build_keeps_frame_present_measurements(self):
        log = self.probe('gx_drain_probe', ('marker-present',))
        self.assertIn('[frame-present] frame=1 us=1000000', log)
        self.assertIn('[frame-present] frame=2 us=2000000', log)

    def test_quiet_build_keeps_frame_timing_measurements(self):
        log = self.probe('vi_clock_probe', ('multiple',))
        self.assertEqual(log.count('[frame-timing]'), 3)

    def test_quiet_build_keeps_frame_measurement_failures_fatal(self):
        for scenario in ('marker-write-failure', 'marker-clock-failure'):
            with self.subTest(scenario=scenario):
                self.assertNotIn('[frame-present]', self.probe('gx_drain_probe', (scenario,)))

    def test_quiet_build_keeps_rel_frontier_materialization_and_link_milestones(self):
        with patch.object(test_host, 'OUT', QUIET_OUT):
            log = test_rel_materialize.RelMaterializeTest().run_probe('link').stderr
        for marker in ('[rel-frontier]', '[rel-materialized]', '[rel-link]'):
            self.assertIn(marker, log)

    def test_quiet_build_keeps_prolog_and_caller_return_milestones(self):
        with patch.object(test_host, 'OUT', QUIET_OUT):
            log = test_ship_prolog.ShipPrologTest().run_probe('success')
        for stage in ('entered', 'ctor-completed', 'caller-returned'):
            self.assertEqual(log.count(f'stage={stage}'), 1, log)
        self.assertIn('PASS caller-returned', log)


if __name__ == '__main__':
    unittest.main()
