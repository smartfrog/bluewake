import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class DiagnosticIoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww25-diagnostic-')
        cls.addClassCleanup(cls.build.cleanup)
        root = Path(cls.build.name)
        cls.binaries = {}
        for platform in ('posix', 'vita-disabled-branch'):
            binary = root / platform
            flags = ['-D__vita__'] if platform == 'vita-disabled-branch' else []
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                            '-Wall', '-Wextra', '-Werror', '-UNDEBUG', *flags,
                            str(ROOT / 'vita/feasibility/armv7/diagnostic_io_probe.c'),
                            '-Wl,--wrap=link', '-Wl,--wrap=unlink', '-Wl,--wrap=fflush',
                            '-Wl,--wrap=fclose', '-o', str(binary)], check=True, capture_output=True)
            cls.binaries[platform] = binary

    def probe(self, root, platform, scenario):
        result = subprocess.run([str(self.binaries[platform]), scenario], cwd=root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_nominal_request_is_blocked_and_retains_the_written_partial_without_a_final(self):
        for platform in self.binaries:
            with self.subTest(platform=platform), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                root = Path(directory)
                self.probe(root, platform, 'nominal')
                self.assertEqual((root / 'capture.partial').read_bytes(), b'new evidence')
                self.assertEqual((root / 'capture.partial').stat().st_nlink, 1)
                self.assertFalse((root / 'capture.final').exists())

    def test_concurrent_existing_entry_is_never_replaced_and_partial_is_retained(self):
        for platform in self.binaries:
            for kind in ('file', 'symlink', 'dangling-symlink'):
                with self.subTest(platform=platform, kind=kind), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                    root = Path(directory)
                    (root / 'target').write_bytes(b'preserved target')
                    self.probe(root, platform, kind)
                    self.assertEqual((root / 'target').read_bytes(), b'preserved target')
                    self.assertEqual((root / 'capture.partial').read_bytes(), b'new evidence')
                    final = root / 'capture.final'
                    self.assertEqual(final.is_symlink(), kind != 'file')
                    if kind == 'dangling-symlink':
                        self.assertFalse((root / 'missing').exists())
                    else:
                        self.assertEqual(final.read_bytes(), b'existing evidence' if kind == 'file' else b'preserved target')

    def test_flush_close_and_publication_errors_keep_partial_and_propagate_failure(self):
        for platform in self.binaries:
            for scenario in ('flush-failure', 'close-failure', 'publish-failure'):
                with self.subTest(platform=platform, scenario=scenario), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                    root = Path(directory)
                    self.probe(root, platform, scenario)
                    self.assertTrue((root / 'capture.partial').exists())
                    self.assertFalse((root / 'capture.final').exists())

    def test_cleanup_is_never_attempted_and_does_not_create_an_extra_link(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            root = Path(directory)
            self.probe(root, 'posix', 'unlink-failure')
            self.assertEqual((root / 'capture.partial').read_bytes(), b'new evidence')
            self.assertFalse((root / 'capture.final').exists())
            self.assertEqual((root / 'capture.partial').stat().st_nlink, 1)

    def test_unproven_publication_fails_closed_without_touching_any_output_name(self):
        for platform in self.binaries:
            with self.subTest(platform=platform), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                root = Path(directory)
                self.probe(root, platform, 'blocked')
                self.assertEqual((root / 'capture.partial').read_bytes(), b'new evidence')
                self.assertFalse((root / 'capture.final').exists())

    def test_partial_substitution_before_publication_preserves_the_written_inode_and_replacement(self):
        for platform in self.binaries:
            for kind in ('file', 'symlink'):
                with self.subTest(platform=platform, kind=kind), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                    root = Path(directory)
                    (root / 'target').write_bytes(b'preserved target')
                    self.probe(root, platform, f'partial-before-{kind}')
                    self.assertEqual((root / 'written.partial').read_bytes(), b'new evidence')
                    self.assertEqual((root / 'target').read_bytes(), b'preserved target')
                    partial = root / 'capture.partial'
                    self.assertEqual(partial.is_symlink(), kind == 'symlink')
                    self.assertEqual(partial.read_bytes(), b'preserved target' if kind == 'symlink' else b'replacement evidence')
                    self.assertFalse((root / 'capture.final').exists())

    def test_no_final_is_created_so_post_publication_partial_substitution_and_cleanup_never_run(self):
        for platform in self.binaries:
            for kind in ('file', 'symlink'):
                with self.subTest(platform=platform, kind=kind), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                    root = Path(directory)
                    (root / 'target').write_bytes(b'preserved target')
                    self.probe(root, platform, f'partial-after-{kind}')
                    self.assertEqual((root / 'capture.partial').read_bytes(), b'new evidence')
                    self.assertFalse((root / 'capture.final').exists())
                    self.assertFalse((root / 'written.partial').exists())
                    self.assertEqual((root / 'target').read_bytes(), b'preserved target')


if __name__ == '__main__':
    unittest.main()
