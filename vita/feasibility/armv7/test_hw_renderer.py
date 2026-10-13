import argparse
from contextlib import contextmanager
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import stat
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))
import fp_launch as launch
import stage_plan
spec = importlib.util.spec_from_file_location('ww24_launch', ROOT / 'scripts/vita/ww05/launch.py')
ww05_launch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ww05_launch)

NODE = Path('/dev/dri/renderD128')
HARDWARE = {'mode': 'hardware-requested', 'render_node': {
    'path': str(NODE), 'major': 226, 'minor': 128, 'device': 9, 'inode': 123,
}}
SOFTWARE = {'mode': 'software', 'render_node': None}


@contextmanager
def node_stat(mode=stat.S_IFCHR | 0o666, device=os.makedev(226, 128), inode=123):
    original = Path.lstat

    def inspect(path, *args, **kwargs):
        if path == NODE:
            return SimpleNamespace(st_mode=mode, st_rdev=device, st_dev=9, st_ino=inode)
        return original(path, *args, **kwargs)

    with mock.patch.object(Path, 'lstat', inspect):
        yield


class RendererCommandTest(unittest.TestCase):
    def test_default_command_keeps_the_exact_software_baseline(self):
        root = Path('/unused')
        expected = ['/usr/bin/bwrap', '--unshare-all', '--die-with-parent', '--new-session',
                    '--ro-bind', '/usr', '/usr', '--ro-bind', '/etc', '/etc',
                    '--symlink', 'usr/lib', '/lib', '--symlink', 'usr/lib64', '/lib64',
                    '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp',
                    '--ro-bind', '/unused', '/run/ww03',
                    '--ro-bind', '/unused/runtime', '/stack/vita3k',
                    '--chdir', '/stack/vita3k', '--clearenv']
        for name in ('home', 'config', 'cache', 'data', 'state', 'work', 'xdg-runtime'):
            expected.extend(['--bind', '/unused/' + name, '/run/ww03/' + name])
        environment = {'PATH': '/usr/bin', 'LANG': 'C.UTF-8', 'HOME': '/run/ww03/home',
                       'XDG_CONFIG_HOME': '/run/ww03/config', 'XDG_CACHE_HOME': '/run/ww03/cache',
                       'XDG_DATA_HOME': '/run/ww03/data', 'XDG_STATE_HOME': '/run/ww03/state',
                       'XDG_RUNTIME_DIR': '/run/ww03/xdg-runtime',
                       'XDG_CONFIG_DIRS': '/run/ww03/config-system',
                       'XDG_DATA_DIRS': '/run/ww03/data-system',
                       'QT_QPA_PLATFORM': 'offscreen', 'SDL_VIDEODRIVER': 'offscreen',
                       'SDL_AUDIODRIVER': 'dummy', 'LIBGL_ALWAYS_SOFTWARE': '1',
                       'FONTCONFIG_FILE': '/etc/fonts/fonts.conf'}
        for name, value in environment.items():
            expected.extend(['--setenv', name, value])
        expected.extend(['/stack/vita3k/Vita3K', '--backend-renderer', 'OpenGL'])
        self.assertEqual(launch.sandbox_command(root, 'Vita3K', ['--backend-renderer', 'OpenGL']), expected)

    def test_opt_in_keeps_mount_scope_and_delays_libraries_until_the_guard(self):
        root = Path('/unused')
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww24-display-') as directory, \
                socket.socket(socket.AF_UNIX) as server:
            endpoint = Path(directory) / 'wayland'
            server.bind(str(endpoint))
            for options in ({}, {'libraries': Path('/public-libraries')},
                            {'libraries': Path('/public-libraries'), 'wayland_socket': endpoint}):
                with self.subTest(options=options):
                    baseline = launch.sandbox_command(root, 'Vita3K', [], **options)
                    with node_stat(), mock.patch.dict(os.environ, {'LIBGL_ALWAYS_SOFTWARE': '1'}):
                        command = launch.sandbox_command(root, 'Vita3K', [], render_node=NODE, **options)
                    index = command.index('--dev-bind')
                    self.assertEqual(command[index - 2:index + 3], ['--dir', '/dev/dri', '--dev-bind', str(NODE), str(NODE)])
                    self.assertEqual(command.count('--dev-bind'), 1)
                    self.assertNotIn('LIBGL_ALWAYS_SOFTWARE', command)
                    guard = command.index('/usr/bin/python3')
                    libraries = '/stack/vita3k-env/lib' if options.get('libraries') else ''
                    self.assertEqual(command[guard:], launch.renderer_guard_command(['/stack/vita3k/Vita3K'], HARDWARE, libraries))
                    reduced = command[:index - 2] + command[index + 3:guard] + ['/stack/vita3k/Vita3K']
                    for variable in ('LIBGL_ALWAYS_SOFTWARE', 'LD_LIBRARY_PATH'):
                        if variable in baseline:
                            environment = baseline.index(variable)
                            baseline[environment - 1:environment + 2] = []
                    self.assertEqual(reduced, baseline)

    def test_renderer_identity_records_requested_device_without_claiming_a_gpu(self):
        with node_stat():
            self.assertEqual(launch.renderer_identity(NODE), HARDWARE)

    def test_renderer_identity_defaults_to_software_without_probing_devices(self):
        with mock.patch.object(Path, 'lstat', side_effect=AssertionError('default must not inspect DRI')):
            self.assertEqual(launch.renderer_identity(), SOFTWARE)

    def test_invalid_device_types_and_numbers_are_rejected(self):
        for mode, device in ((stat.S_IFREG | 0o600, 0), (stat.S_IFDIR | 0o700, 0),
                             (stat.S_IFCHR | 0o666, os.makedev(1, 3)),
                             (stat.S_IFCHR | 0o666, os.makedev(226, 0)),
                             (stat.S_IFCHR | 0o666, os.makedev(226, 129))):
            with self.subTest(mode=mode, device=device), node_stat(mode, device):
                with self.assertRaisesRegex(ValueError, 'render node'):
                    launch.sandbox_command(Path('/unused'), 'Vita3K', [], render_node=NODE)

    def test_missing_node_is_rejected_with_context(self):
        with mock.patch.object(Path, 'lstat', side_effect=FileNotFoundError('synthetic absent node')):
            with self.assertRaisesRegex(ValueError, 'render node.*existing'):
                launch.renderer_identity(NODE)

    def test_non_render_device_paths_cannot_be_selected(self):
        for path in ('/dev/dri', '/dev/dri/card0', '/dev/input/event0', '/dev/snd/controlC0',
                     '/dev/null', 'dev/dri/renderD128', '/dev/dri/../renderD128'):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, 'render node'):
                launch.sandbox_command(Path('/unused'), 'Vita3K', [], render_node=Path(path))

    def test_symlink_node_is_rejected(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww24-link-') as directory:
            alias = Path(directory) / 'renderD128'
            alias.symlink_to('/dev/null')
            with self.assertRaisesRegex(ValueError, 'symlink'):
                launch.sandbox_command(Path('/unused'), 'Vita3K', [], render_node=alias)

    def test_symlink_parent_is_rejected(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww24-parent-') as directory:
            alias = Path(directory) / 'dri'
            alias.symlink_to('/dev')
            with self.assertRaisesRegex(ValueError, 'symlink'):
                launch.sandbox_command(Path('/unused'), 'Vita3K', [], render_node=alias / 'renderD128')

    def test_shared_cli_option_is_default_off_and_requires_an_explicit_node(self):
        parser = argparse.ArgumentParser()
        launch.add_render_node_argument(parser)
        for arguments, expected in (([], None), (['--render-node', str(NODE)], NODE)):
            with self.subTest(arguments=arguments):
                self.assertEqual(parser.parse_args(arguments).render_node, expected)


class RendererPlanTest(unittest.TestCase):
    def test_fp_invalid_node_fails_before_staging_or_reading_inputs(self):
        argv = ['fp_launch', '--runtime', '/unused/runtime', '--runtime-sha256', 'a' * 64,
                '--runtime-source-revision', 'synthetic', '--libraries', '/unused/libraries',
                '--render-node', '/dev/null']
        with mock.patch.object(sys, 'argv', argv), \
                mock.patch.object(launch, 'build_directory', side_effect=AssertionError('staging started')):
            with self.assertRaisesRegex(ValueError, 'render node'):
                launch.main()

    def test_changed_node_identity_is_rejected_before_execution(self):
        with node_stat(inode=124), self.assertRaisesRegex(ValueError, 'render node.*changed'):
            launch.verify_renderer_identity(HARDWARE)

    def test_unchanged_node_identity_can_be_verified(self):
        with node_stat():
            launch.verify_renderer_identity(HARDWARE)

    def test_legacy_software_plans_do_not_require_a_device_identity(self):
        for renderer in (None, SOFTWARE):
            with self.subTest(renderer=renderer), \
                    mock.patch.object(Path, 'lstat', side_effect=AssertionError('software plan probed DRI')):
                launch.verify_renderer_identity(renderer)

    def test_ww05_rejects_obsolete_hardware_plan_before_reading_other_inputs(self):
        with self.assertRaisesRegex(ValueError, 'direct-native'):
            ww05_launch.verify_inputs(Path('/unused'), {'renderer': HARDWARE})

    def test_fp_campaign_rechecks_node_before_reading_other_inputs(self):
        from fp_campaign import verify_inputs
        with node_stat(inode=124), self.assertRaisesRegex(ValueError, 'render node.*changed'):
            verify_inputs(Path('/unused'), {'renderer': HARDWARE})

    def test_fp_receipt_records_requested_mode_even_without_a_report(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww24-receipt-') as directory:
            root = Path(directory) / 'run'
            launch.create_run(root)
            self.assertEqual(launch.collect_result(root, 'a' * 32, 'b' * 40, 'c' * 64, 2, renderer=HARDWARE), 2)
            result = json.loads((root / 'evidence/result.json').read_text())
            self.assertEqual(result['renderer'], HARDWARE)
            self.assertEqual(result['completion'], 'INCOMPLETE')

    def test_ww05_receipt_does_not_claim_hardware_for_an_obsolete_plan(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww24-receipt-') as directory:
            root = Path(directory) / 'run'
            launch.create_run(root)
            plan = {'run': 'a' * 32, 'source': 'b' * 40, 'vpk_sha256': 'c' * 64, 'renderer': HARDWARE}
            with mock.patch.object(ww05_launch, 'verify_inputs', side_effect=ValueError('synthetic preflight failure')), \
                    mock.patch.object(ww05_launch, 'run_owned', side_effect=AssertionError('preflight must not launch')):
                self.assertEqual(ww05_launch.execute_run(root, plan), 2)
            result = json.loads((root / 'evidence/result.json').read_text())
            self.assertEqual(result['renderer'], {'mode': 'native-default', 'observation': 'UNOBSERVED'})
            self.assertFalse(result['rendered'])

    def test_ww05_plan_records_native_default_and_keeps_both_commands(self):
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=60)
        plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
        self.assertEqual(plan['renderer'], {'mode': 'native-default', 'observation': 'UNOBSERVED'})
        self.assertNotIn('renderer_guard', plan)
        for phase in plan['phases'].values():
            self.assertEqual(phase['renderer'], plan['renderer'])
            self.assertNotIn('LIBGL_ALWAYS_SOFTWARE', phase['environment'])
            self.assertNotIn('--dev-bind', phase['command'])

    def test_ww05_plan_refuses_the_obsolete_hardware_guard_request(self):
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=60, render_node=NODE)
        with self.assertRaisesRegex(ValueError, 'direct native'):
            stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)

    def test_ww05_optional_disc_and_frames_do_not_change_native_guest_argv(self):
        for options, source in (({'disc': Path('/synthetic.iso')}, '/synthetic.iso'),
                                ({'mem1_snapshots': 2, 'continue_gx': 2048}, '/unused/evidence/frames')):
            with self.subTest(options=options):
                args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=60, **options)
                plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
                command = plan['phases']['guest']['command']
                application = ['/unused/runtime/Vita3K', '--backend-renderer', 'OpenGL', '--installed-path', 'BWWG00005']
                self.assertEqual(command, application)
                self.assertNotIn(source, command)

    def test_ww05_invalid_node_fails_before_staging_or_reading_game_inputs(self):
        args = SimpleNamespace(timeout=60, render_node=Path('/dev/null'))
        with mock.patch.object(stage_plan, 'checked_sources', side_effect=AssertionError('read game inputs')), \
                mock.patch.object(stage_plan, 'create_run', side_effect=AssertionError('created run')):
            with self.assertRaisesRegex(ValueError, 'render-node'):
                stage_plan.stage(args)

    def test_fp_staged_plan_and_campaign_keep_the_same_requested_node(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww24-fp-') as directory:
            output = Path(directory)
            runtime = output / 'public-runtime'
            runtime.mkdir()
            shutil.copyfile('/usr/bin/true', runtime / 'Vita3K')
            for name in ('data', 'icons', 'shaders-builtin', 'translations'):
                (runtime / name).mkdir()
            vpk = output / 'vita-fp/ww_fp.vpk'
            vpk.parent.mkdir()
            with zipfile.ZipFile(vpk, 'w') as archive:
                archive.writestr('eboot.bin', b'synthetic diagnostic')
                archive.writestr('sce_sys/param.sfo', b'BWWF00003\0')
            evidence = output / 'build.json'
            evidence.write_text(json.dumps({'source': 'a' * 40, 'artifacts': {
                str(vpk): {'sha256': launch.sha256(vpk)}}}))
            firmware = output / 'firmware'
            for name in ('vs0', 'sa0'):
                (firmware / name).mkdir(parents=True)
                (firmware / name / 'public-fixture').write_bytes(name.encode())
            argv = ['fp_launch', '--two-phase', '--runtime', str(runtime / 'Vita3K'),
                    '--runtime-sha256', launch.sha256(runtime / 'Vita3K'), '--runtime-source-revision', 'synthetic',
                    '--libraries', str(runtime), '--firmware', str(firmware),
                    '--build-evidence', str(evidence), '--out', str(output), '--render-node', str(NODE)]
            with node_stat(), mock.patch.object(sys, 'argv', argv), \
                    mock.patch.object(launch, 'build_directory', return_value=output), \
                    mock.patch.object(launch, 'run_owned', side_effect=AssertionError('staging must not launch')):
                self.assertEqual(launch.main(), 0)
            root, = output.glob('vita3k-run-*')
            plan = json.loads((root / 'evidence/plan.json').read_text())
            self.assertEqual(plan['renderer'], HARDWARE)
            self.assertEqual(plan['renderer_guard']['method'], 'mounted-render-node-stat-v1')
            for name, phase in plan['phases'].items():
                saved = json.loads((root / 'phases' / name / 'evidence/plan.json').read_text())
                self.assertEqual(saved['renderer'], HARDWARE)
                self.assertEqual(saved['renderer_guard'], plan['renderer_guard'])
                self.assertEqual(saved, phase)
                self.assertNotIn('LIBGL_ALWAYS_SOFTWARE', phase['command'])
                self.assertEqual(phase['command'].count('--dev-bind'), 1)
            self.assertFalse(list(root.rglob('process.log')))


if __name__ == '__main__':
    unittest.main()
