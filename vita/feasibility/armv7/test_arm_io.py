import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_arm_report import ROOT
from arm_report import validate


class ArmIoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output = ROOT / "build-ww08-io-tests"
        cc = os.environ.get("CC", "gcc")
        cmake = os.environ.get("CMAKE", "cmake")
        ninja = os.environ.get("NINJA", "ninja")
        command = [sys.executable, ROOT / "scripts/vita/armv7/arm_build.py", "--out", cls.output,
                   "--cc", cc, "--cmake", cmake, "--ninja", ninja]
        cls.output.mkdir(exist_ok=True)
        result = subprocess.run(command, capture_output=True, text=True)
        (cls.output / "build-test.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        cls.source = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        build = cls.output / "io-probe"
        for command in ([cmake, "-S", ROOT / "vita/feasibility/armv7", "-B", build, "-G", "Ninja",
                         f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_MAKE_PROGRAM={ninja}", "-DWW_MODE=arm",
                         "-DWW_IO_PROBE=ON", f"-DFIXTURE_DIR={cls.output / 'fp-fixture'}",
                         f"-DRECOMPCORE_DIR={ROOT / 'ref/vita-ww-01/RecompCore'}"],
                        [cmake, "--build", build, "-j", "2"]):
            result = subprocess.run(command, capture_output=True, text=True)
            with (cls.output / "build-test.log").open("a") as stream:
                stream.write(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
        cls.binary = build / "ww_fixture"

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir=self.output)
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        (self.directory / "arm.request").write_text(f"{'a' * 32} {self.source} {'c' * 64}\n")

    def run_fixture(self, failure="", directory=None):
        return subprocess.run([self.binary], cwd=directory or self.directory, capture_output=True, text=True,
                              env=dict(os.environ, ARM_IO_FAILURE=failure))

    def test_host_uses_real_71_runtime_cases_and_never_claims_arm_control_pass(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stderr)
        text = (self.directory / "arm.report").read_text()
        self.assertEqual(validate(text, "a" * 32, self.source, "c" * 64, "HOST"), 0)
        self.assertEqual(text.count("RUNTIME "), 71)
        self.assertEqual(text.count(" SKIP\n"), 32)

    def test_failed_report_io_never_publishes_a_successful_terminal_report(self):
        for failure in ("open", "write", "flush", "close", "rename"):
            with self.subTest(failure=failure):
                directory = self.directory / failure
                directory.mkdir()
                (directory / "arm.request").write_text(f"{'a' * 32} {self.source} {'c' * 64}\n")
                self.assertNotEqual(self.run_fixture(failure, directory).returncode, 0)
                self.assertFalse((directory / "arm.report").exists())

    def test_stale_or_symlinked_outputs_are_preserved_without_execution(self):
        for filename in ("arm.report", "arm.partial"):
            with self.subTest(filename=filename):
                path = self.directory / filename
                path.symlink_to(self.directory / "absent-sentinel")
                self.assertNotEqual(self.run_fixture().returncode, 0)
                self.assertTrue(path.is_symlink())
                self.assertFalse((self.directory / "absent-sentinel").exists())
                path.rename(self.directory / (filename + ".preserved"))

    def test_wrong_source_request_is_rejected_before_any_report(self):
        (self.directory / "arm.request").write_text(f"{'a' * 32} {'d' * 40} {'c' * 64}\n")
        self.assertNotEqual(self.run_fixture().returncode, 0)
        self.assertFalse((self.directory / "arm.partial").exists())

    def test_fake_fpscr_is_restored_on_every_report_io_error(self):
        for failure in ("open", "write", "flush", "close", "rename"):
            with self.subTest(failure=failure):
                directory = self.directory / failure
                directory.mkdir()
                (directory / "arm.request").write_text(f"{'a' * 32} {self.source} {'c' * 64}\n")
                result = subprocess.run([self.binary], cwd=directory, capture_output=True, text=True,
                                        env=dict(os.environ, ARM_IO_FAILURE=failure, ARM_FAKE_GUARD="1"))
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual((directory / "fake-guard.audit").read_text(), "FAKE initial=a6000090 final=a6000090\n")
