import pathlib
import subprocess
import tempfile
import unittest


SOURCE = pathlib.Path(__file__).resolve().parent


class ProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = pathlib.Path(cls.temp.name) / "probe"
        subprocess.run([
            "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-fno-builtin-memcpy", "-fno-lto", str(SOURCE / "core.c"),
            str(SOURCE / "copy.c"), str(SOURCE / "test_driver.c"),
            "-Wl,--wrap=memcpy", "-o", str(cls.binary),
        ], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_probe(self, *args):
        return subprocess.run([str(self.binary), *map(str, args)], capture_output=True, text=True)

    def test_success_records_and_status(self):
        result = self.run_probe()
        self.assertEqual((result.returncode, result.stdout), (0, "control-enter\ncontrol-pass\ncopy-enter\ncopy-leave\nstatus-pass\n"))

    def test_control_failure_prevents_copy(self):
        result = self.run_probe("control-fail")
        self.assertEqual((result.returncode, result.stdout), (2, "control-enter\ncontrol-fail\n"))

    def test_each_byte_is_validated(self):
        for offset in range(240):
            with self.subTest(offset=offset):
                result = self.run_probe("corrupt", offset)
                self.assertEqual(result.returncode, 3)
                self.assertTrue(result.stdout.endswith("copy-leave\nstatus-fail\n"))

    def test_out_of_bounds_writes_are_rejected(self):
        for offset in (-1, 240):
            with self.subTest(offset=offset):
                self.assertEqual(self.run_probe("corrupt", offset).returncode, 3)

    def test_record_failure_stops_immediately(self):
        result = self.run_probe("record-fail")
        self.assertEqual((result.returncode, result.stdout), (4, ""))


if __name__ == "__main__":
    unittest.main()
