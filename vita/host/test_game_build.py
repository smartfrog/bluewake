import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def load_module():
    spec = importlib.util.spec_from_file_location('ww05_game_build', ROOT / 'scripts/vita/ww05/game_build.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GameInputTest(unittest.TestCase):
    def test_rebuild_preserves_previous_artifact_bytes(self):
        module = load_module()
        with tempfile.TemporaryDirectory(dir='/home/fred/projects/zeldaWWHDRecomp/build-f5', prefix='ww05-artifacts-') as directory:
            output = Path(directory)
            vita = output / 'vita'
            vita.mkdir()
            for name in ('ww05_host', 'ww05_host.velf', 'eboot.bin', 'ww05.vpk_param.sfo', 'ww05.vpk'):
                (vita / name).write_bytes(b'synthetic old artifact')
            first = module.preserve_artifacts(vita, output)
            for path in vita.iterdir():
                path.write_bytes(b'synthetic new artifact')
            second = module.preserve_artifacts(vita, output)
            self.assertTrue(set(first).isdisjoint(second))
            for name, entry in first.items():
                self.assertEqual(Path(name).read_bytes(), b'synthetic old artifact')
                self.assertEqual(module.sha256(name), entry['sha256'])

    def test_malformed_disc_stops_before_generation(self):
        with tempfile.TemporaryDirectory(dir=ROOT, prefix='build-ww05-input-') as directory:
            output = Path(directory)
            disc = output / 'malformed.iso'
            disc.write_bytes(b'not a disc')
            extractor = output / 'disc_extract'
            subprocess.run([os.environ['CC'], '-O2', '-Iapple/ios/src',
                            'scripts/ios/disc_extract.c', 'apple/ios/src/disc_import.c',
                            '-o', str(extractor)], cwd=ROOT, check=True)
            module = load_module()
            with self.assertRaisesRegex(RuntimeError, 'disc verification'):
                module.verify_disc(disc, extractor, output / 'private')
            self.assertFalse((output / 'private/main.dol').exists())


class DstAlphaOptionTest(unittest.TestCase):
    def test_configure_option_is_passed_only_when_requested(self):
        module = load_module()
        self.assertEqual(module.dst_alpha_configure_option(True),
                         ['-DWW_RENDERER_DST_ALPHA_ADMIT=ON'])
        self.assertEqual(module.dst_alpha_configure_option(False), [])


class IdleDeadlineAdvanceOptionTest(unittest.TestCase):
    def test_configure_option_is_always_explicit(self):
        module = load_module()
        self.assertEqual(module.idle_deadline_advance_configure_option(True),
                         ['-DWW_IDLE_DEADLINE_ADVANCE=ON'])
        self.assertEqual(module.idle_deadline_advance_configure_option(False),
                         ['-DWW_IDLE_DEADLINE_ADVANCE=OFF'])


class EdgeObservationAdmitOptionTest(unittest.TestCase):
    def test_configure_option_is_always_explicit(self):
        module = load_module()
        self.assertEqual(module.edge_observation_admit_configure_option(True),
                         ['-DWW_EDGE_OBSERVATION_ADMIT=ON'])
        self.assertEqual(module.edge_observation_admit_configure_option(False),
                         ['-DWW_EDGE_OBSERVATION_ADMIT=OFF'])

    def test_vita_configure_arguments_carry_the_admission_explicitly(self):
        module = load_module()
        base = ['cmake', '-S', 'source', '-B', 'output', '-G', 'Ninja',
                '-DCMAKE_TOOLCHAIN_FILE=toolchain', '-DCMAKE_MAKE_PROGRAM=ninja',
                '-DCMAKE_BUILD_TYPE=Release', '-DRECOMPCORE_DIR=recompcore',
                '-DCOMPOSITE_DIR=composite', '-DWW05_IDENTITY_DIR=identity', '-DWW05_DOL=dol',
                '-DWW05_VITAGL_LIBRARY=vitagl', '-DWW05_RENDERER_SDK=sdk',
                '-DWW05_MODULE_OPT_LEVEL=1', '-DWW05_ENABLE_VI_CLOCK=OFF',
                '-DWW05_ENABLE_PAD_INPUT=OFF']

        experimental = ['-DWW05_DISPATCH_ABI_SENTINEL=OFF', '-DWW05_OBSERVE_NATIVE_CTX=OFF',
                        '-DWW05_QUIET_LOGGING=OFF', '-DWW05_GLOBAL_CTX=OFF']

        def arguments(enabled, idle=False, skip_title_logo=False):
            return [str(value) for value in module.vita_configure_arguments(
                'cmake', 'ninja', 'source', 'output', 'toolchain', 'recompcore', 'composite',
                'identity', 'dol', 'vitagl', 'sdk', '1', False, False, False, enabled, idle,
                0, False, False, skip_title_logo)]

        expected = {'ON': ['-DWW_EDGE_OBSERVATION_ADMIT=ON', '-DWW_IDLE_DEADLINE_ADVANCE=OFF',
                           '-DWW05_REJECTION_CAPSULE=OFF', '-DWW_SKIP_TITLE_LOGO=OFF', *experimental],
                    'OFF': ['-DWW_EDGE_OBSERVATION_ADMIT=OFF', '-DWW_IDLE_DEADLINE_ADVANCE=OFF',
                            '-DWW05_REJECTION_CAPSULE=OFF', '-DWW_SKIP_TITLE_LOGO=OFF', *experimental]}
        for state, option in expected.items():
            with self.subTest(state=state):
                self.assertEqual(arguments(state == 'ON'), base + option)

    def test_vita_configure_arguments_carry_the_perf_options_when_requested(self):
        module = load_module()
        arguments = [str(value) for value in module.vita_configure_arguments(
            'cmake', 'ninja', 'source', 'output', 'toolchain', 'recompcore', 'composite',
            'identity', 'dol', 'vitagl', 'sdk', '1', True, False, True, True, True, 8, True, True, False)]
        self.assertEqual(arguments, [
            'cmake', '-S', 'source', '-B', 'output', '-G', 'Ninja',
            '-DCMAKE_TOOLCHAIN_FILE=toolchain', '-DCMAKE_MAKE_PROGRAM=ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DRECOMPCORE_DIR=recompcore',
            '-DCOMPOSITE_DIR=composite', '-DWW05_IDENTITY_DIR=identity', '-DWW05_DOL=dol',
            '-DWW05_VITAGL_LIBRARY=vitagl', '-DWW05_RENDERER_SDK=sdk',
            '-DWW05_MODULE_OPT_LEVEL=1', '-DWW05_ENABLE_VI_CLOCK=ON',
            '-DWW05_ENABLE_PAD_INPUT=OFF', '-DWW_RENDERER_DST_ALPHA_ADMIT=ON',
            '-DWW_EDGE_OBSERVATION_ADMIT=ON', '-DWW_IDLE_DEADLINE_ADVANCE=ON',
            '-DWW_TURN_CAP=8', '-DWW_PROGRAM_CACHE=ON', '-DWW05_REJECTION_CAPSULE=ON',
            '-DWW_SKIP_TITLE_LOGO=OFF', '-DWW05_DISPATCH_ABI_SENTINEL=OFF',
            '-DWW05_OBSERVE_NATIVE_CTX=OFF', '-DWW05_QUIET_LOGGING=OFF', '-DWW05_GLOBAL_CTX=OFF'])


class TurnCapOptionTest(unittest.TestCase):
    def test_configure_option_is_absent_at_the_default(self):
        self.assertEqual(load_module().turn_cap_configure_option(0), [])

    def test_configure_option_is_passed_when_requested(self):
        self.assertEqual(load_module().turn_cap_configure_option(6), ['-DWW_TURN_CAP=6'])

    def test_negative_cap_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'turn cap'):
            load_module().turn_cap_configure_option(-1)

    def test_parser_rejects_a_negative_cap(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            load_module().parse_arguments(['--disc', 'disc.iso', '--turn-cap', '-1'])

    def test_parser_rejects_a_non_numeric_cap(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            load_module().parse_arguments(['--disc', 'disc.iso', '--turn-cap', 'many'])


class ProgramCacheOptionTest(unittest.TestCase):
    def test_configure_option_is_passed_only_when_requested(self):
        module = load_module()
        self.assertEqual(module.program_cache_configure_option(True), ['-DWW_PROGRAM_CACHE=ON'])
        self.assertEqual(module.program_cache_configure_option(False), [])


class RejectionCapsuleOptionTest(unittest.TestCase):
    def test_configure_option_is_always_explicit(self):
        module = load_module()
        self.assertEqual(module.rejection_capsule_configure_option(True),
                         ['-DWW05_REJECTION_CAPSULE=ON'])
        self.assertEqual(module.rejection_capsule_configure_option(False),
                         ['-DWW05_REJECTION_CAPSULE=OFF'])


class ParserDefaultsTest(unittest.TestCase):
    def test_the_perf_profile_is_the_default(self):
        module = load_module()
        args = module.parse_arguments(['--disc', 'disc.iso'])
        self.assertTrue(args.vi_clock)
        self.assertTrue(args.dst_alpha_admit)
        self.assertTrue(args.edge_observation_admit)
        self.assertTrue(args.idle_deadline_advance)
        self.assertTrue(args.program_cache)
        self.assertTrue(args.rejection_capsule)
        self.assertEqual(args.turn_cap, 4096)
        self.assertFalse(args.skip_title_logo)
        self.assertFalse(args.pad_input)

    def test_each_perf_lever_can_be_disabled(self):
        module = load_module()
        args = module.parse_arguments([
            '--disc', 'disc.iso', '--no-vi-clock', '--no-dst-alpha-admit',
            '--no-edge-observation-admit', '--no-idle-deadline-advance',
            '--no-program-cache', '--no-rejection-capsule', '--turn-cap', '0'])
        self.assertFalse(args.vi_clock)
        self.assertFalse(args.dst_alpha_admit)
        self.assertFalse(args.edge_observation_admit)
        self.assertFalse(args.idle_deadline_advance)
        self.assertFalse(args.program_cache)
        self.assertFalse(args.rejection_capsule)
        self.assertEqual(args.turn_cap, 0)


class BuildEvidenceConfigTest(unittest.TestCase):
    def test_config_records_the_perf_options(self):
        module = load_module()
        config = module.evidence_config(
            module.parse_arguments(['--disc', 'disc.iso', '--turn-cap', '8', '--program-cache']))
        self.assertEqual(config['turn_cap'], 8)
        self.assertTrue(config['program_cache'])

    def test_config_records_the_defaults(self):
        module = load_module()
        config = module.evidence_config(module.parse_arguments(['--disc', 'disc.iso']))
        self.assertEqual(config['turn_cap'], 4096)
        self.assertTrue(config['program_cache'])
        self.assertTrue(config['vi_clock'])
        self.assertTrue(config['dst_alpha_admit'])
        self.assertTrue(config['edge_observation_admit'])
        self.assertTrue(config['idle_deadline_advance'])
        self.assertTrue(config['rejection_capsule'])
        self.assertFalse(config['skip_title_logo'])
        self.assertFalse(config['dispatch_abi_sentinel'])
        self.assertFalse(config['observe_native_ctx'])
        self.assertFalse(config['quiet_logging'])
        self.assertFalse(config['global_ctx'])

    def test_vita_configure_arguments_carry_the_title_logo_skip_when_requested(self):
        module = load_module()
        arguments = [str(value) for value in module.vita_configure_arguments(
            'cmake', 'ninja', 'source', 'output', 'toolchain', 'recompcore', 'composite',
            'identity', 'dol', 'vitagl', 'sdk', '1', False, False, False, False, False, 0, False,
            False, True)]
        self.assertIn('-DWW_SKIP_TITLE_LOGO=ON', arguments)


if __name__ == '__main__':
    unittest.main()
