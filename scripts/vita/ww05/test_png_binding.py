import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts/vita'))
from gx.image_metadata import png_metadata


class PngBindingTest(unittest.TestCase):
    def test_png_pixels_and_digest_use_one_read_even_when_name_changes(self):
        binary = ROOT / 'build-ww25-png/gx/png_capture_tests'
        if not binary.exists():
            self.fail('build the game-free png_capture_tests target before this check')
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww25-png-binding-') as directory:
            path = Path(directory) / 'capture.png'
            subprocess.run([str(binary), str(path)], check=True, capture_output=True)
            original = path.read_bytes()
            with patch.object(Path, 'read_bytes', side_effect=[original, b'replaced after first read']) as reader:
                metadata = png_metadata(path)
            self.assertEqual(reader.call_count, 1)
            self.assertEqual(metadata['sha256'], hashlib.sha256(original).hexdigest())
            self.assertEqual((metadata['width'], metadata['height'], metadata['distinct_rgba']), (3, 2, 6))
