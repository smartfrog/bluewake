from contextlib import contextmanager, ExitStack
import errno
import json
import os
from pathlib import Path
import signal
import stat
import sys
from types import SimpleNamespace
import unittest
from unittest import mock
import zipfile

import test_fp_campaign as campaign_tests
from test_hw_renderer import NODE, SOFTWARE, launch, node_stat, ww05_launch


class RendererLaunchTest(unittest.TestCase):
    def setUp(self):
        campaign_tests.CampaignTest.setUp(self)
        self.node = self.output / 'nodes/renderD128'
        self.node.parent.mkdir()
        self.node.write_text('authorized object A')
        self.inode = self.node.stat().st_ino
        self.renderer = {'mode': 'hardware-requested', 'render_node': {
            'path': str(NODE), 'major': 226, 'minor': 128, 'device': 9, 'inode': self.inode}}
        self.root = self.output / 'run'
        launch.create_run(self.root)
        with node_stat(inode=self.inode):
            self.command = launch.sandbox_command(self.root, 'Fake', [], render_node=NODE)
        self.opened = []
        self.closed = []
        self.real_open, self.real_close, self.real_fstat = os.open, os.close, os.fstat

    def replace_node(self, remove_original=False):
        original = self.node.with_name('original')
        self.node.rename(original)
        if remove_original:
            original.unlink()
        self.node.write_text('unauthorized object B')
        self.assertNotEqual(self.node.stat().st_ino, self.inode)

    @contextmanager
    def virtual_node(self):
        directory_fd = launch.directory_fd
        live = set()

        def parent(path):
            return directory_fd(self.node.parent if Path(path) == NODE.parent else path)

        def open_node(path, flags, *args, **kwargs):
            fd = self.real_open(path, flags, *args, **kwargs)
            if path == NODE.name and flags & os.O_PATH:
                self.opened.append((fd, flags))
                live.add(fd)
            return fd

        def inspect(fd):
            info = self.real_fstat(fd)
            if fd in live and stat.S_ISREG(info.st_mode):
                return SimpleNamespace(st_mode=stat.S_IFCHR | 0o666,
                    st_rdev=os.makedev(226, 128), st_dev=9, st_ino=info.st_ino)
            return info

        def close(fd):
            if fd in live:
                live.remove(fd)
                self.closed.append(fd)
            self.real_close(fd)

        with node_stat(inode=self.inode), mock.patch.object(launch, 'directory_fd', side_effect=parent), \
                mock.patch.object(os, 'open', side_effect=open_node), \
                mock.patch.object(os, 'fstat', side_effect=inspect), \
                mock.patch.object(os, 'close', side_effect=close):
            yield

    def fake_process(self, wait=None):
        process = mock.MagicMock()
        process.__enter__.return_value = process
        process.pid, process.returncode = 12345, 0
        process.poll.return_value = 0
        process.wait.side_effect = wait or (lambda **kwargs: 0)
        return process

    def assert_pinned(self, command, options):
        index = command.index('--dev-bind')
        source, destination = command[index + 1:index + 3]
        fd, = options['pass_fds']
        self.assertEqual(source, f'/proc/self/fd/{fd}')
        self.assertEqual(destination, str(NODE))
        self.assertEqual(self.real_fstat(fd).st_ino, self.inode)
        self.assertEqual(self.opened[-1][1], os.O_PATH | os.O_NOFOLLOW | os.O_CLOEXEC)
        self.assertFalse(os.get_inheritable(fd))
        self.assertEqual(Path(source).read_text(), 'authorized object A')
        return fd

    def test_substitution_after_validation_fails_before_spawn(self):
        self.replace_node()
        with self.virtual_node(), mock.patch.object(launch.subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(ValueError, 'render node.*changed'):
                launch.run_owned(self.command, self.root, 1, renderer=self.renderer)
            spawn.assert_not_called()
        self.assertEqual(self.closed, [fd for fd, _ in self.opened])

    def test_substitution_after_fstat_preserves_preflight_fd_without_claiming_postmount_validation(self):
        planned = list(self.command)
        observed = {}

        def spawn(command, **options):
            self.replace_node()
            fd = self.assert_pinned(command, options)
            observed.update(command=command, pass_fds=list(options['pass_fds']))

            def wait(**kwargs):
                with self.assertRaises(OSError):
                    self.real_fstat(fd)
                return 0

            return self.fake_process(wait)

        with self.virtual_node(), mock.patch.object(launch.subprocess, 'Popen', side_effect=spawn):
            self.assertEqual(launch.run_owned(self.command, self.root, 1, renderer=self.renderer), 0)
        self.assertEqual(self.command, planned)
        self.assertEqual(self.closed, [fd for fd, _ in self.opened])
        process = json.loads((self.root / 'evidence/process.json').read_text())
        self.assertEqual(process['renderer_launch']['renderer'], self.renderer)
        self.assertTrue(process['renderer_launch']['preflight_validated'])
        self.assertEqual(process['renderer_launch']['postmount_validation'], 'UNOBSERVED')
        self.assertEqual(process['renderer_launch']['command'], observed['command'])
        self.assertEqual(process['renderer_launch']['pass_fds'], observed['pass_fds'])

    def test_deleted_authorized_object_fails_closed_before_marker_exec(self):
        fake = self.root / 'runtime/Fake'
        fake.write_text("#!/usr/bin/python3\nfrom pathlib import Path\n"
                        "Path('/run/ww03/data/guest-marker').write_text('unsafe exec')\n")
        fake.chmod(0o700)
        popen = launch.subprocess.Popen

        def spawn(command, **options):
            self.replace_node(remove_original=True)
            self.assert_pinned(command, options)
            return popen(command, **options)

        with self.virtual_node(), mock.patch.object(launch.subprocess, 'Popen', side_effect=spawn):
            status = launch.run_owned(self.command, self.root, 1, renderer=self.renderer)
        self.assertNotEqual(status, 0)
        self.assertFalse((self.root / 'data/guest-marker').exists())
        self.assertEqual(self.closed, [fd for fd, _ in self.opened])
        record = json.loads((self.root / 'evidence/process.json').read_text())
        self.assertTrue(record['renderer_launch']['preflight_validated'])
        self.assertEqual(record['renderer_launch']['postmount_validation'], 'UNOBSERVED')

    def test_symlink_substitution_at_open_is_rejected_without_following_it(self):
        with self.virtual_node():
            open_node = os.open

            def substitute(path, flags, *args, **kwargs):
                if path == NODE.name and flags & os.O_PATH:
                    self.replace_node()
                    self.node.unlink()
                    self.node.symlink_to('original')
                return open_node(path, flags, *args, **kwargs)

            with mock.patch.object(os, 'open', side_effect=substitute), \
                    mock.patch.object(launch.subprocess, 'Popen', side_effect=AssertionError('unsafe spawn')) as spawn:
                with self.assertRaisesRegex(ValueError, 'render node'):
                    launch.run_owned(self.command, self.root, 1, renderer=self.renderer)
                spawn.assert_not_called()
        self.assertEqual(self.closed, [fd for fd, _ in self.opened])

    def test_failed_command_construction_closes_the_descriptor_before_spawn(self):
        class InvalidCommand(list):
            def __iter__(self):
                raise RuntimeError('synthetic command construction failure')

        with self.virtual_node(), mock.patch.object(launch.subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(RuntimeError, 'synthetic command construction failure'):
                launch.run_owned(InvalidCommand(self.command), self.root, 1, renderer=self.renderer)
            spawn.assert_not_called()
        self.assertEqual(self.closed, [fd for fd, _ in self.opened])

    def test_failed_fstat_closes_the_descriptor_before_spawn(self):
        def inspect(fd):
            if self.opened and fd == self.opened[-1][0]:
                raise OSError(errno.EIO, 'synthetic fstat failure')
            return self.real_fstat(fd)

        with self.virtual_node(), mock.patch.object(os, 'fstat', side_effect=inspect), \
                mock.patch.object(launch.subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(OSError, 'synthetic fstat failure'):
                launch.run_owned(self.command, self.root, 1, renderer=self.renderer)
            spawn.assert_not_called()
        self.assertEqual(self.closed, [fd for fd, _ in self.opened])

    def test_failed_open_never_spawns(self):
        def open_node(path, flags, *args, **kwargs):
            if path == NODE.name and flags & os.O_PATH:
                raise PermissionError(errno.EACCES, 'synthetic open failure')
            return self.real_open(path, flags, *args, **kwargs)

        with self.virtual_node(), mock.patch.object(os, 'open', side_effect=open_node), \
                mock.patch.object(launch.subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(OSError, 'synthetic open failure'):
                launch.run_owned(self.command, self.root, 1, renderer=self.renderer)
            spawn.assert_not_called()
        self.assertFalse(self.opened)

    def test_failed_spawn_or_pass_fds_closes_the_descriptor(self):
        for index, error in enumerate((OSError(errno.EBADF, 'synthetic pass_fds failure'), RuntimeError('synthetic spawn failure'))):
            evidence = self.output / f'failed-{index}'
            launch.create_run(evidence)
            with self.subTest(error=error), self.virtual_node(), \
                    mock.patch.object(launch.subprocess, 'Popen', side_effect=error):
                with self.assertRaises(type(error)):
                    launch.run_owned(self.command, self.root, 1, renderer=self.renderer, evidence_root=evidence)
                self.assertEqual(self.closed, [fd for fd, _ in self.opened])

    def test_close_error_after_spawn_still_terminates_and_reaps_the_owned_process(self):
        process = self.fake_process()
        process.poll.return_value = None
        failed = False
        with self.virtual_node():
            close = os.close

            def fail_after_close(fd):
                nonlocal failed
                close(fd)
                if self.opened and fd == self.opened[-1][0] and not failed:
                    failed = True
                    raise OSError(errno.EIO, 'synthetic close failure')

            with mock.patch.object(os, 'close', side_effect=fail_after_close), \
                    mock.patch.object(launch.subprocess, 'Popen', return_value=process), \
                    mock.patch.object(os, 'killpg') as kill:
                with self.assertRaisesRegex(OSError, 'synthetic close failure'):
                    launch.run_owned(self.command, self.root, 1, renderer=self.renderer)
                kill.assert_called_once_with(process.pid, signal.SIGTERM)
                process.wait.assert_called_once_with(timeout=2)
        self.assertEqual(self.closed, [fd for fd, _ in self.opened])

    def test_software_spawn_keeps_the_same_command_and_no_pass_fds(self):
        command = launch.sandbox_command(self.root, 'Fake', [])
        with mock.patch.object(launch.subprocess, 'Popen', return_value=self.fake_process()) as spawn, \
                mock.patch.object(launch, 'directory_fd', side_effect=launch.directory_fd) as directories:
            self.assertEqual(launch.run_owned(command, self.root, 1, renderer=SOFTWARE), 0)
        self.assertEqual(spawn.call_args.args[0], command)
        self.assertNotIn('pass_fds', spawn.call_args.kwargs)
        self.assertNotIn(mock.call(NODE.parent), directories.call_args_list)
        self.assertNotIn('renderer_launch', json.loads((self.root / 'evidence/process.json').read_text()))

    def test_hardware_command_without_an_authorized_identity_cannot_spawn(self):
        with mock.patch.object(launch.subprocess, 'Popen', side_effect=AssertionError('unsafe spawn')) as spawn:
            with self.assertRaisesRegex(ValueError, 'render node.*identity'):
                launch.run_owned(self.command, self.root, 1)
            spawn.assert_not_called()

    def test_changed_command_destination_cannot_expose_another_device(self):
        command = list(self.command)
        command[command.index('--dev-bind') + 2] = '/dev/input/event0'
        with self.virtual_node(), mock.patch.object(launch.subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(ValueError, 'render node.*command'):
                launch.run_owned(command, self.root, 1, renderer=self.renderer)
            spawn.assert_not_called()

    def test_fp_single_phase_passes_the_authorized_identity_to_the_pinned_spawn(self):
        argv = [argument for argument in self.argv if argument != '--two-phase']
        argv.extend(['--render-node', str(NODE), '--execute-reviewed'])

        def spawn(command, **options):
            self.replace_node()
            self.assert_pinned(command, options)
            return self.fake_process()

        with self.virtual_node(), mock.patch.object(sys, 'argv', argv), \
                mock.patch.object(launch, 'build_directory', return_value=self.output), \
                mock.patch.object(launch.subprocess, 'Popen', side_effect=spawn) as spawned:
            self.assertEqual(launch.main(), 2)
            spawned.assert_called_once()
        self.assertEqual(self.closed, [fd for fd, _ in self.opened])

    def test_fp_campaign_refuses_the_regular_object_before_any_fixture_code_executes(self):
        self.argv.extend(['--render-node', str(NODE)])
        popen = launch.subprocess.Popen

        def spawn(command, **options):
            self.assert_pinned(command, options)
            return popen(command, **options)

        with self.virtual_node(), mock.patch.object(launch.subprocess, 'Popen', side_effect=spawn):
            status, root, processes = campaign_tests.CampaignTest.execute_fake(self, installer_exit='sys.exit(0)')
        self.assertEqual((status, len(processes)), (126, 1))
        self.assertEqual(len(self.opened), 1)
        self.assertEqual(self.closed, [fd for fd, _ in self.opened])
        record = json.loads((root / 'phases/install/evidence/process.json').read_text())
        self.assertEqual(record['renderer_launch']['renderer'], self.renderer)
        self.assertEqual(record['renderer_launch']['postmount_validation'], 'REFUSED')
        self.assertFalse((root / 'data/Vita3K/Vita3K/ux0/app/BWWF00003').exists())
        self.assertFalse((root / 'phases/guest/evidence/process.json').exists())

    def test_fp_campaign_passes_guarded_commands_to_both_phases_without_claiming_validation(self):
        self.argv.extend(['--render-node', str(NODE)])

        def spawn(command, **options):
            self.assert_pinned(command, options)
            root = options['cwd'].parent
            if '--console' in command:
                app = root / 'data/Vita3K/Vita3K/ux0/app/BWWF00003'
                app.mkdir(parents=True)
                with zipfile.ZipFile(root / 'runtime/fp.vpk') as archive:
                    archive.extractall(app)
            return self.fake_process()

        with self.virtual_node(), mock.patch.object(launch.subprocess, 'Popen', side_effect=spawn):
            status, root, processes = campaign_tests.CampaignTest.execute_fake(self, installer_exit='sys.exit(0)')
        self.assertEqual((status, len(processes)), (2, 2))
        self.assertEqual(len(self.opened), 2)
        self.assertEqual(self.closed, [fd for fd, _ in self.opened])
        for phase in ('install', 'guest'):
            record = json.loads((root / 'phases' / phase / 'evidence/process.json').read_text())
            self.assertTrue(record['renderer_launch']['preflight_validated'])
            self.assertEqual(record['renderer_launch']['postmount_validation'], 'UNOBSERVED')

    def test_ww05_install_and_guest_use_common_supervision_without_the_gpu_guard(self):
        plan = {'run': 'a' * 32, 'source': 'b' * 40, 'vpk_sha256': 'c' * 64,
                'renderer': ww05_launch.RENDERER, 'execution_mode': 'direct-native',
                'private_config': None, 'phases': ww05_launch.direct_phases(self.root, 1)}
        for name in ('install', 'guest'):
            phase = self.root / 'phases' / name
            (phase / 'evidence').mkdir(parents=True)

        def spawn(command, **options):
            self.assertEqual(command[0], str(self.root / 'runtime/Vita3K'))
            self.assertEqual(options['env']['HOME'], str(self.root / 'home'))
            self.assertNotIn('LIBGL_ALWAYS_SOFTWARE', options['env'])
            self.assertNotIn('pass_fds', options)
            self.assertEqual(options['cwd'], str(self.root / 'runtime'))
            return self.fake_process()

        boundaries = {'verify_inputs': None, 'config_identity': None, 'verify_stopped': None,
                      'installer_observer': None, 'emitted_config': b'', 'verify_phase_logs': None,
                      'verify_installed': {}, 'restore_config': None, 'fresh_request': 'a' * 32}
        with mock.patch.object(launch.subprocess, 'Popen', side_effect=spawn) as spawned, \
                ExitStack() as patches:
            for name, value in boundaries.items():
                patches.enter_context(mock.patch.object(ww05_launch, name, return_value=value))
            self.assertEqual(ww05_launch.execute_run(self.root, plan), 2)
            self.assertEqual(spawned.call_count, 2)
        self.assertIs(ww05_launch.run_owned, launch.run_owned)
        self.assertEqual(len(self.opened), 0)
        self.assertEqual(self.closed, [])
        for phase in plan['phases'].values():
            record = json.loads((Path(phase['evidence_root']) / 'evidence/process.json').read_text())
            self.assertNotIn('renderer_launch', record)


if __name__ == '__main__':
    unittest.main()
