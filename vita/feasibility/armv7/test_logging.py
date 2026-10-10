import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class VitaLoggingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output = ROOT / "build-vita-ww-01/logging-regression"
        output.mkdir(parents=True, exist_ok=True)
        cls.binary = output / "logging_probe"
        sdk = Path(os.environ.get("VITASDK", "~/vitasdk")).expanduser()
        command = [os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-Werror", "-D__vita__", "-ffunction-sections", "-fdata-sections",
                   "-idirafter", str(sdk / "arm-vita-eabi/include"),
                   "-I", str(ROOT / "ref/vita-ww-01/RecompCore/GXRuntime/include"),
                   "-Wl,--gc-sections", str(ROOT / "vita/feasibility/armv7/logging_probe.c"),
                   "-o", str(cls.binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        (output / "compile.log").write_text(shlex.join(command) + "\n" + result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f"logging probe build failed: {result.stdout}{result.stderr}")

    def run_probe(self, scenario):
        result = subprocess.run([self.binary, scenario], capture_output=True, text=True)
        proof = result.stdout + result.stderr
        print(proof, end="")
        self.assertEqual(result.returncode, 0, proof)

    def test_failed_write_remains_fatal_after_successful_flush(self):
        for stream in ("stdout", "stderr"):
            with self.subTest(stream=stream):
                self.run_probe(stream)

    def test_snapshot_write_failure_is_fatal(self):
        self.run_probe("snapshot")

    def test_both_streams_are_flushed_after_stdout_flush_failure(self):
        self.run_probe("flush-both")

    def test_healthy_completion_remains_successful(self):
        self.run_probe("healthy")

    def test_memory_query_failure_remains_fatal(self):
        self.run_probe("query-error")

    def test_report_is_not_published_after_failed_write(self):
        with tempfile.TemporaryDirectory(dir=self.binary.parent) as directory:
            partial = Path(directory) / "partial"
            complete = Path(directory) / "complete"
            partial.write_text("incomplete")
            result = subprocess.run([self.binary, "publish-write", partial, complete],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(complete.exists())

    def test_report_publication_checks_close(self):
        with tempfile.TemporaryDirectory(dir=self.binary.parent) as directory:
            partial = Path(directory) / "partial"
            complete = Path(directory) / "complete"
            partial.write_text("report")
            result = subprocess.run([self.binary, "publish-close", partial, complete],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(complete.exists())

    def test_successfully_closed_report_is_published(self):
        with tempfile.TemporaryDirectory(dir=self.binary.parent) as directory:
            partial = Path(directory) / "partial"
            complete = Path(directory) / "complete"
            partial.write_text("report")
            result = subprocess.run([self.binary, "publish-healthy", partial, complete],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(complete.read_text(), "report")
            self.assertFalse(partial.exists())


if __name__ == "__main__":
    unittest.main()
