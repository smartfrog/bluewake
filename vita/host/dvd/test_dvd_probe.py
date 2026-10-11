"""Synthetic probe test for VITA-WW-13: the pinned dvd.c through the seam.

Builds vita/host/dvd against the read-only pinned GXRuntime and runs the
synthetic image mode: no real file, no game data.
"""

import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
STACK = Path('/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack')
RECOMPCORE = Path(os.environ.get('RECOMPCORE_DIR', str(ROOT / 'ref/vita-ww-01/RecompCore')))
OUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f2/ww13-seam/dvd-probe-tests')


class DvdProbeSyntheticTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CC', 'cc')
        subprocess.run(
            [str(STACK / 'bin/cmake'), '-S', str(ROOT / 'vita/host/dvd'), '-B', str(OUT),
             '-G', 'Ninja', f'-DCMAKE_C_COMPILER={compiler}',
             f'-DCMAKE_MAKE_PROGRAM={STACK / "bin/ninja"}',
             f'-DRECOMPCORE_DIR={RECOMPCORE}', '-DWW13_TESTS_ONLY=ON'],
            check=True, capture_output=True)
        subprocess.run([str(STACK / 'bin/cmake'), '--build', str(OUT), '-j', '2'],
                       check=True, capture_output=True)

    @classmethod
    def probe(cls):
        return subprocess.run([str(OUT / 'dvd_probe'), 'synthetic'],
                              capture_output=True, text=True, timeout=30)

    def test_synthetic_image_passes(self):
        result = self.probe()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('SYNTHETIC PASS', result.stdout)

    def test_the_fake_transport_is_used(self):
        result = self.probe()
        self.assertIn('backend fake', result.stdout)

    def test_resolutions_are_the_pinned_answers(self):
        result = self.probe()
        for line in (
            'resolve boot.bin 1 1280 48',
            'resolve res 2 0 0',
            'resolve res/asset.bin 3 1536 256',
            'resolve res/leaf.bin 4 2048 64',
            'resolve BOOT.BIN 1 1280 48',
            'resolve /res/../boot.bin 1 1280 48',
            'resolve res/ 2 0 0',
            'resolve missing -1 0 0',
            'resolve res/missing -1 0 0',
            'resolve asset.bin -1 0 0',
        ):
            with self.subTest(line=line):
                self.assertIn(line, result.stdout)

    def test_guest_copies_match_the_image(self):
        result = self.probe()
        for line in (
            'guest boot.bin ok 48',
            'guest res/asset.bin ok 256',
            'guest-uncached res/leaf.bin ok 64',
            'guest-past-eof prefix=16 zeros=48',
        ):
            with self.subTest(line=line):
                self.assertIn(line, result.stdout)

    def test_no_staging_is_recorded(self):
        result = self.probe()
        self.assertIn('staging=0', result.stdout)


if __name__ == '__main__':
    unittest.main()
