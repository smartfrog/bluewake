"""Public synthetic CPU-on and continuous diagnostic contracts; never launch."""

from contextlib import redirect_stdout
import io
import json
import sys
import unittest
from unittest.mock import MagicMock, patch

import run
import fp_launch
import test_diagnostic_readiness as readiness
import test_diagnostic_run as diagnostic
import test_watch_frontier as frontier
import test_run_units as units


class ContinuousCommandTests(unittest.TestCase):
    arguments = readiness.DiagnosticCommandLineTests.arguments

    def test_explicit_continuous_check_disables_only_guest_frontier_timers(self):
        with patch.object(sys, 'argv', self.arguments(
                '--check', '--diagnostic-profile', '/profile', '--request-token', 'e' * 32,
                '--diagnostic-cpu-opt', 'on', '--until-failure')):
            args = run.parse_arguments()
        self.assertEqual(args.diagnostic_cpu_opt, 'on')
        self.assertIsNone(args.guest_ceiling_seconds)
        self.assertIsNone(args.present_grace_seconds)
        self.assertIsNone(args.rel_settle_seconds)
        self.assertEqual(args.continue_gx, 64)
        self.assertEqual(args.hang_seconds, 30)

    def test_incompatible_options_fail_before_execution(self):
        for extra in (('--until-failure',), ('--diagnostic-cpu-opt', 'on'),
                      ('--check', '--diagnostic-profile', '/profile', '--request-token', 'e' * 32,
                       '--until-failure', '--guest-ceiling-seconds', '600'),
                      ('--check', '--diagnostic-profile', '/profile', '--request-token', 'e' * 32,
                       '--until-failure', '--present-grace-seconds', '0'),
                      ('--check', '--diagnostic-profile', '/profile', '--request-token', 'e' * 32,
                       '--until-failure', '--rel-settle-seconds', '0'),
                      ('--check', '--diagnostic-profile', '/profile', '--request-token', 'e' * 32,
                       '--until-failure', '--native-context-artifacts', '/unused')):
            with self.subTest(options=extra), patch.object(sys, 'argv', self.arguments(*extra)), \
                    patch('sys.stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
                run.parse_arguments()

    def test_legacy_cpu_and_time_defaults_stay_unchanged(self):
        with patch.object(sys, 'argv', self.arguments()):
            args = run.parse_arguments()
        self.assertEqual(args.diagnostic_cpu_opt, 'off')
        self.assertFalse(args.until_failure)
        self.assertEqual(args.guest_ceiling_seconds, 180)


class CpuOnReadinessTests(unittest.TestCase):
    setUp = readiness.DiagnosticReadinessTests.setUp

    def test_cpu_on_requires_matching_canonical_profile(self):
        self.args.diagnostic_cpu_opt = 'on'
        config = self.profile / readiness.CONFIG_PATH
        config.write_bytes(readiness.profile_config(
            alt=False, pref_path=self.profile / run.FIRMWARE_DIRECTORY))
        plan = run.diagnostic_readiness(self.args)
        self.assertEqual(plan['diagnostic_cpu_opt'], 'on')
        self.assertNotIn('cpu_opt_off', plan['private_config'])
        self.assertEqual(run.profile_options({'diagnostic_readiness': plan}), {'alt': False})

    def test_cpu_on_rejects_cpu_off_profile(self):
        self.args.diagnostic_cpu_opt = 'on'
        with self.assertRaisesRegex(ValueError, 'profile'):
            run.diagnostic_readiness(self.args)

    def test_postflight_rejects_changed_cpu_setting(self):
        self.args.diagnostic_cpu_opt = 'on'
        config = self.profile / readiness.CONFIG_PATH
        config.write_bytes(readiness.profile_config(
            alt=False, pref_path=self.profile / run.FIRMWARE_DIRECTORY))
        ready = run.diagnostic_readiness(self.args)
        plan = {'diagnostic_readiness': ready, 'private_config': ready['private_config']}
        config.write_bytes(config.read_bytes().replace(b'cpu-opt: true', b'cpu-opt: false'))
        with self.assertRaisesRegex(ValueError, 'profile'):
            run.verify_private_config(self.profile, plan)


class OperatorDispositionTests(unittest.TestCase):
    setUp = readiness.DiagnosticReadinessTests.setUp
    live_args = diagnostic.DiagnosticPreflightTests.live_args

    def test_operator_interrupt_is_aborted_not_success_even_without_exception_text(self):
        self.live_args()
        with patch.object(run, 'diagnostic_plan', side_effect=KeyboardInterrupt), \
                patch.object(run, 'record_identities', return_value={}), \
                redirect_stdout(io.StringIO()):
            self.assertEqual(run.run_trial(self.args), 2)


class ContinuousWatchTests(diagnostic.DiagnosticWatchTests):
    def setUp(self):
        super().setUp()
        self.args.until_failure = True
        self.args.present_grace_seconds = None
        self.args.rel_settle_seconds = None
        with patch.object(run.time, 'monotonic', return_value=0):
            self.watch = run.PhaseWatch('guest', self.args, self.runroot, self.receipts, {}, self.entry,
                                        'ww56-test-guest.service', 1024, 2048, None, self.state)

    def test_healthy_present_and_rel_continue_far_beyond_six_hundred_seconds(self):
        self.observe(0, 0, frontier.PRESENT_ONE + '\n[rel-frontier] synthetic\n')
        result, signal = self.observe(86400, 86400 * self.watch.idle.ticks_per_second)
        self.assertIsNone(result)
        signal.assert_not_called()
        self.assertIsNone(self.watch.stop_state.ceiling_seconds)

    def test_failure_has_priority_over_continuous_progress(self):
        result, _ = self.observe(86400, 0, frontier.PRESENT_ONE + '\n[fatal] synthetic\n')
        self.assertEqual(result['stop_reason'], 'unsupported-or-context-error')



class ContinuousWrapperTests(frontier.WatchFixture):
    def test_untimed_runner_still_observes_and_cleans_up_on_operator_interrupt(self):
        process = MagicMock(pid=987654, returncode=-15)
        process.__enter__.return_value = process
        process.poll.return_value = None
        with patch.object(fp_launch.subprocess, 'Popen', return_value=process), \
                patch.object(fp_launch.os, 'killpg') as kill, \
                patch.object(fp_launch.resource, 'getrlimit', return_value=(-1, -1)), \
                self.assertRaises(KeyboardInterrupt):
            fp_launch.run_owned(['synthetic-only'], self.runroot, None,
                                evidence_root=self.phase_root,
                                phase_observer=lambda _: (_ for _ in ()).throw(KeyboardInterrupt()))
        kill.assert_called_once()
        process.wait.assert_called_once_with(timeout=2)
        disposition = json.loads((self.phase_root / 'evidence/process.json').read_text())
        self.assertEqual(disposition['stop_reason'], 'operator-stop')

    def test_untimed_runner_calls_observer_after_days_without_timeout(self):
        process = MagicMock(pid=987654, returncode=-15)
        process.__enter__.return_value = process
        process.poll.return_value = None
        observer = MagicMock(side_effect=[None, {'stop_reason': 'operator-stop'}])
        with patch.object(fp_launch.subprocess, 'Popen', return_value=process), \
                patch.object(fp_launch.os, 'killpg'), \
                patch.object(fp_launch.resource, 'getrlimit', return_value=(-1, -1)), \
                patch.object(fp_launch.time, 'monotonic', side_effect=[0, 172800]), \
                patch.object(fp_launch.time, 'sleep'):
            status = fp_launch.run_owned(['synthetic-only'], self.runroot, None,
                                        evidence_root=self.phase_root, phase_observer=observer)
        self.assertEqual(status, 125)
        self.assertEqual(observer.call_count, 2)
        disposition = json.loads((self.phase_root / 'evidence/process.json').read_text())
        self.assertEqual(disposition['stop_reason'], 'operator-stop')
        self.assertEqual(disposition['duration_seconds'], 172800)

    def test_untimed_runner_rejects_missing_observer_before_spawn(self):
        with patch.object(fp_launch.subprocess, 'Popen') as spawn, self.assertRaises(ValueError):
            fp_launch.run_owned(['synthetic-only'], self.runroot, None)
        spawn.assert_not_called()


class ContinuousPhaseTests(unittest.TestCase):
    setUp = units.PhaseCommandTests.setUp
    tearDown = units.PhaseCommandTests.tearDown

    def test_only_explicit_guest_removes_wrapper_and_service_elapsed_limits(self):
        self.args.diagnostic_run = True
        self.args.until_failure = True
        self.plan['phases']['install'] = self.plan['phases']['guest']
        for name, deadline in (('guest', None), ('install', run.INSTALL_DEADLINE_SECONDS)):
            with self.subTest(phase=name), \
                    patch.object(run, 'run_owned', return_value=0) as owned, \
                    patch.object(run, 'properties', return_value={'LoadState': 'not-found'}), \
                    patch.object(run, 'stop_owned_unit', return_value={}):
                run.run_phase(name, self.args, self.root, self.root / 'receipts', self.plan,
                              self.record, budget=1024, headroom=2048, deadline=deadline)
            command, _, timeout = owned.call_args.args
            self.assertIn('--property=RuntimeMaxSec=' + (
                'infinity' if name == 'guest' else str(int(deadline + 15))), command)
            self.assertEqual(timeout, None if name == 'guest' else deadline + 10)
            self.assertIn('--property=TimeoutStopSec=12s', command)

    def test_untimed_non_opted_in_phase_fails_before_writes_or_services(self):
        with patch.object(run, 'require_fresh_unit') as fresh, self.assertRaises(ValueError):
            run.run_phase('guest', self.args, self.root, self.root / 'receipts', self.plan,
                          self.record, budget=1024, headroom=2048, deadline=None)
        fresh.assert_not_called()
        self.assertEqual(list((self.root / 'receipts').iterdir()), [])

    def test_operator_interrupt_keeps_first_reason_and_performs_owned_cleanup(self):
        self.args.diagnostic_run = self.args.until_failure = True
        with patch.object(run, 'run_owned', side_effect=KeyboardInterrupt), \
                patch.object(run, 'properties', return_value={'LoadState': 'not-found'}), \
                patch.object(run, 'stop_owned_unit', return_value={}) as cleanup, \
                self.assertRaises(KeyboardInterrupt):
            run.run_phase('guest', self.args, self.root, self.root / 'receipts', self.plan,
                          self.record, budget=1024, headroom=2048, deadline=None)
        cleanup.assert_called_once()
        self.assertEqual(self.record['phases']['guest']['error'], 'KeyboardInterrupt')


if __name__ == '__main__':
    unittest.main()
