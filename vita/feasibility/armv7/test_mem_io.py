import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_mem import ROOT, compile_probe
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))
from mem_report import validate


class MemoryIoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output = ROOT / "build-vita-ww-04-rework1-tests/io-probe"
        output.mkdir(parents=True, exist_ok=True)
        (output / "mem_identity.h").write_text('#define WW_MEM_SOURCE "' + "b" * 40 + '"\n')
        cls.binary = output / "mem_io_probe"
        compile_probe(cls.binary, ["mem_main.c", "mem_io_probe.c", "abi.c", "platform.c"],
                      ["-I", str(output), "-fno-builtin", "-Wl,--wrap=fopen", "-Wl,--wrap=fprintf",
                       "-Wl,--wrap=fflush", "-Wl,--wrap=fclose", "-Wl,--wrap=link"])

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=self.binary.parent)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        (self.directory / "mem.request").write_text("a" * 32 + " " + "b" * 40 + " " + "c" * 64 + "\n")

    def run_fixture(self, failure=""):
        return subprocess.run([self.binary], cwd=self.directory, capture_output=True, text=True,
                              env=dict(os.environ, MEM_IO_FAILURE=failure))

    def test_successful_report_is_closed_and_validated(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(validate((self.directory / "mem.report").read_text(), "a" * 32, "b" * 40, "c" * 64), 0)
        self.assertFalse((self.directory / "mem.partial").exists())

    def test_each_report_io_failure_is_nonzero_and_not_published(self):
        for failure in ("open", "write", "flush", "close", "rename"):
            with self.subTest(failure=failure):
                result = self.run_fixture(failure)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.directory / "mem.report").exists())
                partial = self.directory / "mem.partial"
                if partial.exists():
                    partial.rename(self.directory / failure)

    def test_stale_publication_is_preserved_without_execution(self):
        for name in ("mem.report", "mem.partial"):
            with self.subTest(name=name):
                path = self.directory / name
                path.write_text("stale")
                self.assertNotEqual(self.run_fixture().returncode, 0)
                self.assertEqual(path.read_text(), "stale")
                path.rename(self.directory / (name + ".preserved"))

    def test_wrong_source_request_is_rejected_before_publication(self):
        request = self.directory / "mem.request"
        request.write_text(request.read_text().replace("b" * 40, "d" * 40))
        self.assertNotEqual(self.run_fixture().returncode, 0)
        self.assertFalse((self.directory / "mem.partial").exists())

    def test_dangling_partial_symlink_is_not_followed(self):
        sentinel = self.directory / "sentinel"
        (self.directory / "mem.partial").symlink_to(sentinel)
        self.assertNotEqual(self.run_fixture().returncode, 0)
        self.assertFalse(sentinel.exists())


if __name__ == "__main__":
    unittest.main()
