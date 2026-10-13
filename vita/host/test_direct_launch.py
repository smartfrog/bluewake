import os
from pathlib import Path
import socket
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))
sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))


class DirectLaunchTest(unittest.TestCase):
    def plan(self, **options):
        from stage_plan import make_plan
        args = SimpleNamespace(timeout=60, libraries=Path('/approved/dependencies'), wayland_socket=None, **options)
        return make_plan(Path('/private/run'), args, 'a' * 40, 'b' * 64, 'c' * 32)

    def test_native_argv_contains_only_the_staged_runtime_and_game_arguments(self):
        plan = self.plan()
        self.assertEqual(plan['phases']['guest']['command'],
                         ['/private/run/runtime/Vita3K', '--backend-renderer', 'OpenGL', '--installed-path', 'BWWG00005'])

    def test_private_environment_ignores_inherited_software_and_player_profile(self):
        with patch.dict(os.environ, {'LIBGL_ALWAYS_SOFTWARE': '1', 'HOME': '/player', 'LD_PRELOAD': '/unapproved'}):
            env = self.plan()['phases']['guest']['environment']
        self.assertNotIn('LIBGL_ALWAYS_SOFTWARE', env)
        self.assertNotIn('LD_PRELOAD', env)
        self.assertEqual(env['HOME'], '/private/run/home')
        for name, child in (('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                            ('XDG_CACHE_HOME', 'cache'), ('XDG_STATE_HOME', 'state')):
            self.assertEqual(env[name], '/private/run/' + child)

    def test_native_dependencies_and_working_directory_use_host_paths(self):
        phase = self.plan()['phases']['guest']
        self.assertEqual(phase['environment']['LD_LIBRARY_PATH'], '/approved/dependencies/lib')
        self.assertEqual(phase['environment']['QT_PLUGIN_PATH'], '/approved/dependencies/lib/qt6/plugins')
        self.assertEqual(phase['cwd'], '/private/run/runtime')

    def test_direct_mode_does_not_claim_gpu_or_sandbox_verification(self):
        plan = self.plan()
        self.assertEqual(plan['execution_mode'], 'direct-native')
        self.assertEqual(plan['renderer'], {'mode': 'native-default', 'observation': 'UNOBSERVED'})
        self.assertNotIn('renderer_guard', plan)
        self.assertNotIn('sandbox_cwd', plan)
        self.assertEqual(plan['continue_gx'], 0)
        self.assertEqual(plan['mem1_snapshots'], 0)
        self.assertFalse(plan['defer_audio'])

    def test_direct_mode_refuses_obsolete_render_node_selection(self):
        with self.assertRaisesRegex(ValueError, 'direct'):
            self.plan(render_node=Path('/dev/dri/renderD128'))

    def test_authorized_wayland_socket_is_used_without_mount_aliases(self):
        from stage_plan import make_plan
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory, socket.socket(socket.AF_UNIX) as display:
            path = Path(directory) / 'display'
            display.bind(str(path))
            args = SimpleNamespace(timeout=60, libraries=None, wayland_socket=path)
            env = make_plan(Path(directory) / 'run', args, 'a' * 40, 'b' * 64, 'c' * 32)['phases']['guest']['environment']
            self.assertEqual(env['WAYLAND_DISPLAY'], str(path))
            self.assertEqual(env['QT_QPA_PLATFORM'], 'wayland')
            self.assertNotIn('SDL_VIDEODRIVER', env)

    def test_native_config_changes_only_the_preference_path_in_the_approved_schema(self):
        from fp_profile import profile_config
        baseline = profile_config()
        path = Path('/private/profile with spaces/data/Vita3K/Vita3K')
        native = profile_config(pref_path=path)
        self.assertEqual(native.replace(b'pref-path: /private/profile with spaces/data/Vita3K/Vita3K\n',
                                        b'pref-path: /run/ww03/data/Vita3K/Vita3K\n'), baseline)

    def test_native_config_refuses_player_preference_path_mutation(self):
        from fp_launch import create_run
        from fp_profile import CONFIG_PATH, FIRMWARE_DIRECTORY, config_identity, profile_config
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            root = Path(directory) / 'run'
            create_run(root)
            (root / CONFIG_PATH).parent.mkdir()
            (root / CONFIG_PATH).write_bytes(profile_config(pref_path=Path('/player')))
            with self.assertRaisesRegex(ValueError, 'private native profile'):
                config_identity(root, pref_path=root / FIRMWARE_DIRECTORY)

    def test_changed_native_argv_or_environment_is_refused_before_runtime_inputs(self):
        import launch
        for field, value in (('command', ['/unapproved/Vita3K']),
                             ('environment', {'LIBGL_ALWAYS_SOFTWARE': '1'}), ('cwd', '/player')):
            with self.subTest(field=field):
                plan = self.plan()
                plan.update(runtime_libraries={'root': '/approved/dependencies'}, wayland_socket=None,
                            build_evidence_path='/unused/receipt', build_evidence_sha256='d' * 64,
                            build_evidence={})
                plan['phases']['guest'][field] = value
                with patch('launch.subprocess.check_output', side_effect=['a' * 40, '']), \
                     patch('launch.checked_sources'), patch('launch.sha256', return_value='d' * 64), \
                     patch('launch.checked_build_receipt', return_value={}):
                    with self.assertRaisesRegex(ValueError, 'commands, environment or phase identities'):
                        launch.verify_inputs(Path('/private/run'), plan)

    def test_candidate_plan_is_verified_through_the_candidate_admission(self):
        import launch
        plan = self.plan()
        plan.update(candidate_build=True, candidate={'descriptor': '/private/candidate.json',
                                                     'descriptor_sha256': 'd' * 64},
                    build_evidence_path='/private/receipt.json', build_evidence_sha256='d' * 64,
                    build_evidence={'source': 'a' * 40}, runtime_libraries={'root': '/approved/dependencies'},
                    wayland_socket=None)
        with patch('launch.subprocess.check_output', side_effect=['a' * 40, '']), \
                patch('launch.checked_sources'), patch('launch.sha256', return_value='d' * 64), \
                patch('launch.load_candidate_descriptor', return_value={'synthetic': True}) as load, \
                patch('launch.checked_candidate_receipt', return_value=plan['build_evidence']), \
                patch('launch.verify_candidate_package', side_effect=ValueError('synthetic candidate gate')), \
                patch('launch.checked_build_receipt', side_effect=AssertionError('legacy admission used')):
            with self.assertRaisesRegex(ValueError, 'synthetic candidate gate'):
                launch.verify_inputs(Path('/private/run'), plan)
        load.assert_called_once_with(Path('/private/candidate.json'))

    def test_launcher_check_mode_verifies_without_staging_or_executing(self):
        import launch
        argv = ['launch', '--check', '--candidate-build', '/private/candidate.json',
                '--wayland-socket', '/run/user/1000/wayland-0']
        with patch.object(sys, 'argv', argv), patch('launch.check', return_value=0) as check, \
                patch('launch.stage', side_effect=AssertionError('check mode must not stage')), \
                patch('launch.execute_run', side_effect=AssertionError('check mode must not execute')):
            self.assertEqual(launch.main(), 0)
        check.assert_called_once()

    def test_firmware_staging_records_the_native_preference_path_and_exact_config(self):
        from fp_launch import create_run
        from fp_profile import CONFIG_PATH, FIRMWARE_DIRECTORY, config_identity, profile_config, stage_firmware
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            root = Path(directory) / 'run'
            create_run(root)
            source = Path(directory) / 'firmware'
            for name in ('vs0', 'sa0'):
                (source / name).mkdir(parents=True)
                (source / name / 'synthetic-system-file').write_bytes(b'synthetic')
            pref_path = root / FIRMWARE_DIRECTORY
            stage_firmware(root, source, pref_path=pref_path)
            self.assertEqual((root / CONFIG_PATH).read_bytes(), profile_config(pref_path=pref_path))
            self.assertEqual(config_identity(root, pref_path=pref_path)['pref_path'], str(pref_path))

    def test_preference_path_cannot_inject_yaml_or_use_a_relative_player_directory(self):
        from fp_profile import profile_config
        for path in ('relative/path', '/private/path\npref-path: /player', '/private/#comment'):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, 'plain YAML path'):
                profile_config(pref_path=path)


if __name__ == '__main__':
    unittest.main()
