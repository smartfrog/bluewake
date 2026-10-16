"""Owned cleanup races at actual proc-file read boundaries; no services."""

from contextlib import contextmanager
from pathlib import Path
import unittest
from unittest.mock import patch

import run
import test_phase_ownership as ownership


class CleanupIdentityRaceTests(ownership.OwnedGateFixture):
    def setUp(self):
        super().setUp()
        self.release()
        self.terminal = dict(self.props, ActiveState='inactive', SubState='dead', MainPID='0',
                             ControlGroup='', Result='success', ExecMainCode='2', ExecMainStatus='15')

    @contextmanager
    def cleanup_race(self, *, boundary='stat', error=FileNotFoundError, terminal=None, reused=False):
        observations = []
        reads = []

        def properties(*unused):
            observations.append(True)
            return self.props if len(observations) <= 2 else terminal or self.terminal

        with self.manager(properties):
            read_text = Path.read_text

            def proc_read(path, *args, **kwargs):
                if str(path) == '/proc/12345/' + boundary:
                    reads.append(str(path))
                    if reused:
                        return '12345 (replacement) S ' + '0 ' * 18 + '988 0\n'
                    raise error(2 if error is FileNotFoundError else 13, 'synthetic proc read', str(path))
                return read_text(path, *args, **kwargs)

            with patch.object(Path, 'read_text', proc_read), patch.object(run.subprocess, 'run') as signal:
                yield signal, observations, reads

    def test_disappearance_at_stat_read_accepts_fresh_owned_terminal_without_signal(self):
        with self.cleanup_race() as (signal, observations, reads):
            result = run.stop_owned_unit(self.unit, self.state)
        self.assertEqual(result, self.terminal)
        self.assertEqual(self.state['properties_at_exit'], self.terminal)
        self.assertGreaterEqual(len(reads), 1)
        self.assertEqual(len(observations), 3)
        signal.assert_not_called()

    def test_disappearance_after_stat_before_cgroup_read_accepts_fresh_owned_terminal(self):
        with self.cleanup_race(boundary='cgroup') as (signal, _, reads):
            self.assertEqual(run.stop_owned_unit(self.unit, self.state), self.terminal)
        self.assertGreaterEqual(len(reads), 1)
        signal.assert_not_called()

    def test_reused_pid_starttime_is_rejected_without_terminal_adoption(self):
        with self.cleanup_race(reused=True) as (signal, observations, _):
            with self.assertRaisesRegex(ValueError, 'ownership'):
                run.stop_owned_unit(self.unit, self.state)
        self.assertEqual(len(observations), 2)
        self.assertNotIn('properties_at_exit', self.state)
        signal.assert_not_called()

    def test_foreign_invocation_after_disappearance_is_not_adopted(self):
        with self.cleanup_race(terminal=dict(self.terminal, InvocationID='f' * 32)) as (signal, _, _):
            with self.assertRaisesRegex(ValueError, 'ownership'):
                run.stop_owned_unit(self.unit, self.state)
        self.assertNotIn('properties_at_exit', self.state)
        signal.assert_not_called()

    def test_stat_permission_denial_is_not_treated_as_absence(self):
        with self.cleanup_race(error=PermissionError) as (signal, observations, _):
            with self.assertRaises(PermissionError):
                run.stop_owned_unit(self.unit, self.state)
        self.assertEqual(len(observations), 2)
        self.assertNotIn('properties_at_exit', self.state)
        signal.assert_not_called()

    def test_cgroup_permission_denial_after_stat_is_not_treated_as_absence(self):
        with self.cleanup_race(boundary='cgroup', error=PermissionError) as (signal, observations, _):
            with self.assertRaises(PermissionError):
                run.stop_owned_unit(self.unit, self.state)
        self.assertEqual(len(observations), 2)
        self.assertNotIn('properties_at_exit', self.state)
        signal.assert_not_called()

    def test_disappearance_without_terminal_status_is_unverified(self):
        with self.cleanup_race(terminal=self.props) as (signal, _, _):
            with self.assertRaisesRegex(ValueError, 'UNVERIFIED'):
                run.stop_owned_unit(self.unit, self.state)
        self.assertNotIn('properties_at_exit', self.state)
        signal.assert_not_called()

    def test_unbound_disappeared_gate_is_not_adopted_as_terminal(self):
        self.state.pop('owned_identity')
        with self.cleanup_race() as (signal, _, _):
            with self.assertRaises(FileNotFoundError):
                run.stop_owned_unit(self.unit, self.state)
        self.assertNotIn('properties_at_exit', self.state)
        signal.assert_not_called()


if __name__ == '__main__':
    unittest.main()
