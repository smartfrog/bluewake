import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_mem import diagnostic_compile_args

ROOT = Path(__file__).resolve().parents[3]
TOKEN = 'c' * 32


class PublicationProducerTest(unittest.TestCase):
    def test_nominal_payload_is_retained_and_sealed_with_its_producer_hash(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww25-v2-') as directory:
            root = Path(directory)
            binary = root / 'probe'
            command = [os.environ.get('CC', 'cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                       '-I', str(ROOT / 'vita/feasibility/armv7'),
                       str(ROOT / 'vita/feasibility/armv7/publication_probe.c'),
                       *diagnostic_compile_args(), '-o', str(binary)]
            subprocess.run(command, check=True, capture_output=True)
            result = subprocess.run([str(binary)], cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            digest = hashlib.sha256(b'abc').hexdigest()
            seal = root / f'payload.v2.{TOKEN}.{digest}.3.seal'
            self.assertEqual(seal.read_bytes(), b'')
            self.assertEqual((root / 'payload.partial').read_bytes(), b'abc')


if __name__ == '__main__':
    unittest.main()
