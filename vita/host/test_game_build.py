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
    def test_configure_option_is_always_explicit(self):
        module = load_module()
        self.assertEqual(module.dst_alpha_configure_option(True),
                         ['-DWW_RENDERER_DST_ALPHA_ADMIT=ON'])
        self.assertEqual(module.dst_alpha_configure_option(False),
                         ['-DWW_RENDERER_DST_ALPHA_ADMIT=OFF'])


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
                        '-DWW05_QUIET_LOGGING=OFF', '-DWW05_GLOBAL_CTX=OFF',
                        '-DWW05_ENABLE_SHIP_PROLOG=OFF', '-DWW05_ENABLE_TITLE_CONTRACT=OFF',
                        '-DWW05_ENABLE_IKADA_CONTRACT=OFF', '-DWW05_ENABLE_ACTOR_BATCH_CONTRACTS=OFF',
                        '-DWW_RENDERER_DRAW_CENSUS=OFF']

        def arguments(edge_observation, idle=False, skip_title_logo=False):
            return [str(value) for value in module.vita_configure_arguments(
                'cmake', 'ninja', 'source', 'output', 'toolchain', 'recompcore', 'composite',
                'identity', 'dol', 'vitagl', 'sdk', '1', False, False, False, edge_observation, idle,
                0, False, True, False, skip_title_logo, 1025)]

        prefix = base + ['-DWW_RENDERER_DST_ALPHA_ADMIT=OFF']
        suffix = ['-DWW_IDLE_DEADLINE_ADVANCE=OFF', '-DWW_TURN_CAP=0',
                  '-DWW05_GX_MAX_COMMAND_BYTES=1025', '-DWW_PROGRAM_CACHE=OFF',
                  '-DWW05_RAM_GUARD=ON', '-DWW05_REJECTION_CAPSULE=OFF', '-DWW_SKIP_TITLE_LOGO=OFF',
                  *experimental]
        for state in ('ON', 'OFF'):
            with self.subTest(state=state):
                self.assertEqual(arguments(state == 'ON'),
                                 prefix + [f'-DWW_EDGE_OBSERVATION_ADMIT={state}'] + suffix)

    def test_vita_configure_arguments_carry_the_perf_options_when_requested(self):
        module = load_module()
        arguments = [str(value) for value in module.vita_configure_arguments(
            'cmake', 'ninja', 'source', 'output', 'toolchain', 'recompcore', 'composite',
            'identity', 'dol', 'vitagl', 'sdk', '1', True, False, True, True, True, 8, True, True,
            True, False, 8192)]
        self.assertEqual(arguments, [
            'cmake', '-S', 'source', '-B', 'output', '-G', 'Ninja',
            '-DCMAKE_TOOLCHAIN_FILE=toolchain', '-DCMAKE_MAKE_PROGRAM=ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DRECOMPCORE_DIR=recompcore',
            '-DCOMPOSITE_DIR=composite', '-DWW05_IDENTITY_DIR=identity', '-DWW05_DOL=dol',
            '-DWW05_VITAGL_LIBRARY=vitagl', '-DWW05_RENDERER_SDK=sdk',
            '-DWW05_MODULE_OPT_LEVEL=1', '-DWW05_ENABLE_VI_CLOCK=ON',
            '-DWW05_ENABLE_PAD_INPUT=OFF', '-DWW_RENDERER_DST_ALPHA_ADMIT=ON',
            '-DWW_EDGE_OBSERVATION_ADMIT=ON', '-DWW_IDLE_DEADLINE_ADVANCE=ON',
            '-DWW_TURN_CAP=8', '-DWW05_GX_MAX_COMMAND_BYTES=8192', '-DWW_PROGRAM_CACHE=ON',
            '-DWW05_RAM_GUARD=ON',
            '-DWW05_REJECTION_CAPSULE=ON', '-DWW_SKIP_TITLE_LOGO=OFF',
            '-DWW05_DISPATCH_ABI_SENTINEL=OFF', '-DWW05_OBSERVE_NATIVE_CTX=OFF',
            '-DWW05_QUIET_LOGGING=OFF', '-DWW05_GLOBAL_CTX=OFF',
            '-DWW05_ENABLE_SHIP_PROLOG=OFF', '-DWW05_ENABLE_TITLE_CONTRACT=OFF',
            '-DWW05_ENABLE_IKADA_CONTRACT=OFF', '-DWW05_ENABLE_ACTOR_BATCH_CONTRACTS=OFF',
            '-DWW_RENDERER_DRAW_CENSUS=OFF'])


