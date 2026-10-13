import hashlib
from pathlib import Path
import tempfile
import unittest

from publication import read_artifact

TOKEN = 'c' * 32


class PublicationReaderTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww25-seals-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.logical = self.root / 'capture.png'
        self.candidate = self.root / 'capture.png.partial'
        self.candidate.write_bytes(b'abc')

    def seal(self, token=TOKEN, digest=None, size=3):
        digest = digest if digest is not None else hashlib.sha256(b'abc').hexdigest()
        path = self.root / f'capture.png.v2.{token}.{digest}.{size}.seal'
        path.write_bytes(b'')
        return path

    def read(self):
        return read_artifact(self.logical, 4096, token=TOKEN, publication=2)

    def test_v2_reads_exact_sealed_candidate_bytes(self):
        self.seal()
        self.assertEqual(self.read(), b'abc')

    def test_v2_missing_seal_never_falls_back_to_a_final_or_candidate(self):
        self.logical.write_bytes(b'abc')
        with self.assertRaises(ValueError):
            self.read()

    def test_malformed_seal_is_refused(self):
        (self.root / 'capture.png.v2.broken.seal').write_bytes(b'')
        with self.assertRaises(ValueError):
            self.read()

    def test_multiple_seals_are_refused(self):
        self.seal()
        self.seal(digest='0' * 64)
        with self.assertRaises(ValueError):
            self.read()

    def test_wrong_hash_is_refused(self):
        self.seal(digest='0' * 64)
        with self.assertRaises(ValueError):
            self.read()

    def test_wrong_token_is_refused(self):
        self.seal(token='d' * 32)
        with self.assertRaises(ValueError):
            self.read()

    def test_wrong_extent_is_refused(self):
        self.seal(size=4)
        with self.assertRaises(ValueError):
            self.read()

    def test_nonempty_seal_is_refused(self):
        self.seal().write_bytes(b'partial marker')
        with self.assertRaises(ValueError):
            self.read()

    def test_replaced_candidate_content_is_refused_without_deleting_it(self):
        self.seal()
        self.candidate.write_bytes(b'replacement')
        with self.assertRaises(ValueError):
            self.read()
        self.assertEqual(self.candidate.read_bytes(), b'replacement')

    def test_candidate_symlink_is_refused_without_touching_target(self):
        self.seal()
        target = self.root / 'target'
        target.write_bytes(b'abc')
        self.candidate.unlink()
        self.candidate.symlink_to(target)
        with self.assertRaises(OSError):
            self.read()
        self.assertEqual(target.read_bytes(), b'abc')

    def test_history_requires_explicit_v1_selection(self):
        self.logical.write_bytes(b'historical')
        self.assertEqual(read_artifact(self.logical, 4096, token=TOKEN, publication=1), b'historical')


if __name__ == '__main__':
    unittest.main()
