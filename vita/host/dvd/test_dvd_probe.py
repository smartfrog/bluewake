"""Synthetic and real-image probe tests for VITA-WW-13.

Builds vita/host/dvd against the read-only pinned GXRuntime and runs the
synthetic image mode (game-free, always) and the real-image mode (private
local image only, skipped when absent).
"""

import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
STACK = Path('/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack')
RECOMPCORE = Path(os.environ.get('RECOMPCORE_DIR', str(ROOT / 'ref/vita-ww-01/RecompCore')))
ISO = Path(os.environ.get(
    'WW13_ISO', '/home/fred/projects/zeldaWWHDRecomp/Legend of Zelda, The - The Wind Waker (USA).iso'))
OUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f2/ww13-seam/dvd-probe-tests')


def build():
    compiler = os.environ.get('CC', 'cc')
    subprocess.run(
        [str(STACK / 'bin/cmake'), '-S', str(ROOT / 'vita/host/dvd'), '-B', str(OUT),
         '-G', 'Ninja', f'-DCMAKE_C_COMPILER={compiler}',
         f'-DCMAKE_MAKE_PROGRAM={STACK / "bin/ninja"}',
         f'-DRECOMPCORE_DIR={RECOMPCORE}', '-DWW13_TESTS_ONLY=ON'],
        check=True, capture_output=True)
    subprocess.run([str(STACK / 'bin/cmake'), '--build', str(OUT), '-j', '2'],
                   check=True, capture_output=True)


class DvdProbeSyntheticTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build()

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


class DvdProbeMissingImageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build()

    def test_missing_image_fails_loudly(self):
        report = OUT / 'missing-report.txt'
        result = subprocess.run([str(OUT / 'dvd_probe'), 'real', '/nonexistent/ww13.iso',
                                 str(report)],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("no readable GameCube disc image at '/nonexistent/ww13.iso'", result.stderr)
        self.assertIn('REAL FAIL', result.stderr)


@unittest.skipUnless(ISO.exists(), 'private disc image not available')
class DvdProbeRealTest(unittest.TestCase):
    report = None
    report_path = None

    @classmethod
    def setUpClass(cls):
        build()
        cls.report_path = OUT / 'real-report.txt'
        cls.report = subprocess.run([str(OUT / 'dvd_probe'), 'real', str(ISO),
                                     str(cls.report_path)],
                                    capture_output=True, text=True, timeout=600)

    def test_real_image_passes(self):
        self.assertEqual(self.report.returncode, 0, self.report.stdout + self.report.stderr)
        self.assertIn('REAL PASS', self.report.stdout)

    def test_the_report_file_carries_the_verdict(self):
        content = self.report_path.read_text()
        self.assertIn('REAL PASS', content)
        self.assertIn('throughput bytes=', content)
        self.assertIn('staging=0', content)

    def test_the_posix_transport_is_used(self):
        self.assertIn('backend host-posix', self.report.stdout)

    def test_oracle_and_pin_agree(self):
        self.assertIn('pin-matches-oracle framework.str ok', self.report.stdout)
        self.assertIn('pin-matches-oracle RELS.arc ok', self.report.stdout)
        self.assertIn('pin-matches-oracle res/Menu/Menu1.dat ok', self.report.stdout)
        self.assertIn('pin-matches-oracle res/Msg/bmgres.arc ok', self.report.stdout)

    def test_guest_samples_match_direct_reads(self):
        self.assertIn('guest framework.str ok', self.report.stdout)
        self.assertIn('guest RELS.arc ok', self.report.stdout)

    def test_throughput_is_measured(self):
        self.assertIn('throughput bytes=', self.report.stdout)
        self.assertIn('mib_per_s=', self.report.stdout)
        self.assertIn('staging=0', self.report.stdout)


if __name__ == '__main__':
    unittest.main()
