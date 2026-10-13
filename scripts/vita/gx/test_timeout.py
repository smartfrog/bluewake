from contextlib import redirect_stderr
import io
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import launch
import watch


class TimeoutTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)
        for name in ('phases', 'evidence', 'work'):
            (self.root / name).mkdir()

    def tearDown(self):
        self.temporary.cleanup()

    def test_cli_rejects_invalid_timeout_before_building(self):
        socket = self.root / 'wayland'
        socket.touch()
        for value in ('nan', 'inf', '-inf', '0', '-1', '601'):
            with self.subTest(timeout=value), \
                    patch.object(launch.sys, 'argv', ['launch.py', '--timeout=' + value,
                                                     '--wayland-socket', str(socket),
                                                     '--output', str(self.root / 'output')]), \
                    patch.object(launch, 'build_vpk', side_effect=AssertionError('unexpected build')) as builder, \
                    redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    launch.main()
                self.assertEqual(error.exception.code, 2)
                builder.assert_not_called()
                self.assertFalse((self.root / 'output').exists())

    def test_both_phase_runners_reject_invalid_deadlines_without_side_effects(self):
        for runner in (launch.run_phase, watch.run_phase):
            for value in (float('nan'), float('inf'), -float('inf'), 0, -1, 601):
                with self.subTest(runner=runner.__module__, timeout=value), \
                        patch.object(launch.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as plain, \
                        patch.object(watch.subprocess, 'Popen', return_value=SimpleNamespace(
                            poll=lambda: 0, returncode=0)) as watched:
                    with self.assertRaisesRegex(ValueError, 'finite, positive.*600'):
                        runner(self.root, self.root, 'run', [], value, None)
                    plain.assert_not_called()
                    watched.assert_not_called()
                    self.assertEqual(list((self.root / 'phases').iterdir()), [])
                    self.assertEqual(list((self.root / 'evidence').iterdir()), [])

    def test_accepts_finite_positive_deadlines_up_to_ceiling(self):
        for value in (0.001, 90, 120, 600):
            with self.subTest(timeout=value):
                self.assertIsNone(launch.validate_timeout(value))


if __name__ == '__main__':
    unittest.main()
