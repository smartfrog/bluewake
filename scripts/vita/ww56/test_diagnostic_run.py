"""Public synthetic diagnostic execution contracts; no services or emulator."""

from contextlib import ExitStack, redirect_stdout
import io
import json
from pathlib import Path
import signal
import sys
import unittest
from unittest.mock import MagicMock, patch

import run
import activity
import fp_launch
import test_diagnostic_readiness as readiness
import test_run_flow as flow
import test_watch_frontier as frontier


class DiagnosticCommandTests(unittest.TestCase):
    arguments = readiness.DiagnosticCommandLineTests.arguments

    def test_live_opt_in_accepts_bound_profile(self):
        with patch.object(sys, 'argv', self.arguments('--diagnostic-run', '--diagnostic-profile',
                                                      '/profile', '--request-token', 'e' * 32)):
            args = run.parse_arguments()
        self.assertTrue(args.diagnostic_run)
        self.assertFalse(args.check)

    def test_live_opt_in_requires_profile_and_token(self):
        for extra in (('--diagnostic-run',), ('--diagnostic-run', '--diagnostic-profile', '/profile')):
            with self.subTest(options=extra), patch.object(sys, 'argv', self.arguments(*extra)):
                with patch('sys.stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
                    run.parse_arguments()

    def test_live_opt_in_rejects_check_and_long_hang_window(self):
        for extra in (('--check',), ('--hang-seconds', '31')):
            with self.subTest(options=extra), patch.object(sys, 'argv', self.arguments(
                    '--diagnostic-run', '--diagnostic-profile', '/profile',
                    '--request-token', 'e' * 32, *extra)):
                with patch('sys.stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
                    run.parse_arguments()


class DiagnosticPropertyTests(unittest.TestCase):
    def test_diagnostic_properties_never_request_memory_usage(self):
        with patch.object(run.subprocess, 'check_output', return_value='') as command:
            run.phase_properties('synthetic.service', diagnostic=True)
        self.assertNotIn('--property=MemoryPeak', command.call_args.args[0])
        self.assertNotIn('--property=MemoryCurrent', command.call_args.args[0])
        self.assertIn('--property=MemoryMax', command.call_args.args[0])


class OwnedWrapperTests(frontier.WatchFixture):
    def test_shared_runner_reaps_only_spawned_wrapper_after_observer_error(self):
        process = MagicMock(pid=987654, returncode=-signal.SIGTERM)
        process.__enter__.return_value = process
        process.poll.return_value = None

        def failing_observer(pid):
            self.assertEqual(pid, 987654)
            raise RuntimeError('first observer fault')

        with patch.object(fp_launch.subprocess, 'Popen', return_value=process), \
                patch.object(fp_launch.os, 'killpg') as kill, \
                patch.object(fp_launch.resource, 'getrlimit', return_value=(-1, -1)):
            with self.assertRaisesRegex(RuntimeError, 'first observer fault'):
                fp_launch.run_owned(['synthetic-only'], self.runroot, 60,
                                    evidence_root=self.phase_root,
                                    phase_observer=failing_observer)
        kill.assert_called_once_with(987654, signal.SIGTERM)
        process.wait.assert_called_once_with(timeout=2)
        disposition = json.loads((self.phase_root / 'evidence/process.json').read_text())
        self.assertTrue(disposition['waited'])
        self.assertEqual(disposition['stop_reason'], 'observer_error')


class DiagnosticPreflightTests(unittest.TestCase):
    setUp = readiness.DiagnosticReadinessTests.setUp

    def live_args(self):
        with patch.object(sys, 'argv', readiness.DiagnosticCommandLineTests.arguments(self)):
            defaults = run.parse_arguments()
        defaults.__dict__.update(vars(self.args))
        self.args = defaults
        self.args.check = False
        self.args.diagnostic_run = True
        return self.args

    def test_live_wrong_profile_fails_before_any_launch_or_write(self):
        self.live_args()
        config = self.profile / readiness.CONFIG_PATH
        config.write_bytes(config.read_bytes().replace(b'cpu-opt: false', b'cpu-opt: true'))
        with patch.object(run, 'stage_trial', side_effect=AssertionError('staging')) as stage, \
                patch.object(run, 'record_identities', return_value={}), \
                patch.object(run, 'verify_uncapped_supervisor', side_effect=AssertionError('late gate')) as supervisor, \
                redirect_stdout(io.StringIO()):
            self.assertEqual(run.run_trial(self.args), 2)
        stage.assert_not_called()
        supervisor.assert_not_called()

    def test_unknown_profile_fails_before_staging_or_launch(self):
        self.live_args()
        self.args.diagnostic_profile = self.root / 'unknown-profile'
        with patch.object(run, 'stage_trial') as stage, \
                patch.object(run, 'record_identities', return_value={}), \
                patch.object(run, 'verify_uncapped_supervisor') as supervisor, \
                redirect_stdout(io.StringIO()):
            self.assertEqual(run.run_trial(self.args), 2)
        stage.assert_not_called()
        supervisor.assert_not_called()

    def test_each_admission_failure_prevents_staging_and_launch(self):
        self.live_args()
        gates = ('check_supervisor', 'check_launcher', 'check_inputs', 'check_freshness')
        for blocked in gates:
            with self.subTest(gate=blocked), ExitStack() as stack:
                for name in gates:
                    stack.enter_context(patch.object(run, name, side_effect=ValueError(name)
                                                     if name == blocked else None, return_value={}))
                stage = stack.enter_context(patch.object(run, 'stage_trial'))
                stack.enter_context(patch.object(run, 'record_identities', return_value={}))
                stack.enter_context(redirect_stdout(io.StringIO()))
                self.assertEqual(run.run_trial(self.args), 2)
                stage.assert_not_called()

    def test_invalid_explicit_token_is_rejected_before_profile_writes(self):
        destination = self.root / 'owned-run'
        destination.mkdir()
        with self.assertRaisesRegex(ValueError, 'token'):
            run.fresh_request(destination, 'b' * 40, 'c' * 64, token='invalid')
        self.assertEqual(list(destination.iterdir()), [])

    def test_staged_requests_reuse_explicit_binding_without_touching_source(self):
        destination = self.root / 'owned-run'
        destination.mkdir()
        self.args.diagnostic_run = True
        self.args.check = False
        before = {str(p): p.read_bytes() for p in self.profile.rglob('*') if p.is_file()}
        token = run.fresh_request(destination, self.args.guest_source, self.args.vpk_sha256,
                                  64, 0, True, None, token=self.args.request_token)
        self.assertEqual(token, self.args.request_token)
        for name, content in self.contents.items():
            self.assertEqual((destination / run.REPORT_DIRECTORY / name).read_text(), content)
        self.assertEqual(before, {str(p): p.read_bytes() for p in self.profile.rglob('*') if p.is_file()})


class DiagnosticStagingTests(unittest.TestCase):
    setUp = flow.StagingSchemaTests.setUp
    tearDown = flow.StagingSchemaTests.tearDown

    def test_diagnostic_staging_uses_existing_alternate_profile(self):
        self.args.diagnostic_run = True
        self.args.diagnostic_profile = self.root / 'approved-profile'
        self.args.request_token = 'a' * 32
        with ExitStack() as stack:
            checked = stack.enter_context(patch.object(run, 'diagnostic_plan', return_value={'synthetic': True}))
            stack.enter_context(patch.object(run, 'stage_runtime', return_value={}))
            firmware = stack.enter_context(patch.object(run, 'stage_firmware', return_value={}))
            stack.enter_context(patch.object(run, 'stage_shader_compiler', side_effect=flow.fake_stage_shader))
            stack.enter_context(patch.object(run, 'stage_disc', side_effect=flow.fake_stage_disc))
            stack.enter_context(patch.object(run, 'library_identity', return_value={}))
            stack.enter_context(patch.object(run, 'runtime_inputs', return_value={}))
            config = stack.enter_context(patch.object(run, 'config_identity', return_value=flow.CANONICAL))
            stack.enter_context(patch.object(run, 'direct_phases', side_effect=flow.fake_direct_phases))
            root, receipts, plan = run.stage_trial(self.args)
        checked.assert_called_once_with(self.args)
        self.assertTrue(firmware.call_args.kwargs['alt'])
        self.assertTrue(config.call_args.kwargs['alt'])
        self.assertEqual(firmware.call_args.kwargs['pref_path'], root / run.FIRMWARE_DIRECTORY)
        self.assertEqual(plan['diagnostic_readiness'], {'synthetic': True})
        self.assertFalse(self.args.diagnostic_profile.exists())

    def test_complete_synthetic_flow_checks_bound_staged_profile_before_guest(self):
        args = self.args
        args.check = False
        args.diagnostic_run = True
        args.request_token = 'a' * 32
        args.diagnostic_profile = self.root / 'approved-profile'
        args.supervisor_unit = 'synthetic.service'
        args.install_budget_bytes = args.guest_budget_bytes = 1024
        args.install_headroom_bytes = args.guest_headroom_bytes = 2048
        args.present_grace_seconds = 10
        args.rel_settle_seconds = 5
        args.cleanup_grace_seconds = 1
        args.native_context_artifacts = None
        for name in ('vs0', 'sa0'):
            (self.firmware / name / 'public-synthetic').write_bytes(b'public fixture')
        directory = args.diagnostic_profile / run.REPORT_DIRECTORY
        directory.mkdir(parents=True)
        from launch import request_content
        for name, count in (('ww05.request', None), ('ww05.continue', 64), ('ww05.audio', 1)):
            (directory / name).write_text(request_content(args.request_token, args.guest_source,
                                                         args.vpk_sha256, count))
        config = args.diagnostic_profile / readiness.CONFIG_PATH
        config.parent.mkdir(parents=True)
        config.write_bytes(readiness.profile_config(alt=True, pref_path=args.diagnostic_profile / run.FIRMWARE_DIRECTORY))
        before = {str(p): p.read_bytes() for p in args.diagnostic_profile.rglob('*') if p.is_file()}
        phases = []

        def fake_phase(name, arguments, root, receipts, plan, record, **kwargs):
            self.assertTrue(arguments.diagnostic_run)
            run.verify_private_config(root, plan)
            (Path(plan['phases'][name]['evidence_root']) / 'evidence/process.log').write_text('')
            phases.append(name)
            if name == 'guest':
                staged = run.diagnostic_plan(arguments, root)
                self.assertEqual(staged['token'], args.request_token)
            return {'requested_stop': {'stop_reason': 'installed_payload'}}

        with ExitStack() as stack:
            for name in ('check_supervisor', 'check_launcher', 'check_inputs', 'check_freshness',
                         'verify_uncapped_supervisor', 'record_identities', 'verify_trial_inputs', 'run_probe'):
                stack.enter_context(patch.object(run, name, return_value={}))
            stack.enter_context(patch.object(run, 'stage_runtime', return_value={}))
            stack.enter_context(patch.object(run, 'stage_shader_compiler', side_effect=flow.fake_stage_shader))
            stack.enter_context(patch.object(run, 'stage_disc', side_effect=flow.fake_stage_disc))
            stack.enter_context(patch.object(run, 'runtime_inputs', return_value={}))
            stack.enter_context(patch.object(run, 'direct_phases', side_effect=flow.fake_direct_phases))
            stack.enter_context(patch.object(run, 'install_ready_probe', return_value=lambda: True))
            stack.enter_context(patch.object(run, 'verify_installed', return_value={'synthetic': True}))
            stack.enter_context(patch.object(run, 'run_phase', side_effect=fake_phase))
            stack.enter_context(patch.object(run, 'capture_snapshot', side_effect=AssertionError('memory capture')))
            stack.enter_context(patch.object(run.subprocess, 'Popen', side_effect=AssertionError('process')))
            stack.enter_context(redirect_stdout(io.StringIO()))
            self.assertEqual(run.run_trial(args), 0)
        self.assertEqual(phases, ['install', 'guest'])
        self.assertEqual(before, {str(p): p.read_bytes() for p in args.diagnostic_profile.rglob('*') if p.is_file()})
        result = json.loads((args.receipts_root / ('ww56-' + args.run_id) / 'result.json').read_text())
        self.assertEqual(result['completion'], 'INCOMPLETE')
        self.assertEqual(result['postflight_input_and_payload_gates'], 'PASS')


class DiagnosticWatchTests(frontier.WatchFixture):
    def setUp(self):
        super().setUp()
        self.args.diagnostic_run = True
        self.state['pid'] = 12345
        self.state['invocation_id'] = 'a' * 32
        self.props = {'MainPID': '12345', 'ControlGroup': 'test', 'InvocationID': 'a' * 32}
        with patch.object(run.time, 'monotonic', return_value=0):
            self.watch = run.PhaseWatch('guest', self.args, self.runroot, self.receipts, {}, self.entry,
                                        'ww56-test-guest.service', 1024, 2048, 180, self.state)

    def test_markers_and_all_terminal_drains_never_capture_memory(self):
        self.journal.write_text(frontier.GPU_LINE + '\n' + frontier.PRESENT_ONE + '\n')
        with patch.object(run, 'capture_snapshot', side_effect=AssertionError('memory capture')):
            for reason in ('managed-native-exit', 'wrapper-exit', 'post-cleanup'):
                self.watch.final_drain(reason)
        self.assertEqual(self.state['snapshots'], [])
        self.assertEqual(len(self.state['presentations']), 1)

    def test_live_fault_and_teardown_never_read_memory_files(self):
        original = Path.read_text

        def cpu_or_log_only(path, *args, **kwargs):
            if str(path).startswith('/proc/') and path.name in {'status', 'maps', 'smaps', 'smaps_rollup', 'meminfo'}:
                self.fail('diagnostic memory read: ' + str(path))
            if path.name in {'memory.current', 'memory.stat', 'memory.events'}:
                self.fail('diagnostic memory polling: ' + str(path))
            return original(path, *args, **kwargs)

        with patch.object(Path, 'read_text', cpu_or_log_only), \
                patch.object(run, 'capture_snapshot', side_effect=AssertionError('snapshot')):
            stop, _signal = self.observe(1, 0, frontier.GPU_LINE + '\n[fatal] synthetic fault\n')
            self.watch.final_drain('post-cleanup')
        self.assertEqual(stop['stop_reason'], 'unsupported-or-context-error')
        self.assertEqual(self.watch.failure_lines, ['[fatal] synthetic fault'])
        self.assertEqual(self.state['snapshots'], [])

    def observe(self, now, ticks, output=None):
        if output is not None:
            self.journal.write_text(output)
        with patch.object(run.time, 'monotonic', return_value=now), \
                patch.object(activity, 'process_cpu_ticks', return_value={(12345, 1): ticks}), \
                patch.object(activity, 'available_memory_bytes', side_effect=AssertionError('RAM polling')), \
                patch.object(run.Path, 'exists', return_value=True), \
                patch.object(run, 'properties', return_value=self.props), \
                patch.object(run.subprocess, 'run') as signal, redirect_stdout(io.StringIO()):
            result = self.watch(99)
        return result, signal

    def test_silent_near_zero_cpu_aborts_at_thirty_seconds(self):
        self.assertIsNone(self.observe(0, 0)[0])
        self.assertIsNone(self.observe(29, 0)[0])
        result, signal = self.observe(30, 0)
        self.assertEqual(result['stop_reason'], 'inactive')
        self.assertEqual(signal.call_args.args[0][-1], self.watch.unit)
        self.assertEqual(self.state['requested_stop']['idle_seconds'], 30)

    def test_healthy_cpu_continues_without_passive_wait(self):
        self.observe(0, 0)
        self.assertIsNone(self.observe(31, 31 * self.watch.idle.ticks_per_second)[0])
        self.assertGreaterEqual(self.watch.idle.window.cpu_fraction, 0.99)

    def test_log_progress_keeps_idle_cpu_alive(self):
        self.observe(0, 0)
        self.assertIsNone(self.observe(29, 0, '[progress] synthetic\n')[0])
        self.assertIsNone(self.observe(31, 0)[0])

    def test_foreign_invocation_is_not_observed_or_signalled(self):
        self.props['InvocationID'] = 'f' * 32
        with patch.object(activity, 'process_cpu_ticks', side_effect=AssertionError('foreign process')):
            with self.assertRaisesRegex(ValueError, 'invocation'):
                self.observe(1, 0)

    def test_release_refuses_an_invocation_not_created_by_owned_wrapper(self):
        self.state['released'] = False
        (self.receipts / 'guest-ready.json').write_text(json.dumps({'pid': 12345, 'cgroup': 'test'}))
        with patch.object(run, 'properties', return_value=self.props), \
                patch.object(run, 'wrapper_invocation', return_value='f' * 32):
            with self.assertRaisesRegex(ValueError, 'invocation'):
                self.watch._release()
        self.assertFalse((self.receipts / 'guest-permit').exists())

    def test_first_fault_is_preserved_without_snapshot_on_teardown(self):
        self.journal.write_text('[fatal] first synthetic fault\n' + frontier.PRESENT_ONE + '\n')
        self.args.run_id = 'fault'
        self.args.cleanup_grace_seconds = 1
        entry = dict(self.entry, command=['synthetic'], environment={}, cwd=str(self.root))
        record = {'phases': {}}
        with patch.object(run, 'run_owned', side_effect=RuntimeError('first observer fault')), \
                patch.object(run, 'properties', side_effect=[{'LoadState': 'not-found'}, ValueError('late properties')]), \
                patch.object(run, 'stop_owned_unit', side_effect=ValueError('late cleanup')), \
                patch.object(run, 'capture_snapshot', side_effect=AssertionError('snapshot')):
            with self.assertRaisesRegex(RuntimeError, 'first observer fault'):
                run.run_phase('guest', self.args, self.runroot, self.receipts, {'phases': {'guest': entry}},
                              record, budget=1024, headroom=2048, deadline=180)
        disposition = json.loads((self.receipts / 'guest-disposition.json').read_text())
        self.assertEqual(disposition['snapshots'], [])
        self.assertIn('late cleanup', disposition['cleanup_error'])
        self.assertEqual(disposition['error'], 'first observer fault')


if __name__ == '__main__':
    unittest.main()
