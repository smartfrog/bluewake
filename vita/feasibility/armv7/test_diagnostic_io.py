import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_mem import ROOT, diagnostic_compile_args
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))
from publication import read_artifact

TOKEN = 'c' * 32


class DiagnosticIoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww25-diagnostic-')
        cls.addClassCleanup(cls.build.cleanup)
        cls.binary = Path(cls.build.name) / 'probe'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                        '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                        str(ROOT / 'vita/feasibility/armv7/diagnostic_io_probe.c'),
                        *diagnostic_compile_args(), '-Wl,--wrap=open', '-Wl,--wrap=close',
                        '-Wl,--wrap=fwrite', '-Wl,--wrap=fflush', '-Wl,--wrap=fclose',
                        '-o', str(cls.binary)], check=True, capture_output=True)

    def probe(self, root, scenario, expected=0):
        result = subprocess.run([str(self.binary), scenario], cwd=root, capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stderr)

    def read(self, root):
        return read_artifact(root / 'capture.final', 4096, candidate=root / 'capture.partial',
                             token=TOKEN, publication=2)

    def test_nominal_payload_is_certified_from_retained_candidate(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            root = Path(directory)
            self.probe(root, 'nominal')
            self.assertEqual(self.read(root), b'new evidence')
            self.assertFalse((root / 'capture.final').exists())
            self.assertEqual((root / 'capture.partial').stat().st_nlink, 1)

    def test_preexisting_file_or_symlink_is_never_altered(self):
        for kind in ('file', 'symlink'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                root = Path(directory)
                target = root / 'target'
                target.write_bytes(b'preserved target')
                candidate = root / 'capture.partial'
                if kind == 'file':
                    candidate.write_bytes(b'preexisting evidence')
                else:
                    candidate.symlink_to(target)
                self.probe(root, 'nominal', expected=1)
                self.assertEqual(candidate.read_bytes(), b'preexisting evidence' if kind == 'file' else b'preserved target')
                self.assertEqual(candidate.is_symlink(), kind == 'symlink')
                self.assertEqual(target.read_bytes(), b'preserved target')
                self.assertEqual(list(root.glob('*.seal')), [])

    def test_payload_write_flush_or_close_failure_never_creates_a_seal(self):
        for scenario in ('write-failure', 'flush-failure', 'close-failure'):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                root = Path(directory)
                self.probe(root, scenario, expected=1)
                self.assertTrue((root / 'capture.partial').exists())
                self.assertEqual(list(root.glob('*.seal')), [])
                with self.assertRaises(ValueError):
                    self.read(root)

    def test_source_replacement_before_or_after_commit_cannot_delete_or_certify_replacement(self):
        digest = hashlib.sha256(b'new evidence').hexdigest()
        for stage in ('before', 'after'):
            for kind in ('file', 'symlink'):
                with self.subTest(stage=stage, kind=kind), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                    root = Path(directory)
                    (root / 'target').write_bytes(b'preserved target')
                    self.probe(root, f'partial-{stage}-{kind}')
                    self.assertEqual((root / 'written.partial').read_bytes(), b'new evidence')
                    self.assertEqual((root / f'capture.final.v2.{TOKEN}.{digest}.12.seal').read_bytes(), b'')
                    self.assertEqual((root / 'target').read_bytes(), b'preserved target')
                    self.assertEqual((root / 'capture.partial').read_bytes(), b'replacement evidence' if kind == 'file' else b'preserved target')
                    with self.assertRaises((ValueError, OSError)):
                        self.read(root)

    def test_seal_creation_failure_propagates_without_certification(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            root = Path(directory)
            self.probe(root, 'seal-open-failure', expected=1)
            self.assertEqual(list(root.glob('*.seal')), [])
            with self.assertRaises(ValueError):
                self.read(root)

    def test_preexisting_seal_file_or_symlink_is_never_altered(self):
        digest = hashlib.sha256(b'new evidence').hexdigest()
        for kind in ('file', 'symlink', 'dangling-symlink'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                root = Path(directory)
                seal = root / f'capture.final.v2.{TOKEN}.{digest}.12.seal'
                target = root / 'target'
                if kind == 'file':
                    seal.write_bytes(b'existing seal entry')
                else:
                    if kind == 'symlink':
                        target.write_bytes(b'preserved target')
                    seal.symlink_to(target)
                self.probe(root, 'nominal', expected=1)
                self.assertEqual((root / 'capture.partial').read_bytes(), b'new evidence')
                if kind == 'dangling-symlink':
                    self.assertFalse(target.exists())
                else:
                    self.assertEqual(seal.read_bytes(), b'existing seal entry' if kind == 'file' else b'preserved target')
                self.assertEqual(seal.is_symlink(), kind != 'file')
                with self.assertRaises((ValueError, OSError)):
                    self.read(root)

    def test_seal_close_failure_never_reports_producer_success(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            root = Path(directory)
            self.probe(root, 'seal-close-failure', expected=1)
            self.assertEqual(self.read(root), b'new evidence')
            self.assertEqual(len(list(root.glob('*.seal'))), 1)

    def test_stopped_producer_retains_unsealed_uncertified_candidate(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            root = Path(directory)
            self.probe(root, 'stop-before-completion', expected=1)
            self.assertEqual((root / 'capture.partial').read_bytes(), b'new evidence')
            with self.assertRaises(ValueError):
                self.read(root)


if __name__ == '__main__':
    unittest.main()
