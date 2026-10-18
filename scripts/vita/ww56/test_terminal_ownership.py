"""Synthetic terminal races; no manager, service or guest is started."""

from contextlib import nullcontext, redirect_stdout
import io
import json
from pathlib import Path
import unittest
from unittest.mock import MagicMock, patch

import run
import phase_identity
import fp_launch
import test_phase_ownership as ownership


class TerminalOwnershipTests(ownership.OwnedGateFixture):
    def setUp(self):
        super().setUp()
        self.release()
        self.terminal = dict(self.props, ActiveState='failed', SubState='failed',
                             MainPID='0', ControlGroup='', Result='oom-kill',
                             ExecMainCode='2', ExecMainStatus='9')

    def test_native_terminal_between_live_pid_check_and_verification_is_not_foreign(self):
        self.journal.write_text(''.join(
            '[frame-present] frame=' + str(n) + ' us=' + str(1000 + n) + '\n'
            for n in range(1, 298)))
        original_exists = Path.exists

        def live_before_manager_query(path):
            return True if str(path) == '/proc/12345' else original_exists(path)

        with patch.object(run, 'phase_properties', return_value=self.terminal), \
                patch.object(run, 'properties', return_value=self.terminal), \
                patch.object(Path, 'exists', live_before_manager_query), \
                patch.object(phase_identity, 'process_identity', side_effect=FileNotFoundError), \
                patch.object(run.subprocess, 'run') as signal, redirect_stdout(io.StringIO()):
            self.assertIsNone(self.watch(999))
        signal.assert_not_called()
        self.assertEqual(self.state['properties_at_exit'], self.terminal)
        self.assertEqual(self.state['owned_identity'].starttime, 987)
        self.assertEqual(len(self.state['presentations']), 297)

    def test_proven_terminal_survives_manager_property_deletion_during_cleanup(self):
        with patch.object(run, 'phase_properties', return_value=self.terminal), \
                patch.object(run, 'properties', return_value=self.terminal), \
                patch.object(phase_identity, 'process_identity', side_effect=FileNotFoundError), \
                redirect_stdout(io.StringIO()):
            self.watch(999)
        absent = {'LoadState': 'not-found', 'ActiveState': 'inactive', 'InvocationID': '',
                  'MainPID': '0', 'ControlGroup': '', 'Result': 'success',
                  'ExecMainCode': '0', 'ExecMainStatus': '0'}
        with patch.object(run, 'phase_properties', return_value=absent), \
                patch.object(run.subprocess, 'run') as signal:
            run.stop_owned_unit(self.unit, self.state)
        signal.assert_not_called()
        self.assertEqual(self.state['properties_at_exit'], self.terminal)

    def test_unobserved_deleted_invocation_is_unverified_not_natural_exit(self):
        absent = {'LoadState': 'not-found', 'ActiveState': 'inactive', 'InvocationID': '',
                  'MainPID': '0', 'ControlGroup': ''}
        with patch.object(run, 'phase_properties', return_value=absent), \
                patch.object(run, 'properties', return_value=absent), \
                patch.object(run, 'process_identity', side_effect=FileNotFoundError), \
                patch.object(run.subprocess, 'run') as signal:
            with self.assertRaisesRegex(ValueError, 'UNVERIFIED'):
                self.watch(999)
        signal.assert_not_called()
        self.assertNotIn('properties_at_exit', self.state)

    def test_manager_deletion_between_live_and_terminal_queries_is_unverified_without_proof(self):
        absent = {'LoadState': 'not-found', 'ActiveState': 'inactive', 'InvocationID': '',
                  'MainPID': '0', 'ControlGroup': ''}
        with patch.object(run, 'phase_properties', side_effect=[self.props, absent]), \
                patch.object(run, 'process_identity', side_effect=FileNotFoundError), \
                patch.object(run.subprocess, 'run') as signal:
            with self.assertRaisesRegex(ValueError, 'UNVERIFIED'):
                self.watch(999)
        signal.assert_not_called()
        self.assertNotIn('properties_at_exit', self.state)

    def test_foreign_or_incomplete_terminal_never_authorizes_signals(self):
        for changes in ({'InvocationID': 'f' * 32}, {'ExecMainCode': '0'},
                        {'ExecMainStatus': ''}, {'ActiveState': 'activating'},
                        {'MainPID': '54321'}, {'ControlGroup': '/foreign'}):
            with self.subTest(changes=changes), \
                    patch.object(run, 'phase_properties', return_value=dict(self.terminal, **changes)), \
                    patch.object(run, 'properties', return_value=dict(self.terminal, **changes)), \
                    patch.object(run, 'process_identity', side_effect=FileNotFoundError), \
                    patch.object(run.subprocess, 'run') as signal:
                with self.assertRaises(ValueError):
                    self.watch(999)
                signal.assert_not_called()
                self.assertNotIn('properties_at_exit', self.state)

    def test_terminal_pid_reuse_is_rejected_before_signalling(self):
        with patch.object(run, 'phase_properties', return_value=self.terminal), \
                patch.object(run, 'properties', return_value=self.terminal), \
                patch.object(phase_identity, 'process_identity', return_value=(988, self.cgroup)), \
                patch.object(run.subprocess, 'run') as signal:
            with self.assertRaisesRegex(ValueError, 'ownership|reused'):
                self.watch(999)
        signal.assert_not_called()

    def test_terminal_with_retained_cgroup_requires_original_process_gone(self):
        self.terminal['ControlGroup'] = self.cgroup
        with patch.object(run, 'phase_properties', return_value=self.terminal), \
                patch.object(phase_identity, 'process_identity', side_effect=FileNotFoundError), \
                patch.object(run.subprocess, 'run') as signal, redirect_stdout(io.StringIO()):
            self.assertIsNone(self.watch(999))
        signal.assert_not_called()
        self.assertEqual(self.state['properties_at_exit'], self.terminal)

    def test_live_properties_then_original_pid_disappears_then_terminal_is_preserved(self):
        with patch.object(run, 'phase_properties', side_effect=[self.props, self.terminal]), \
                patch.object(run, 'process_identity', side_effect=FileNotFoundError), \
                patch.object(phase_identity, 'process_identity', side_effect=FileNotFoundError), \
                patch.object(run.subprocess, 'run') as signal, redirect_stdout(io.StringIO()):
            self.assertIsNone(self.watch(999))
        signal.assert_not_called()
        self.assertEqual(self.state['properties_at_exit'], self.terminal)

    def test_replacement_after_proven_terminal_is_rejected_without_signalling(self):
        self.state['properties_at_exit'] = self.terminal
        with patch.object(run, 'phase_properties', return_value=dict(self.props, InvocationID='f' * 32)), \
                patch.object(run.subprocess, 'run') as signal:
            with self.assertRaisesRegex(ValueError, 'ownership'):
                run.stop_owned_unit(self.unit, self.state)
        signal.assert_not_called()
        self.assertEqual(self.state['properties_at_exit'], self.terminal)

    def test_cleanup_live_to_owned_terminal_transition_does_not_signal(self):
        with patch.object(run, 'phase_properties', side_effect=[self.props, self.terminal]), \
                patch.object(phase_identity, 'process_identity', side_effect=FileNotFoundError), \
                patch.object(run.subprocess, 'run') as signal:
            run.stop_owned_unit(self.unit, self.state)
        signal.assert_not_called()
        self.assertEqual(self.state['properties_at_exit'], self.terminal)

    def test_manager_deletes_proven_remain_after_exit_between_cleanup_queries(self):
        self.terminal.update(ActiveState='active', SubState='exited', Result='success',
                             ExecMainCode='1', ExecMainStatus='0')
        absent = {'LoadState': 'not-found', 'ActiveState': 'inactive', 'InvocationID': '',
                  'MainPID': '0', 'ControlGroup': ''}
        with patch.object(run, 'phase_properties', side_effect=[self.terminal, absent]), \
                patch.object(phase_identity, 'process_identity', side_effect=FileNotFoundError), \
                patch.object(run.subprocess, 'run') as signal:
            run.stop_owned_unit(self.unit, self.state)
        signal.assert_not_called()
        self.assertEqual(self.state['properties_at_exit'], self.terminal)

    def test_nonempty_terminal_cgroup_with_original_process_alive_is_unverified(self):
        self.terminal['ControlGroup'] = self.cgroup
        with patch.object(run, 'phase_properties', return_value=self.terminal), \
                patch.object(phase_identity, 'process_identity', return_value=(987, self.cgroup)), \
                patch.object(run.subprocess, 'run') as signal:
            with self.assertRaisesRegex(ValueError, 'UNVERIFIED'):
                self.watch(999)
        signal.assert_not_called()

    def test_native_terminal_during_presignal_recheck_preserves_logged_failure_without_signal(self):
        self.journal.write_text('[fatal] first synthetic exception\n')
        with patch.object(run, 'phase_properties', side_effect=[self.props, self.terminal]), \
                patch.object(run, 'process_identity', return_value=(987, self.cgroup)), \
                patch.object(phase_identity, 'process_identity', side_effect=FileNotFoundError), \
                patch.object(run.subprocess, 'run') as signal, redirect_stdout(io.StringIO()):
            self.assertIsNone(self.watch(999))
        signal.assert_not_called()
        self.assertNotIn('requested_stop', self.state)
        self.assertIn('[fatal] first synthetic exception', self.state['terminal_drain']['failure_lines'])
        self.assertEqual(self.state['properties_at_exit'], self.terminal)

    def test_proven_terminal_waits_for_wrapper_native_exit_without_killing_wrapper(self):
        process = MagicMock(pid=999, returncode=137)
        process.__enter__.return_value = process
        process.poll.side_effect = [None, 137, 137]
        evidence = self.root / 'wrapper'
        (evidence / 'evidence').mkdir(parents=True)
        with patch.object(fp_launch.subprocess, 'Popen', return_value=process), \
                patch.object(fp_launch.os, 'killpg') as kill, \
                patch.object(fp_launch.resource, 'getrlimit', return_value=(-1, -1)), \
                patch.object(fp_launch.time, 'sleep'), \
                patch.object(run, 'phase_properties', return_value=self.terminal), \
                patch.object(phase_identity, 'process_identity', side_effect=FileNotFoundError), \
                redirect_stdout(io.StringIO()):
            status = fp_launch.run_owned(['synthetic'], self.runroot, None,
                                         evidence_root=evidence, phase_observer=self.watch)
        self.assertEqual(status, 137)
        kill.assert_not_called()
        disposition = json.loads((evidence / 'evidence/process.json').read_text())
        self.assertEqual(disposition['stop_reason'], 'exit')
        self.assertEqual(disposition['exit'], 137)
        self.assertTrue(disposition['waited'])

    def test_terminal_failure_reaches_caller_after_wrapper_reap_and_late_drain(self):
        self.assert_native_failure('oom-kill.*9')

    def test_native_sigkill_without_oom_is_not_success(self):
        self.terminal['Result'] = 'signal'
        self.assert_native_failure('signal.*9')

    def test_native_segv_is_not_success(self):
        self.terminal.update(Result='core-dump', ExecMainCode='3', ExecMainStatus='11')
        self.assert_native_failure('core-dump.*11')

    def test_native_timeout_abort_six_is_not_an_intentional_runner_stop(self):
        self.terminal.update(Result='timeout', ExecMainStatus='6')
        self.state['requested_stop'] = {'stop_reason': 'ceiling'}
        self.assert_native_failure('timeout.*6')

    def test_external_user_stop_is_not_success(self):
        self.terminal.update(Result='signal', ExecMainStatus='15')
        self.assert_native_failure('signal.*15')

    def test_intentional_runner_sigterm_remains_an_intentional_stop(self):
        self.terminal.update(Result='signal', ExecMainStatus='15')
        self.state['requested_stop'] = {'stop_reason': 'ceiling'}
        self.assert_native_failure(None)

    def test_unconfirmed_stop_request_does_not_hide_spontaneous_sigterm(self):
        self.terminal.update(Result='signal', ExecMainStatus='15')
        self.state['requested_stop'] = {'stop_reason': 'bounded-phase-planned-time-stop'}
        self.assert_native_failure('signal.*15', wrapper_stop={'stop_reason': 'exit'})

    def test_success_result_sigterm_without_intended_stop_is_fatal(self):
        self.terminal.update(Result='success', ExecMainStatus='15')
        self.assert_native_failure('success.*15', wrapper_status=-15)

    def test_intended_guest_time_stop_accepts_success_result_sigterm(self):
        self.terminal.update(Result='success', ExecMainStatus='15')
        self.state['requested_stop'] = {'stop_reason': 'bounded-phase-planned-time-stop'}
        self.assert_native_failure(None, wrapper_status=-15)

    def test_installed_payload_context_mismatch_does_not_hide_sigterm(self):
        self.terminal.update(Result='success', ExecMainStatus='15')
        self.state['requested_stop'] = {'stop_reason': 'installed_payload', 'payload': {'verified': True}}
        self.assert_native_failure('success.*15', wrapper_stop={
            'stop_reason': 'installed_payload', 'payload': {'different': True}})

    def test_unwaited_wrapper_does_not_authenticate_intended_sigterm(self):
        self.terminal.update(Result='success', ExecMainStatus='15')
        self.state['requested_stop'] = {'stop_reason': 'bounded-phase-planned-time-stop'}
        self.assert_native_failure('success.*15', wrapper_waited=False)

    def test_verified_natural_success_is_not_a_native_failure(self):
        self.terminal.update(Result='success', ActiveState='inactive',
                             ExecMainCode='1', ExecMainStatus='0')
        self.assert_native_failure(None)

    def test_first_observer_exception_survives_native_failure_and_cleanup_error(self):
        self.assert_native_failure('first observer fault', first_error=RuntimeError('first observer fault'),
                                   cleanup_error=ValueError('secondary cleanup fault'))

    def test_native_failure_is_not_masked_by_later_cleanup_error(self):
        self.assert_native_failure('oom-kill.*9', cleanup_error=ValueError('secondary cleanup fault'))

    def test_zero_wrapper_status_cannot_hide_native_oom(self):
        self.assert_native_failure('oom-kill.*9', wrapper_status=0)

    def assert_native_failure(self, pattern, first_error=None, cleanup_error=None, wrapper_status=137,
                              wrapper_stop=None, wrapper_waited=True):
        self.args.run_id = 'test'
        self.args.cleanup_grace_seconds = 1
        entry = dict(self.entry, command=['synthetic'], environment={}, cwd=str(self.root))
        record = {'phases': {}}

        def wrapper(command, *args, **kwargs):
            watch = kwargs['phase_observer']
            watch.state.update(self.state)
            watch(999)
            self.journal.write_text('[frame-present] frame=297 us=1297\n')
            disposition = dict(self.state.get('requested_stop') or {'stop_reason': 'exit'})
            if wrapper_stop is not None:
                disposition = dict(wrapper_stop)
            disposition.update(exit=wrapper_status, waited=wrapper_waited)
            (self.phase_root / 'evidence/process.json').write_text(json.dumps(disposition))
            if first_error:
                raise first_error
            return wrapper_status

        with patch.object(run, 'require_fresh_unit', return_value={'LoadState': 'not-found'}), \
                patch.object(run, 'phase_properties', return_value=self.terminal), \
                patch.object(run, 'properties', return_value=self.terminal), \
                patch.object(phase_identity, 'process_identity', side_effect=FileNotFoundError), \
                patch.object(run, 'run_owned', side_effect=wrapper), \
                patch.object(run, 'stop_owned_unit', wraps=run.stop_owned_unit,
                             side_effect=cleanup_error), \
                patch.object(run.subprocess, 'run') as signal, redirect_stdout(io.StringIO()):
            expectation = (self.assertRaisesRegex(type(first_error) if first_error else ValueError, pattern)
                           if pattern else nullcontext())
            with expectation:
                run.run_phase('guest', self.args, self.runroot, self.receipts,
                              {'phases': {'guest': entry}}, record,
                              budget=1024, headroom=2048, deadline=180)
        signal.assert_not_called()
        disposition = json.loads((self.receipts / 'guest-disposition.json').read_text())
        self.assertEqual(disposition['wrapper']['waited'], wrapper_waited)
        self.assertEqual(disposition['presentations'][-1]['index'], 297)
        if pattern:
            self.assertEqual(disposition['native_failure'], self.terminal)
        else:
            self.assertNotIn('native_failure', disposition)
        if cleanup_error:
            self.assertEqual(disposition['cleanup_error'], str(cleanup_error))
        if first_error:
            self.assertEqual(disposition['error'], str(first_error))


