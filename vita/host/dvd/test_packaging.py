"""Tests for the VITA-WW-13 packaging tools: the synthetic image generator and
the ux0 install layout. No game data is involved."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts/vita/dvd'))

import iso_layout  # noqa: E402
import make_synthetic_image  # noqa: E402

OUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f2/ww13-seam/tools-tests')


class SyntheticImageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)

    def test_image_has_the_gamecube_header(self):
        image = make_synthetic_image.build_image()
        self.assertEqual(len(image), make_synthetic_image.DISC_SIZE)
        self.assertEqual(struct.unpack_from('>I', image, 0x1C)[0], make_synthetic_image.MAGIC)
        fst_off, fst_size = struct.unpack_from('>II', image, 0x424)
        self.assertEqual(fst_off, make_synthetic_image.FST_OFFSET)
        self.assertEqual(struct.unpack_from('>I', image, fst_off + 8)[0], len(make_synthetic_image.ENTRIES))
        self.assertEqual(fst_size, 5 * 12 + len(make_synthetic_image.STRINGS))

    def test_image_files_carry_the_pattern(self):
        image = make_synthetic_image.build_image()
        self.assertEqual(image[0x500], make_synthetic_image.pattern(0x500))
        self.assertEqual(image[0x5FF], make_synthetic_image.pattern(0x5FF))

    def test_write_image_creates_a_file(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            path = make_synthetic_image.write_image(Path(directory) / 'image.iso')
            self.assertTrue(path.is_file())
            self.assertEqual(path.stat().st_size, make_synthetic_image.DISC_SIZE)

    def test_validate_rejects_a_non_image(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            junk = Path(directory) / 'junk.iso'
            junk.write_bytes(b'not a disc image')
            with self.assertRaises(SystemExit):
                iso_layout.validate_image(junk)


class IsoLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)

    def synthetic_image(self, directory):
        return make_synthetic_image.write_image(Path(directory) / 'image.iso')

    def test_install_symlinks_the_install_path(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            iso = self.synthetic_image(root)
            target, size, action = iso_layout.install(iso, root / 'ux0')
            self.assertEqual(action, 'symlinked')
            self.assertEqual(size, make_synthetic_image.DISC_SIZE)
            self.assertTrue(target.is_symlink())
            self.assertEqual(target.resolve(), iso.resolve())
            self.assertEqual(target, root / 'ux0' / iso_layout.TARGET_RELATIVE)

    def test_install_is_idempotent_for_the_same_link(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            iso = self.synthetic_image(root)
            iso_layout.install(iso, root / 'ux0')
            target, _, action = iso_layout.install(iso, root / 'ux0')
            self.assertEqual(action, 'already installed')
            self.assertEqual(target.resolve(), iso.resolve())

    def test_copy_mode_writes_a_real_file(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            root = Path(directory)
            iso = self.synthetic_image(root)
            target, _, action = iso_layout.install(iso, root / 'ux0', copy=True)
            self.assertEqual(action, 'copied')
            self.assertFalse(target.is_symlink())
            self.assertEqual(target.read_bytes(), iso.read_bytes())

    def test_install_refuses_a_destination_inside_the_worktree(self):
        with tempfile.TemporaryDirectory(dir=OUT) as directory:
            iso = self.synthetic_image(Path(directory))
            with self.assertRaises(SystemExit):
                iso_layout.install(iso, ROOT)


if __name__ == '__main__':
    unittest.main()
