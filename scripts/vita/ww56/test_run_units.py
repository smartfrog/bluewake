"""Tests for transient phase-unit ownership.

WW56 creates one transient unit per phase. A unit that already exists (from an
earlier run, a foreign tool or a leftover failure) must never be stopped by
this run: the run refuses to reuse it before spawning, and cleanup stops only
the invocation this run created, proven by the unit's InvocationID (or the
wrapper's own "Running as unit: ...; invocation ID: ..." line).
"""

import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import run

UNIT = 'ww56-collide-install.service'
INVOCATION = 'a' * 32
FOREIGN = 'f' * 32


class FreshUnitTests(unittest.TestCase):
    def test_a_loaded_unit_is_refused(self):
        with patch.object(run, 'properties', return_value={'LoadState': 'loaded', 'ActiveState': 'active'}):
            with self.assertRaisesRegex(ValueError, 'already exists'):
                run.require_fresh_unit(UNIT)

    def test_a_not_found_unit_is_accepted(self):
        with patch.object(run, 'properties', return_value={'LoadState': 'not-found'}):
            self.assertEqual(run.require_fresh_unit(UNIT)['LoadState'], 'not-found')


class WrapperInvocationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)
        (self.root / 'evidence').mkdir()

    def tearDown(self):
        self.temporary.cleanup()

    def test_reads_the_invocation_from_the_owned_wrapper_line(self):
        (self.root / 'evidence/process.log').write_text(
            'Running as unit: ' + UNIT + '; invocation ID: ' + INVOCATION + '\n')
        self.assertEqual(run.wrapper_invocation(self.root, UNIT), INVOCATION)

    def test_other_units_and_missing_logs_have_no_invocation(self):
        (self.root / 'evidence/process.log').write_text(
            'Running as unit: ww56-other-guest.service; invocation ID: ' + INVOCATION + '\n')
        self.assertIsNone(run.wrapper_invocation(self.root, UNIT))
        self.assertIsNone(run.wrapper_invocation(self.root / 'missing', UNIT))
        self.assertIsNone(run.wrapper_invocation(None, UNIT))

    def test_a_failed_creation_line_has_no_invocation(self):
        (self.root / 'evidence/process.log').write_text(
            'Failed to start transient service unit: Unit ' + UNIT + ' was already loaded.\n')
        self.assertIsNone(run.wrapper_invocation(self.root, UNIT))


class CleanupOwnershipTests(unittest.TestCase):
    def state(self, **overrides):
        values = {'unit': UNIT, 'invocation_id': INVOCATION, 'phase_root': None}
        values.update(overrides)
        return values

    def test_matching_invocation_is_stopped(self):
        state = self.state()
        current = {'ControlGroup': '/user.slice/x/app.slice/' + UNIT, 'InvocationID': INVOCATION}
        with patch.object(run, 'properties', return_value=current), \
                patch.object(run.subprocess, 'run') as stop:
            run.cleanup_phase_unit(UNIT, state)
        stop.assert_called_once()
        self.assertEqual(stop.call_args.args[0],
                         ['systemctl', '--user', 'stop', UNIT])
        self.assertEqual(state['properties_before_cleanup'], current)

    def test_a_foreign_invocation_is_never_stopped(self):
        state = self.state()
        current = {'ControlGroup': '/user.slice/x/app.slice/' + UNIT, 'InvocationID': FOREIGN}
        with patch.object(run, 'properties', return_value=current), \
                patch.object(run.subprocess, 'run') as stop:
            with self.assertRaisesRegex(ValueError, 'refusing to stop'):
                run.cleanup_phase_unit(UNIT, state)
        stop.assert_not_called()
        self.assertEqual(state['foreign_unit_untouched']['current'], current)

    def test_a_unit_with_no_recorded_invocation_is_never_stopped(self):
        state = self.state(invocation_id=None)
        current = {'ControlGroup': '/user.slice/x/app.slice/' + UNIT, 'InvocationID': FOREIGN}
        with patch.object(run, 'properties', return_value=current), \
                patch.object(run.subprocess, 'run') as stop:
            with self.assertRaisesRegex(ValueError, 'refusing to stop'):
                run.cleanup_phase_unit(UNIT, state)
        stop.assert_not_called()

    def test_a_missing_control_group_stops_nothing(self):
        state = self.state()
        current = {'ControlGroup': '', 'InvocationID': ''}
        with patch.object(run, 'properties', return_value=current), \
                patch.object(run.subprocess, 'run') as stop:
            run.cleanup_phase_unit(UNIT, state)
        stop.assert_not_called()

    def test_cleanup_falls_back_to_the_wrapper_invocation(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as temporary:
            phase_root = Path(temporary)
            (phase_root / 'evidence').mkdir()
            (phase_root / 'evidence/process.log').write_text(
                'Running as unit: ' + UNIT + '; invocation ID: ' + INVOCATION + '\n')
            state = self.state(invocation_id=None, phase_root=str(phase_root))
            current = {'ControlGroup': '/user.slice/x/app.slice/' + UNIT, 'InvocationID': INVOCATION}
            with patch.object(run, 'properties', return_value=current), \
                    patch.object(run.subprocess, 'run') as stop:
                run.cleanup_phase_unit(UNIT, state)
            stop.assert_called_once()


class PhaseCollisionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)
        (self.root / 'receipts').mkdir()
        (self.root / 'phase/evidence').mkdir(parents=True)
        self.args = SimpleNamespace(run_id='collide')
        self.plan = {'phases': {'install': {'evidence_root': str(self.root / 'phase'),
                                            'command': ['/usr/bin/true'], 'environment': {},
                                            'cwd': str(self.root)}}}
        self.record = {'phases': {}}

    def tearDown(self):
        self.temporary.cleanup()

    def test_an_existing_foreign_unit_is_refused_and_never_stopped(self):
        foreign = {'LoadState': 'loaded', 'ActiveState': 'active', 'InvocationID': FOREIGN,
                   'ControlGroup': '/user.slice/x/app.slice/' + UNIT}
        with patch.object(run, 'properties', return_value=foreign), \
                patch.object(run, 'run_owned') as owned, \
                patch.object(run.subprocess, 'run') as stop:
            with self.assertRaisesRegex(ValueError, 'already exists'):
                run.run_phase('install', self.args, self.root, self.root / 'receipts', self.plan,
                              self.record, budget=1024, headroom=2048, deadline=60)
        owned.assert_not_called()
        stop.assert_not_called()


if __name__ == '__main__':
    unittest.main()
