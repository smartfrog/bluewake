import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GameInputTest(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()
