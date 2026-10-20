"""Test for scripts/vita/dvd/measure.py: run the probe on a generated
game-free image and check the stored evidence."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import test_dvd_probe

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts/vita/dvd'))

import make_synthetic_image  # noqa: E402

OUT = ROOT / 'build-f13/ww13-seam/measure-tests'


class MeasureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_dvd_probe.build()
        OUT.mkdir(parents=True, exist_ok=True)

    def run_measure(self, iso, out, extra=()):
        return subprocess.run(
            [sys.executable, str(ROOT / 'scripts/vita/dvd/measure.py'), '--iso', str(iso),
             '--probe', str(test_dvd_probe.OUT / 'dvd_probe'), '--out', str(out),
             '--label', 'synthetic', *extra],
            capture_output=True, text=True, timeout=120)

    def test_measure_records_evidence_for_a_generated_image(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            iso = make_synthetic_image.write_image(root / 'image.iso')
            out = root / 'evidence'
            result = self.run_measure(iso, out, ('--path', 'boot.bin', '--path', 'res/asset.bin',
                                                 '--path', 'res/leaf.bin'))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            evidence = json.loads((out / 'evidence.json').read_text())
            self.assertEqual(evidence['parsed']['verdict'], 'REAL PASS')
            self.assertEqual(evidence['parsed']['backend'], 'host-posix')
            self.assertEqual(evidence['parsed']['entries'], 5)
            self.assertEqual(evidence['parsed']['largest'], 'asset.bin')
            self.assertEqual(evidence['parsed']['largest_length'], 0x100)
            self.assertEqual(evidence['parsed']['throughput_bytes'], 0x100)
            self.assertEqual(evidence['parsed']['staging_bytes'], 0)
            self.assertGreater(evidence['parsed']['chunk_bytes'], 0)
            self.assertLessEqual(evidence['parsed']['chunk_bytes'], 1 << 20)
            self.assertEqual(evidence['image']['size'], make_synthetic_image.DISC_SIZE)
            self.assertEqual(len(evidence['pin_dvd_sha256']), 64)
            self.assertIn('guest boot.bin ok 48', evidence['probe']['stdout'])
            self.assertTrue((out / 'report.txt').is_file())
            self.assertTrue((out / 'stderr.txt').is_file())

    def test_measure_reports_a_missing_image(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            result = self.run_measure(Path(directory) / 'missing.iso', Path(directory) / 'evidence')
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
