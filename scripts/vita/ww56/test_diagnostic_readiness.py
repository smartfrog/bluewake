"""Synthetic check-only diagnostic readiness; never execute a guest."""

from contextlib import ExitStack, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import tempfile
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import run
from fp_profile import CONFIG_PATH, FIRMWARE_DIRECTORY, profile_config


class DiagnosticReadinessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.profile = self.root / 'profile'
        self.directory = self.profile / run.REPORT_DIRECTORY
        self.directory.mkdir(parents=True)
        self.package = self.root / 'synthetic.vpk'
        self.package.write_bytes(b'public synthetic artifact, not a game package')
        self.args = SimpleNamespace(
            diagnostic_profile=self.profile, request_token='a' * 32,
            guest_source='b' * 40, vpk=self.package,
            vpk_sha256=hashlib.sha256(self.package.read_bytes()).hexdigest(),
            continue_gx=64, audio='deferred', check=True)
        self.identity = f'{self.args.request_token} {self.args.guest_source} {self.args.vpk_sha256}'
        self.contents = {'ww05.request': self.identity + '\n',
                         'ww05.continue': self.identity + ' 64\n',
                         'ww05.audio': self.identity + ' 1\n'}
        for name, content in self.contents.items():
            (self.directory / name).write_text(content)
        config = self.profile / CONFIG_PATH
        config.parent.mkdir(parents=True)
        config.write_bytes(profile_config(alt=True, surface_sync=False,
                                          pref_path=self.profile / FIRMWARE_DIRECTORY))

    def check(self):
        return run.diagnostic_readiness(self.args)

    def test_valid_inputs_bind_the_actual_synthetic_package(self):
        plan = self.check()
        self.assertEqual(plan['vpk_sha256'], hashlib.sha256(self.package.read_bytes()).hexdigest())
        self.assertEqual(plan['continue_gx'], 64)
        self.assertEqual(plan['audio'], 'deferred')
        self.assertFalse(plan['launch_authorized'])

    def test_missing_requests_fail_closed(self):
        for name in self.contents:
            with self.subTest(request=name):
                path = self.directory / name
                path.unlink()
                with self.assertRaisesRegex(ValueError, name):
                    self.check()
                path.write_text(self.contents[name])

    def test_malformed_requests_fail_closed(self):
        for name in self.contents:
            with self.subTest(request=name):
                path = self.directory / name
                path.write_text('malformed\n')
                with self.assertRaisesRegex(ValueError, name):
                    self.check()
                path.write_text(self.contents[name])

    def test_request_identity_mismatches_fail_closed(self):
        for name in self.contents:
            for value in (self.args.request_token, self.args.guest_source, self.args.vpk_sha256):
                with self.subTest(request=name, identity_length=len(value)):
                    path = self.directory / name
                    path.write_text(self.contents[name].replace(value, 'c' * len(value)))
                    with self.assertRaisesRegex(ValueError, name):
                        self.check()
                    path.write_text(self.contents[name])

    def test_wrong_continuation_counts_fail_closed(self):
        for count in (0, 1, 63, 65, -1):
            with self.subTest(count=count):
                (self.directory / 'ww05.continue').write_text(self.identity + f' {count}\n')
                with self.assertRaisesRegex(ValueError, 'ww05.continue'):
                    self.check()

    def test_non_deferred_audio_requests_fail_closed(self):
        (self.directory / 'ww05.audio').write_text(self.identity + ' 0\n')
        with self.assertRaisesRegex(ValueError, 'ww05.audio'):
            self.check()

    def test_changed_package_fails_closed(self):
        self.package.write_bytes(b'different synthetic artifact')
        with self.assertRaisesRegex(ValueError, 'package.*SHA-256'):
            self.check()

    def test_missing_candidate_identity_is_incomplete(self):
        self.args.vpk_sha256 = None
        with self.assertRaisesRegex(ValueError, 'candidate.*identity'):
            self.check()

    def test_invalid_identity_arguments_fail_closed(self):
        for field in ('request_token', 'guest_source', 'vpk_sha256'):
            with self.subTest(field=field):
                original = getattr(self.args, field)
                setattr(self.args, field, 'not-hex')
                with self.assertRaisesRegex(ValueError, 'incomplete readiness'):
                    self.check()
                setattr(self.args, field, original)

    def test_wrong_profile_fails_closed(self):
        config = self.profile / CONFIG_PATH
        approved = config.read_bytes()
        for old, new in ((b'cpu-opt: false', b'cpu-opt: true'),
                         (b'backend-renderer: OpenGL', b'backend-renderer: Vulkan'),
                         (b'disable-surface-sync: true', b'disable-surface-sync: false')):
            with self.subTest(setting=old):
                config.write_bytes(approved.replace(old, new))
                with self.assertRaisesRegex(ValueError, 'profile'):
                    self.check()

    def test_capture_requests_and_old_outputs_fail_closed(self):
        for name in ('ww05.mem1', 'ww05.mem1-start', 'ww05.mem1-end', 'ww05.mem1-extra',
                     'frames', 'ww05.partial', 'ww05.report', 'ww05.log', 'ww05.dispatch'):
            with self.subTest(path=name):
                path = self.directory / name
                path.touch()
                with self.assertRaisesRegex(ValueError, name):
                    self.check()
                path.unlink()

    def test_symlinked_request_fails_closed(self):
        path = self.directory / 'ww05.request'
        path.rename(self.root / 'request')
        path.symlink_to(self.root / 'request')
        with self.assertRaisesRegex(ValueError, 'ww05.request'):
            self.check()

    def test_non_diagnostic_options_fail_closed(self):
        for field, value in (('continue_gx', 63), ('audio', 'strict')):
            with self.subTest(option=field):
                original = getattr(self.args, field)
                setattr(self.args, field, value)
                with self.assertRaisesRegex(ValueError, 'continuation 64.*deferred'):
                    self.check()
                setattr(self.args, field, original)

    def test_helper_refuses_non_check_use(self):
        self.args.check = False
        with self.assertRaisesRegex(ValueError, 'check-only'):
            self.check()

    def test_valid_check_prints_readiness_without_live_execution_or_writes(self):
        before = {str(path): None if path.is_dir() else path.read_bytes()
                  for path in self.root.rglob('*')}
        output = io.StringIO()
        with ExitStack() as stack:
            for name in ('check_supervisor', 'check_launcher', 'check_inputs', 'check_freshness'):
                stack.enter_context(patch.object(run, name, return_value={'synthetic': True}))
            for name in ('run_trial', 'stage_trial', 'run_phase', 'fresh_request', 'capture_snapshot'):
                stack.enter_context(patch.object(run, name, side_effect=AssertionError('live execution')))
            for name in ('Popen', 'run', 'check_output'):
                stack.enter_context(patch.object(run.subprocess, name, side_effect=AssertionError('process')))
            stack.enter_context(redirect_stdout(output))
            self.assertEqual(run.check_mode(self.args), 0)
        result = json.loads(output.getvalue())
        self.assertEqual(result['status'], 'ready-for-supervised-review')
        self.assertFalse(result['plan']['launch_authorized'])
        self.assertEqual(result['plan']['observation'],
                         {'snapshots': False, 'memwatcher': False, 'ram_poll': False})
        self.assertEqual(before, {str(path): None if path.is_dir() else path.read_bytes()
                                  for path in self.root.rglob('*')})

    def test_invalid_diagnostic_inputs_stop_before_legacy_checks(self):
        (self.directory / 'ww05.audio').unlink()
        with patch.object(run, 'check_supervisor', side_effect=AssertionError('late check')):
            with redirect_stdout(io.StringIO()) as output:
                self.assertEqual(run.check_mode(self.args), 2)
        self.assertEqual(json.loads(output.getvalue())['status'], 'blocked')

    def test_failed_legacy_gate_never_returns_a_ready_plan(self):
        with ExitStack() as stack:
            for name in ('check_launcher', 'check_inputs', 'check_freshness'):
                stack.enter_context(patch.object(run, name, return_value={}))
            stack.enter_context(patch.object(run, 'check_supervisor', side_effect=ValueError('wrong cgroup')))
            with redirect_stdout(io.StringIO()) as output:
                self.assertEqual(run.check_mode(self.args), 2)
        result = json.loads(output.getvalue())
        self.assertEqual(result['status'], 'blocked')
        self.assertNotIn('plan', result)


