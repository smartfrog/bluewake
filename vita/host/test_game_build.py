import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GameInputTest(unittest.TestCase):
    def test_rebuild_preserves_previous_artifact_bytes(self):
        spec = importlib.util.spec_from_file_location('ww05_game_build', ROOT / 'scripts/vita/ww05/game_build.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
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
            spec = importlib.util.spec_from_file_location('game_build', ROOT / 'scripts/vita/ww05/game_build.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            with self.assertRaisesRegex(RuntimeError, 'disc verification'):
                module.verify_disc(disc, extractor, output / 'private')
            self.assertFalse((output / 'private/main.dol').exists())


class DstAlphaOptionTest(unittest.TestCase):
    def test_configure_option_is_passed_only_when_requested(self):
        spec = importlib.util.spec_from_file_location('ww05_game_build',
                                                      ROOT / 'scripts/vita/ww05/game_build.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.dst_alpha_configure_option(True),
                         ['-DWW_RENDERER_DST_ALPHA_ADMIT=ON'])
        self.assertEqual(module.dst_alpha_configure_option(False), [])


if __name__ == '__main__':
    unittest.main()
