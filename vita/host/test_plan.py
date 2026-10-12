import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))
sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))


class PlanTest(unittest.TestCase):
    def test_shader_compiler_is_copied_into_the_private_profile_with_its_identity(self):
        import stage_plan
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-shader-') as directory:
            root = Path(directory)
            shader = root / 'shader.suprx'
            shader.write_bytes(b'synthetic private system module')
            record = stage_plan.stage_shader_compiler(root / 'run', shader)
            target = root / 'run' / record['install_path']
            self.assertEqual(target.read_bytes(), shader.read_bytes())
            self.assertEqual(record['staged'], stage_plan.file_identity(target))

    def test_optional_disc_staging_keeps_a_verified_identity_and_creates_only_an_empty_bind_target(self):
        import stage_plan
        with tempfile.TemporaryDirectory(dir='/home/fred/projects/zeldaWWHDRecomp/build-f5', prefix='ww05-disc-') as directory:
            root = Path(directory)
            image = root / 'private.iso'
            image.write_bytes(b'verified private fixture')
            record = stage_plan.stage_disc(root / 'run', image, stage_plan.sha256(image))
            self.assertEqual(record['identity']['sha256'], stage_plan.sha256(image))
            self.assertEqual((root / 'run' / record['install_path']).read_bytes(), b'')

    def test_optional_disc_rejects_a_different_image_before_creating_an_install_target(self):
        import stage_plan
        with tempfile.TemporaryDirectory(dir='/home/fred/projects/zeldaWWHDRecomp/build-f5', prefix='ww05-disc-') as directory:
            root = Path(directory)
            image = root / 'private.iso'
            image.write_bytes(b'different private fixture')
            with self.assertRaisesRegex(ValueError, 'verified WW05 input'):
                stage_plan.stage_disc(root / 'run', image, 'a' * 64)
            self.assertFalse((root / 'run').exists())
    def test_continuation_is_opt_in_and_has_a_command_log_limit_not_an_execution_limit(self):
        import stage_plan
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=600, continue_gx=64, disc=None)
        plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
        self.assertEqual(plan['continue_gx'], 64)
        self.assertIn('no guest execution cap', plan['continuation_policy'])

    def test_continuation_log_limit_is_bounded(self):
        import stage_plan
        for limit in (-1, 257):
            with self.subTest(limit=limit), self.assertRaises(ValueError):
                stage_plan.make_plan(Path('/unused'), SimpleNamespace(timeout=600, continue_gx=limit),
                                     'a' * 40, 'b' * 64, 'c' * 32)

    def test_optional_disc_is_bound_read_only_at_the_existing_ww13_install_path(self):
        import stage_plan
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=600, continue_gx=64,
                               disc=Path('/private/input.iso'))
        plan = stage_plan.make_plan(Path('/private/run'), args, 'a' * 40, 'b' * 64, 'c' * 32)
        for phase in plan['phases'].values():
            command = phase['command']
            index = command.index('/private/input.iso')
            self.assertEqual(command[index - 1:index + 2], ['--ro-bind', '/private/input.iso',
                             '/run/ww03/data/Vita3K/Vita3K/ux0/data/bluewake/GZLE01.iso'])
    def test_deeper_startup_allows_a_bounded_three_minute_phase(self):
        import stage_plan
        with tempfile.TemporaryDirectory(prefix='ww05-deadline-', dir='/home/fred/projects/zeldaWWHDRecomp/build-f5') as directory:
            args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=180)
            plan = stage_plan.make_plan(Path(directory), args, 'a' * 40, 'b' * 64, 'c' * 32)
            self.assertEqual(plan['phases']['install']['timeout_seconds'], 180)
            self.assertEqual(plan['phases']['guest']['timeout_seconds'], 180)

    def test_coordinator_requested_ten_minute_phases_keep_owned_cleanup(self):
        import stage_plan
        with tempfile.TemporaryDirectory(prefix='ww05-deadline-', dir='/home/fred/projects/zeldaWWHDRecomp/build-f5') as directory:
            args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=600)
            plan = stage_plan.make_plan(Path(directory), args, 'a' * 40, 'b' * 64, 'c' * 32)
            self.assertEqual(plan['phases']['install']['timeout_seconds'], 600)
            self.assertEqual(plan['phases']['guest']['timeout_seconds'], 600)
            self.assertIn('KILL/reap', plan['cleanup'])

    def test_phase_deadlines_remain_finite_positive_and_bounded(self):
        import stage_plan
        for timeout in (0, -1, 601, float('inf'), float('nan')):
            with self.subTest(timeout=timeout):
                args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=timeout)
                with self.assertRaisesRegex(ValueError, 'deadline'):
                    stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)

    def test_changed_elf_is_rejected_before_staging(self):
        import stage_plan
        with tempfile.TemporaryDirectory(prefix='ww05-package-', dir='/home/fred/projects/zeldaWWHDRecomp/build-f5') as directory:
            output = Path(directory)
            (output / 'private').mkdir()
            dol = output / 'private/main.dol'
            dol.write_bytes(b'synthetic DOL')
            (output / 'inputs.json').write_text(json.dumps({'dol_sha256': stage_plan.sha256(dol)}))
            for name in ('ww05_host', 'ww05_host.velf', 'eboot.bin', 'ww05.vpk_param.sfo'):
                (output / name).write_bytes(b'synthetic ' + name.encode())
            with zipfile.ZipFile(output / 'ww05.vpk', 'w') as archive:
                archive.write(dol, 'main.dol')
                archive.write(output / 'eboot.bin', 'eboot.bin')
                archive.write(output / 'ww05.vpk_param.sfo', 'sce_sys/param.sfo')
            artifacts = {str(path): {'bytes': path.stat().st_size, 'sha256': stage_plan.sha256(path)}
                         for path in output.iterdir() if path.name.startswith(('ww05', 'eboot'))}
            evidence = {'title': 'BWWG00005', 'method': 'startup-gx-v1', 'artifacts': artifacts}
            (output / 'ww05_host').write_bytes(b'changed ELF')
            with patch.object(stage_plan, 'OUTPUT', output):
                with self.assertRaisesRegex(ValueError, 'artifact changed: ww05_host'):
                    stage_plan.verify_package(evidence)

    def test_preparation_has_two_exact_commands_and_starts_no_process(self):
        spec = importlib.util.spec_from_file_location('ww05_stage', ROOT / 'scripts/vita/ww05/stage_plan.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix='ww05-plan-', dir='/home/fred/projects/zeldaWWHDRecomp/build-f5') as directory:
            root = Path(directory)
            args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=60)
            with patch('subprocess.Popen', side_effect=AssertionError('preparation must not launch')):
                plan = module.make_plan(root, args, 'a' * 40, 'b' * 64, 'c' * 32)
            self.assertNotIn('process_budget', plan)
            self.assertNotIn('authorization', plan)
            self.assertEqual(plan['retry'], 'fresh private profile for every attempt')
            self.assertFalse(plan['real_execution'])
            self.assertEqual(plan['phases']['install']['command'][-5:],
                             ['/stack/vita3k/Vita3K', '--console', '--backend-renderer', 'OpenGL', 'ww05.vpk'])
            self.assertEqual(plan['phases']['guest']['command'][-5:],
                             ['/stack/vita3k/Vita3K', '--backend-renderer', 'OpenGL', '--installed-path', 'BWWG00005'])
            self.assertIn('--unshare-all', plan['phases']['guest']['command'])
            self.assertEqual(plan['phases']['guest']['timeout_seconds'], 60)


if __name__ == '__main__':
    unittest.main()
