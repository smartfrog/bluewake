"""Synthetic owned-gate handshake; never starts a service or guest."""

from contextlib import ExitStack, contextmanager, redirect_stdout
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import run
import test_watch_frontier as frontier


class OwnedGateFixture(frontier.WatchFixture):
    def setUp(self):
        super().setUp()
        self.args.diagnostic_run = True
        self.unit = self.watch.unit
        self.cgroup = '/user.slice/user-1000.slice/user@1000.service/app.slice/' + self.unit
        self.nonce = 'b' * 64
        self.invocation = 'a' * 32
        self.identity = {'pid': 12345, 'cgroup': self.cgroup, 'starttime': 987,
                         'invocation_id': self.invocation, 'launch_nonce': self.nonce}
        self.props = {'LoadState': 'loaded', 'ActiveState': 'active', 'MainPID': '12345',
                      'ControlGroup': self.cgroup, 'InvocationID': self.invocation,
                      'MemoryMax': '1024', 'EffectiveMemoryMax': '1024',
                      'MemorySwapMax': '0', 'OOMPolicy': 'stop'}
        self.state.update(released=False, pid=None, invocation_id=None, diagnostic=True,
                          receipts=str(self.receipts), phase='guest', launch_nonce=self.nonce,
                          properties_before_creation={'LoadState': 'not-found'})
        self.watch = run.PhaseWatch('guest', self.args, self.runroot, self.receipts, {}, self.entry,
                                    self.unit, 1024, 2048, 180, self.state)
        self.ready = self.receipts / 'guest-ready.json'
        self.ready.write_text(json.dumps(self.identity))
        self.log = self.phase_root / 'evidence/process.log'
        self.log.write_text('Running as unit: ' + self.unit + '\n')

    @contextmanager
    def manager(self, properties=None):
        original = Path.read_text

        def kernel(path, *args, **kwargs):
            if str(path) == '/proc/self/cgroup':
                return '0::' + str(Path(self.cgroup).parent / 'supervisor.service') + '\n'
            if path.name == 'memory.max':
                return '1024\n'
            if path.name == 'memory.swap.max':
                return '0\n'
            if str(path) == '/proc/meminfo':
                return 'MemAvailable: 4096 kB\n'
            if str(path) == '/proc/12345/cgroup':
                return '0::' + self.cgroup + '\n'
            if str(path) == '/proc/12345/stat':
                return '12345 (synthetic gate) S ' + '0 ' * 18 + '987 0\n'
            return original(path, *args, **kwargs)

        with ExitStack() as stack:
            stack.enter_context(patch.object(Path, 'read_text', kernel))
            stack.enter_context(patch.object(run, 'phase_properties', side_effect=properties,
                                             return_value=self.props))
            stack.enter_context(redirect_stdout(io.StringIO()))
            yield

    def release(self, properties=None):
        with self.manager(properties):
            return self.watch._release()

    def assert_withheld(self):
        self.assertFalse((self.receipts / 'guest-permit').exists())
        self.assertFalse(self.state['released'])