class BoundedTerminalCaptureTests(ownership.OwnedGateFixture):
    def setUp(self):
        super().setUp()
        self.release()
        self.state['requested_stop'] = {'stop_reason': 'installed_payload'}
        self.terminal = dict(self.props, ActiveState='active', SubState='exited',
                             MainPID='0', ControlGroup='', Result='success',
                             ExecMainCode='2', ExecMainStatus='15')

    def test_requested_stop_captures_terminal_published_after_wrapper_exit(self):
        with patch.object(run, 'phase_properties', side_effect=[self.props, self.props, self.terminal]), \
                patch.object(run, 'process_identity', return_value=(987, self.cgroup)), \
                patch.object(phase_identity, 'process_identity', return_value=(987, self.cgroup)), \
                patch.object(run.subprocess, 'run') as signal, redirect_stdout(io.StringIO()):
            result = run.stop_owned_unit(self.unit, self.state)
        self.assertEqual(result, self.terminal)
        self.assertEqual(self.state['properties_at_exit'], self.terminal)
        signal.assert_called_once_with(['systemctl', '--user', 'stop', self.unit], check=True,
                                       timeout=30)

    def test_changed_invocation_during_bounded_capture_is_never_adopted(self):
        foreign = dict(self.props, InvocationID='f' * 32)
        with patch.object(run, 'phase_properties', side_effect=[self.props, self.props, foreign]), \
                patch.object(run, 'process_identity', return_value=(987, self.cgroup)), \
                patch.object(phase_identity, 'process_identity', return_value=(987, self.cgroup)), \
                patch.object(run.subprocess, 'run') as signal:
            with self.assertRaisesRegex(ValueError, 'UNVERIFIED changed'):
                run.stop_owned_unit(self.unit, self.state)
        self.assertNotIn('properties_at_exit', self.state)
        signal.assert_not_called()

    def test_bounded_capture_expiry_keeps_authenticated_stop_without_terminal(self):
        with patch.object(run, 'TERMINAL_CAPTURE_SECONDS', 0.05), \
                patch.object(run, 'phase_properties', return_value=self.props), \
                patch.object(run, 'process_identity', return_value=(987, self.cgroup)), \
                patch.object(phase_identity, 'process_identity', return_value=(987, self.cgroup)), \
                patch.object(run.subprocess, 'run') as signal:
            run.stop_owned_unit(self.unit, self.state)
        signal.assert_called_once_with(['systemctl', '--user', 'stop', self.unit], check=True,
                                       timeout=30)
        self.assertNotIn('properties_at_exit', self.state)


if __name__ == '__main__':
    unittest.main()