class DiagnosticCommandLineTests(unittest.TestCase):
    def arguments(self, *extra):
        return ['run.py', '--expected-head', 'a' * 40, '--vpk', '/synthetic.vpk',
                '--vpk-sha256', 'b' * 64, '--guest-source', 'c' * 40,
                '--disc', '/synthetic.iso', '--disc-sha256', 'd' * 64,
                '--wayland-socket', '/synthetic.socket', '--run-id', 'synthetic', *extra]

    def test_diagnostic_cli_requires_check(self):
        with patch.object(sys, 'argv', self.arguments('--diagnostic-profile', '/profile',
                                                      '--request-token', 'e' * 32)):
            with redirect_stdout(io.StringIO()), patch('sys.stderr', new_callable=io.StringIO):
                with self.assertRaises(SystemExit) as error:
                    run.parse_arguments()
        self.assertEqual(error.exception.code, 2)

    def test_diagnostic_cli_requires_explicit_token(self):
        with patch.object(sys, 'argv', self.arguments('--check', '--diagnostic-profile', '/profile')):
            with patch('sys.stderr', new_callable=io.StringIO):
                with self.assertRaises(SystemExit) as error:
                    run.parse_arguments()
        self.assertEqual(error.exception.code, 2)

    def test_legacy_cli_defaults_are_unchanged(self):
        with patch.object(sys, 'argv', self.arguments()):
            args = run.parse_arguments()
        self.assertIsNone(args.diagnostic_profile)
        self.assertFalse(args.check)
        self.assertEqual(args.continue_gx, 64)
        self.assertEqual(args.audio, 'deferred')

    def test_diagnostic_cli_dispatches_only_to_check(self):
        with patch.object(sys, 'argv', self.arguments('--check', '--diagnostic-profile', '/profile',
                                                      '--request-token', 'e' * 32)):
            with patch.object(run, 'check_mode', return_value=0) as check:
                with patch.object(run, 'run_trial', side_effect=AssertionError('live trial')):
                    self.assertEqual(run.main(), 0)
        self.assertEqual(check.call_args.args[0].diagnostic_profile, Path('/profile'))


if __name__ == '__main__':
    unittest.main()