class OwnedGateTests(OwnedGateFixture):
    def test_actual_unit_only_line_releases_only_authenticated_gate(self):
        self.release()
        self.assertTrue((self.receipts / 'guest-permit').exists())
        self.assertEqual(self.state['invocation_id'], self.invocation)
        captured = json.loads((self.receipts / 'guest-owned.json').read_text())
        self.assertEqual(captured, self.identity)

    def test_legacy_full_suffix_releases_matching_authenticated_gate(self):
        self.log.write_text('Running as unit: ' + self.unit + '; invocation ID: ' + self.invocation + '\n')
        self.release()
        self.assertTrue(self.state['released'])

    def test_wrong_unit_line_withholds_permit(self):
        self.log.write_text('Running as unit: ' + self.unit + '-other\n')
        with self.assertRaisesRegex(ValueError, 'ownership|wrapper|invocation'):
            self.release()
        self.assert_withheld()

    def test_substrings_and_missing_creation_lines_do_not_prove_ownership(self):
        for line in ('', 'prefix Running as unit: ' + self.unit + '\n',
                     'Running as unit: ' + self.unit + '.other\n',
                     'Failed to start transient service unit: ' + self.unit + '\n'):
            with self.subTest(line=line):
                self.log.write_text(line)
                with self.assertRaisesRegex(ValueError, 'wrapper'):
                    self.release()
                self.assert_withheld()

    def test_foreign_full_suffix_withholds_permit(self):
        self.log.write_text('Running as unit: ' + self.unit + '; invocation ID: ' + 'f' * 32 + '\n')
        with self.assertRaisesRegex(ValueError, 'ownership|wrapper|invocation'):
            self.release()
        self.assert_withheld()

    def test_foreign_gate_nonce_withholds_permit(self):
        self.identity['launch_nonce'] = 'f' * 64
        self.ready.write_text(json.dumps(self.identity))
        with self.assertRaisesRegex(ValueError, 'ownership|nonce|invocation'):
            self.release()
        self.assert_withheld()

    def test_incomplete_manager_properties_withhold_permit(self):
        for key in ('LoadState', 'ActiveState', 'MainPID', 'ControlGroup', 'InvocationID'):
            with self.subTest(missing=key):
                props = dict(self.props)
                del props[key]
                with self.assertRaises(ValueError):
                    self.release(properties=lambda *_: props)
                self.assert_withheld()

    def test_incomplete_gate_identity_withholds_permit(self):
        for key in self.identity:
            with self.subTest(missing=key):
                identity = dict(self.identity)
                del identity[key]
                self.ready.write_text(json.dumps(identity))
                with self.assertRaises(ValueError):
                    self.release()
                self.assert_withheld()

    def test_foreign_manager_invocation_withholds_permit(self):
        self.props['InvocationID'] = 'f' * 32
        with self.assertRaises(ValueError):
            self.release()
        self.assert_withheld()

    def test_changed_identity_before_release_withholds_permit(self):
        for key, value in (('MainPID', '54321'), ('InvocationID', 'f' * 32),
                           ('ControlGroup', self.cgroup + '-other')):
            with self.subTest(changed=key):
                changed = dict(self.props, **{key: value})
                with self.assertRaisesRegex(ValueError, 'changed|ownership|invocation'):
                    self.release(properties=[self.props, changed])
                self.assert_withheld()
                (self.receipts / 'guest-owned.json').unlink(missing_ok=True)
                (self.receipts / 'guest-verified-preexec.json').unlink(missing_ok=True)
                self.state.pop('owned_identity', None)

    def test_unit_name_without_owned_launch_context_withholds_permit(self):
        self.state.pop('launch_nonce')
        with self.assertRaises(ValueError):
            self.release()
        self.assert_withheld()

    def test_reused_pid_before_release_withholds_permit(self):
        with patch.object(run, 'process_identity', side_effect=[(987, self.cgroup), (988, self.cgroup)]):
            with self.assertRaisesRegex(ValueError, 'changed'):
                self.release()
        self.assert_withheld()

    def test_not_found_precreation_observation_is_required(self):
        self.state['properties_before_creation'] = {'LoadState': 'loaded'}
        with self.assertRaisesRegex(ValueError, 'launch context'):
            self.release()
        self.assert_withheld()

    def test_gate_outside_expected_app_slice_withholds_permit(self):
        outside = '/foreign.slice/' + self.unit
        self.identity['cgroup'] = outside
        self.props['ControlGroup'] = outside
        self.ready.write_text(json.dumps(self.identity))
        with self.manager(), patch.object(run, 'process_identity', return_value=(987, outside)), \
                patch.object(run.subprocess, 'run') as stop:
            with self.assertRaisesRegex(ValueError, 'ownership|cgroup|sibling'):
                run.stop_owned_unit(self.unit, self.state)
        stop.assert_not_called()

    def test_missing_gate_receipt_cannot_authorize_cleanup(self):
        self.ready.unlink()
        with self.manager(), patch.object(run.subprocess, 'run') as stop:
            with self.assertRaises((ValueError, FileNotFoundError)):
                run.stop_owned_unit(self.unit, self.state)
        stop.assert_not_called()
        self.assert_withheld()

    def test_unreleased_owned_gate_can_be_cleaned_after_wrapper_exit(self):
        self.state['run_owned_status'] = 2
        with self.manager(), patch.object(run.subprocess, 'run') as stop:
            run.stop_owned_unit(self.unit, self.state)
        stop.assert_called_once_with(['systemctl', '--user', 'stop', self.unit], check=True, timeout=30)
        self.assertEqual(self.state['invocation_id'], self.invocation)
        self.assert_withheld()

    def test_changed_identity_before_cleanup_is_never_signalled(self):
        for key, value in (('MainPID', '54321'), ('InvocationID', 'f' * 32),
                           ('ControlGroup', self.cgroup + '-other')):
            with self.subTest(changed=key):
                changed = dict(self.props, **{key: value})
                with self.manager([self.props, changed]), patch.object(run.subprocess, 'run') as stop:
                    with self.assertRaisesRegex(ValueError, 'changed'):
                        run.stop_owned_unit(self.unit, self.state)
                stop.assert_not_called()
                self.assert_withheld()
                (self.receipts / 'guest-owned.json').unlink(missing_ok=True)
                self.state.pop('owned_identity', None)

    def test_reused_pid_before_cleanup_is_never_signalled(self):
        with self.manager(), patch.object(run.subprocess, 'run') as stop, \
                patch.object(run, 'process_identity', side_effect=[(987, self.cgroup), (988, self.cgroup)]):
            with self.assertRaisesRegex(ValueError, 'changed'):
                run.stop_owned_unit(self.unit, self.state)
        stop.assert_not_called()

    def test_incomplete_manager_identity_cannot_authorize_cleanup(self):
        for key in ('LoadState', 'ActiveState', 'MainPID', 'ControlGroup', 'InvocationID'):
            with self.subTest(missing=key):
                props = dict(self.props)
                del props[key]
                with self.manager(lambda *_: props), patch.object(run.subprocess, 'run') as stop:
                    with self.assertRaises(ValueError):
                        run.stop_owned_unit(self.unit, self.state)
                stop.assert_not_called()

    def test_exited_owned_invocation_can_clear_remain_after_exit_without_a_live_pid(self):
        with self.manager():
            run.bind_phase_ownership(self.unit, self.state, self.props)
        terminal = dict(self.props, MainPID='0', ControlGroup='', SubState='exited',
                        Result='success', ExecMainCode='1', ExecMainStatus='0')
        with self.manager(lambda *_: terminal), patch.object(run.subprocess, 'run') as stop:
            run.stop_owned_unit(self.unit, self.state)
        stop.assert_called_once()

    def test_missing_active_state_on_exited_invocation_cannot_authorize_cleanup(self):
        with self.manager():
            run.bind_phase_ownership(self.unit, self.state, self.props)
        terminal = dict(self.props, MainPID='0', ControlGroup='')
        del terminal['ActiveState']
        with self.manager([self.props, terminal]), patch.object(run.subprocess, 'run') as stop:
            with self.assertRaises(ValueError):
                run.stop_owned_unit(self.unit, self.state)
        stop.assert_not_called()

    def test_captured_identity_is_not_rebound_after_gate_receipt_changes(self):
        with self.manager():
            run.bind_phase_ownership(self.unit, self.state, self.props)
            self.identity['invocation_id'] = 'f' * 32
            self.ready.write_text(json.dumps(self.identity))
            run.bind_phase_ownership(self.unit, self.state, self.props)
        self.assertEqual(self.state['owned_identity'].invocation_id, self.invocation)
        captured = json.loads((self.receipts / 'guest-owned.json').read_text())
        self.assertEqual(captured['invocation_id'], self.invocation)

    def test_wrapper_exit_before_observer_still_cleans_authenticated_gate(self):
        self.args.run_id = 'test'
        self.args.cleanup_grace_seconds = 1
        entry = dict(self.entry, command=['synthetic'], environment={}, cwd=str(self.root))
        record = {'phases': {}}

        def wrapper_exit(command, *args, **kwargs):
            self.identity['launch_nonce'] = command[-1]
            self.ready.write_text(json.dumps(self.identity))
            return 2

        terminal = dict(self.props, MainPID='0', ControlGroup='', ActiveState='inactive',
                        Result='success', ExecMainCode='1', ExecMainStatus='0')
        with self.manager([{'LoadState': 'not-found'}, self.props, terminal, terminal]), \
                patch.object(run, 'run_owned', side_effect=wrapper_exit), \
                patch.object(run.subprocess, 'run') as stop:
            result = run.run_phase('guest', self.args, self.runroot, self.receipts,
                                   {'phases': {'guest': entry}}, record,
                                   budget=1024, headroom=2048, deadline=180)
        stop.assert_not_called()
        self.assertFalse(result['released'])
        self.assertEqual(result['invocation_id'], self.invocation)
        self.assertFalse((self.receipts / 'guest-permit').exists())


if __name__ == '__main__':
    unittest.main()
