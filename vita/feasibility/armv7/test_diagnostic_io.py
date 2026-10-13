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
        header = root / 'psp2/io/fcntl.h'
        header.parent.mkdir(parents=True)
        header.write_text('int sceIoRename(const char* source, const char* destination);\n')
        cls.binaries = {}
        for platform in ('posix', 'vita-contract'):
            binary = root / platform
            flags = ['-D__vita__', '-I', str(root)] if platform == 'vita-contract' else []
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                            '-Wall', '-Wextra', '-Werror', '-UNDEBUG', *flags,
                            str(ROOT / 'vita/feasibility/armv7/diagnostic_io_probe.c'),
                            '-Wl,--wrap=link', '-Wl,--wrap=unlink', '-Wl,--wrap=fflush',
                            '-Wl,--wrap=fclose', '-o', str(binary)], check=True, capture_output=True)
            cls.binaries[platform] = binary

    def probe(self, root, platform, scenario):
        result = subprocess.run([str(self.binaries[platform]), scenario], cwd=root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_nominal_publication_is_complete_and_leaves_one_link_without_a_partial(self):
        for platform in self.binaries:
            with self.subTest(platform=platform), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                root = Path(directory)
                self.probe(root, platform, 'nominal')
                self.assertEqual((root / 'capture.final').read_bytes(), b'new evidence')
                self.assertEqual((root / 'capture.final').stat().st_nlink, 1)
                self.assertFalse((root / 'capture.partial').exists())

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

    def test_posix_unlink_error_retains_both_complete_links_and_reports_failure(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            root = Path(directory)
            self.probe(root, 'posix', 'unlink-failure')
            self.assertEqual((root / 'capture.partial').read_bytes(), b'new evidence')
            self.assertEqual((root / 'capture.final').read_bytes(), b'new evidence')
            self.assertEqual((root / 'capture.final').stat().st_nlink, 2)


if __name__ == '__main__':
    unittest.main()
