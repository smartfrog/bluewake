import os
from pathlib import Path
import subprocess
import time
import unittest

import test_hw_renderer_launch as launch_tests
from test_hw_renderer import NODE, launch


class MountedRendererGuardTest(unittest.TestCase):
    def setUp(self):
        self.fixture = launch_tests.RendererLaunchTest('test_software_spawn_keeps_the_same_command_and_no_pass_fds')
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root = self.fixture.root
        self.marker = self.root / 'data/guest-marker'
        self.application = ['/stack/vita3k/Fake', 'first', 'with spaces', '--flag']
        fake = self.root / 'runtime/Fake'
        fake.write_text('#!/usr/bin/python3\nimport os,sys\nfrom pathlib import Path\n'
                        "assert os.environ.get('LD_LIBRARY_PATH') == '/stack/vita3k-env/lib'\n"
                        "Path('/run/ww03/data/guest-marker').write_text('|'.join(sys.argv[1:]))\n")
        fake.chmod(0o700)
        info = Path('/dev/null').lstat()
        self.renderer = {'mode': 'hardware-requested', 'render_node': {
            'path': str(NODE), 'device': info.st_dev, 'inode': info.st_ino,
            'major': os.major(info.st_rdev), 'minor': os.minor(info.st_rdev)}}

    def run_guard(self, source=Path('/dev/null'), renderer=None, symlink=False):
        expected = renderer or self.renderer
        baseline = launch.sandbox_command(self.root, 'Fake', [])
        binding = (['--symlink', '../null', str(NODE)] if symlink else
                   ['--dev-bind', str(source), str(NODE)])
        command = [*baseline[:-1], '--dir', str(NODE.parent), *binding,
                   *launch.renderer_guard_command(self.application, expected, '/stack/vita3k-env/lib')]
        result = subprocess.run(command, cwd=self.root / 'work', env={'PATH': '/usr/bin', 'LANG': 'C.UTF-8'},
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=2)
        (self.root / 'evidence/process.log').write_bytes(result.stdout)
        line = result.stdout.splitlines(keepends=True)[0] if result.stdout else b''
        process = {'renderer_launch': {'postmount_validation': launch.renderer_guard_status(line, expected)}}
        return result.returncode, process

    def test_matching_mounted_character_device_executes_the_exact_marker_argv(self):
        status, process = self.run_guard()
        self.assertEqual(status, 0, (self.root / 'evidence/process.log').read_text())
        self.assertEqual(self.marker.read_text(), 'first|with spaces|--flag')
        self.assertEqual(process['renderer_launch']['postmount_validation'], 'VERIFIED')

    def test_substitution_after_authorization_before_bind_refuses_before_marker_exec(self):
        status, process = self.run_guard(source=Path('/dev/zero'))
        self.assertEqual(status, 126, (self.root / 'evidence/process.log').read_text())
        self.assertFalse(self.marker.exists())
        self.assertEqual(process['renderer_launch']['postmount_validation'], 'REFUSED')

    def test_regular_file_is_refused_before_marker_exec(self):
        status, process = self.run_guard(source=self.fixture.node)
        self.assertEqual(status, 126)
        self.assertFalse(self.marker.exists())
        self.assertEqual(process['renderer_launch']['postmount_validation'], 'REFUSED')

    def test_symlink_is_refused_without_following_it_before_marker_exec(self):
        status, process = self.run_guard(symlink=True)
        self.assertEqual(status, 126)
        self.assertFalse(self.marker.exists())
        self.assertEqual(process['renderer_launch']['postmount_validation'], 'REFUSED')

    def test_missing_bind_source_fails_before_marker_exec_without_claiming_guard_validation(self):
        status, process = self.run_guard(source=self.fixture.node.with_name('absent'))
        self.assertNotEqual(status, 0)
        self.assertFalse(self.marker.exists())
        self.assertEqual(process['renderer_launch']['postmount_validation'], 'UNOBSERVED')

    def test_exec_failure_is_not_hidden_after_successful_mounted_validation(self):
        (self.root / 'runtime/Fake').unlink()
        status, process = self.run_guard()
        self.assertNotEqual(status, 0)
        self.assertFalse(self.marker.exists())
        self.assertEqual(process['renderer_launch']['postmount_validation'], 'VERIFIED')
        self.assertIn('FileNotFoundError', (self.root / 'evidence/process.log').read_text())

    def test_unobserved_or_wrong_identity_evidence_is_never_reported_as_verified(self):
        for line in (b'', b'unrelated output\n',
                     b'WW24_RENDER_NODE_GUARD VERIFIED device=0 inode=0 major=226 minor=128\n'):
            with self.subTest(line=line):
                self.assertEqual(launch.renderer_guard_status(line, self.renderer), 'UNOBSERVED')

    def test_bind_mount_keeps_its_inode_when_the_host_name_is_recreated_after_mount(self):
        fake = self.root / 'runtime/Fake'
        fake.write_text('#!/usr/bin/python3\nfrom pathlib import Path\nimport time\n'
                        "data=Path('/run/ww03/data')\n(data/'mount-ready').touch()\n"
                        "while not (data/'continue').exists(): time.sleep(.005)\n"
                        f'node=Path({str(NODE)!r})\n'
                        f'assert node.stat().st_ino == {self.fixture.inode}\n'
                        "assert node.read_text() == 'authorized object A'\n"
                        "(data/'mount-proof').write_text('inode A remained mounted')\n")
        command = launch.sandbox_command(self.root, 'Fake', [])
        command[-1:-1] = ['--dir', str(NODE.parent), '--dev-bind', str(self.fixture.node), str(NODE)]
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              env={'PATH': '/usr/bin', 'LANG': 'C.UTF-8'}) as process:
            try:
                deadline = time.monotonic() + 2
                while not (self.root / 'data/mount-ready').exists():
                    self.assertIsNone(process.poll())
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(.005)
                self.fixture.replace_node(remove_original=True)
                (self.root / 'data/continue').touch()
                output, _ = process.communicate(timeout=2)
                self.assertEqual(process.returncode, 0, output.decode())
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=2)
        self.assertEqual((self.root / 'data/mount-proof').read_text(), 'inode A remained mounted')

    def test_each_mounted_identity_field_must_match_authorization(self):
        for field in ('device', 'inode', 'major', 'minor'):
            with self.subTest(field=field):
                renderer = {'mode': self.renderer['mode'], 'render_node': dict(self.renderer['render_node'])}
                renderer['render_node'][field] += 1
                status, process = self.run_guard(renderer=renderer)
                self.assertEqual(status, 126)
                self.assertFalse(self.marker.exists())
                self.assertEqual(process['renderer_launch']['postmount_validation'], 'REFUSED')


if __name__ == '__main__':
    unittest.main()