class TurnCapOptionTest(unittest.TestCase):
    def test_configure_option_is_always_explicit(self):
        self.assertEqual(load_module().turn_cap_configure_option(0), ['-DWW_TURN_CAP=0'])

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


class GxMaxCommandBytesOptionTest(unittest.TestCase):
    def test_configure_option_is_always_explicit(self):
        module = load_module()
        self.assertEqual(module.gx_max_command_bytes_configure_option(1025),
                         ['-DWW05_GX_MAX_COMMAND_BYTES=1025'])

    def test_configure_option_is_passed_when_raised(self):
        self.assertEqual(load_module().gx_max_command_bytes_configure_option(8192),
                         ['-DWW05_GX_MAX_COMMAND_BYTES=8192'])

    def test_below_the_xf_load_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'at least 1025'):
            load_module().gx_max_command_bytes_configure_option(1024)

    def test_parser_rejects_a_non_numeric_value(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            load_module().parse_arguments(['--disc', 'disc.iso', '--gx-max-command-bytes', 'many'])

    def test_parser_rejects_a_value_below_the_xf_load(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            load_module().parse_arguments(['--disc', 'disc.iso', '--gx-max-command-bytes', '1024'])


class ProgramCacheOptionTest(unittest.TestCase):
    def test_configure_option_is_always_explicit(self):
        module = load_module()
        self.assertEqual(module.program_cache_configure_option(True), ['-DWW_PROGRAM_CACHE=ON'])
        self.assertEqual(module.program_cache_configure_option(False), ['-DWW_PROGRAM_CACHE=OFF'])


class RamGuardOptionTest(unittest.TestCase):
    def test_configure_option_is_always_explicit(self):
        module = load_module()
        self.assertEqual(module.ram_guard_configure_option(True), ['-DWW05_RAM_GUARD=ON'])
        self.assertEqual(module.ram_guard_configure_option(False), ['-DWW05_RAM_GUARD=OFF'])


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
        self.assertTrue(args.ram_guard)
        self.assertTrue(args.rejection_capsule)
        self.assertEqual(args.turn_cap, 4096)
        self.assertEqual(args.gx_max_command_bytes, 1025)
        self.assertFalse(args.skip_title_logo)
        self.assertFalse(args.pad_input)

    def test_each_perf_lever_can_be_disabled(self):
        module = load_module()
        args = module.parse_arguments([
            '--disc', 'disc.iso', '--no-vi-clock', '--no-dst-alpha-admit',
            '--no-edge-observation-admit', '--no-idle-deadline-advance',
            '--no-program-cache', '--no-ram-guard', '--no-rejection-capsule', '--turn-cap', '0'])
        self.assertFalse(args.vi_clock)
        self.assertFalse(args.dst_alpha_admit)
        self.assertFalse(args.edge_observation_admit)
        self.assertFalse(args.idle_deadline_advance)
        self.assertFalse(args.program_cache)
        self.assertFalse(args.ram_guard)
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
        self.assertEqual(config['gx_max_command_bytes'], 1025)
        self.assertTrue(config['program_cache'])
        self.assertTrue(config['ram_guard'])
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
        self.assertFalse(config['ship_prolog'])
        self.assertFalse(config['title_contract'])
        self.assertFalse(config['ikada_contract'])
        self.assertFalse(config['actor_batch_contracts'])

    def test_vita_configure_arguments_carry_the_title_logo_skip_when_requested(self):
        module = load_module()
        arguments = [str(value) for value in module.vita_configure_arguments(
            'cmake', 'ninja', 'source', 'output', 'toolchain', 'recompcore', 'composite',
            'identity', 'dol', 'vitagl', 'sdk', '1', False, False, False, False, False, 0, False,
            True, False, True, 1025)]
        self.assertIn('-DWW_SKIP_TITLE_LOGO=ON', arguments)


class CacheCheckTest(unittest.TestCase):
    def test_normal_builder_explicitly_disables_the_census(self):
        self.assertIn('-DWW_RENDERER_DRAW_CENSUS=OFF', load_module().experimental_configure_options())

    def test_stale_cached_census_is_rejected(self):
        module, config = self.config()
        text = self.cache(module, config).replace('WW_RENDERER_DRAW_CENSUS:BOOL=OFF',
                                                 'WW_RENDERER_DRAW_CENSUS:BOOL=ON')
        with self.assertRaisesRegex(ValueError, 'WW_RENDERER_DRAW_CENSUS=ON contradicts'):
            module.checked_cache(text, config)

    def config(self, argv=()):
        module = load_module()
        return module, module.evidence_config(module.parse_arguments(['--disc', 'disc.iso', *argv]))

    def cache(self, module, config):
        lines = [f'{variable}:BOOL={"ON" if config[key] else "OFF"}'
                 for variable, key in module.CACHE_FLAGS]
        lines.append(f"WW_TURN_CAP:STRING={config['turn_cap']}")
        lines.append(f"WW05_GX_MAX_COMMAND_BYTES:STRING={config['gx_max_command_bytes']}")
        return '// generated by cmake\n# a comment\n\n' + '\n'.join(lines) + '\n'

    def test_cache_matches_the_default_profile(self):
        module, config = self.config()
        entries = module.checked_cache(self.cache(module, config), config)
        self.assertEqual(entries['WW_TURN_CAP'], '4096')
        self.assertEqual(entries['WW_PROGRAM_CACHE'], 'ON')
        self.assertEqual(entries['WW05_RAM_GUARD'], 'ON')

    def test_cache_matches_an_explicit_off_profile(self):
        module, config = self.config(['--no-program-cache', '--turn-cap', '0', '--no-dst-alpha-admit'])
        entries = module.checked_cache(self.cache(module, config), config)
        self.assertEqual(entries['WW_PROGRAM_CACHE'], 'OFF')
        self.assertEqual(entries['WW_RENDERER_DST_ALPHA_ADMIT'], 'OFF')
        self.assertEqual(entries['WW_TURN_CAP'], '0')

    def test_stale_cached_program_cache_is_rejected(self):
        module, config = self.config(['--no-program-cache'])
        text = self.cache(module, config).replace('WW_PROGRAM_CACHE:BOOL=OFF',
                                                 'WW_PROGRAM_CACHE:BOOL=ON')
        with self.assertRaisesRegex(ValueError, 'WW_PROGRAM_CACHE=ON contradicts'):
            module.checked_cache(text, config)

    def test_stale_cached_turn_cap_is_rejected(self):
        module, config = self.config(['--turn-cap', '0'])
        text = self.cache(module, config).replace('WW_TURN_CAP:STRING=0', 'WW_TURN_CAP:STRING=4096')
        with self.assertRaisesRegex(ValueError, 'WW_TURN_CAP=4096 contradicts'):
            module.checked_cache(text, config)

    def test_stale_cached_gx_max_command_bytes_is_rejected(self):
        module, config = self.config()
        text = self.cache(module, config).replace('WW05_GX_MAX_COMMAND_BYTES:STRING=1025',
                                                  'WW05_GX_MAX_COMMAND_BYTES:STRING=8192')
        with self.assertRaisesRegex(ValueError, 'WW05_GX_MAX_COMMAND_BYTES=8192 contradicts'):
            module.checked_cache(text, config)

    def test_stale_cached_ram_guard_is_rejected(self):
        module, config = self.config()
        text = self.cache(module, config).replace('WW05_RAM_GUARD:BOOL=ON',
                                                 'WW05_RAM_GUARD:BOOL=OFF')
        with self.assertRaisesRegex(ValueError, 'WW05_RAM_GUARD=OFF contradicts'):
            module.checked_cache(text, config)

    def test_missing_option_is_rejected(self):
        module, config = self.config()
        text = self.cache(module, config).replace('WW_SKIP_TITLE_LOGO:BOOL=OFF\n', '')
        with self.assertRaisesRegex(ValueError, 'missing WW_SKIP_TITLE_LOGO'):
            module.checked_cache(text, config)

    def test_non_boolean_option_is_rejected(self):
        module, config = self.config()
        text = self.cache(module, config).replace('WW_PROGRAM_CACHE:BOOL=ON',
                                                 'WW_PROGRAM_CACHE:BOOL=maybe')
        with self.assertRaisesRegex(ValueError, 'non-boolean WW_PROGRAM_CACHE'):
            module.checked_cache(text, config)


if __name__ == '__main__':
    unittest.main()
