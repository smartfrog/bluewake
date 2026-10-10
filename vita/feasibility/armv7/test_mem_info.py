import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_mem import ROOT, compile_probe


class MemoryInfoSizeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output = ROOT / "build-vita-ww-04-rework1-tests/info-probe"
        output.mkdir(parents=True, exist_ok=True)
        (output / "mem_identity.h").write_text('#define WW_MEM_SOURCE "' + "b" * 40 + '"\n')
        cls.binary = output / "mem_info_probe"
        sdk = Path(os.environ["VITASDK"])
        compile_probe(cls.binary, ["mem_info_probe.c", "mem_main.c", "abi.c"],
                      ["-I", str(output), "-idirafter", str(sdk / "arm-vita-eabi/include"),
                       "-Wl,--wrap=mem_sdk_api"])

    def test_native_info_size_is_checked_before_writing_and_freed_once(self):
        for scenario in ("valid", "invalid"):
            with self.subTest(scenario=scenario):
                result = subprocess.run([self.binary, scenario], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_successful_api_with_wrong_size_publishes_closed_failure_and_stops(self):
        with tempfile.TemporaryDirectory(dir=self.binary.parent) as temporary:
            directory = Path(temporary)
            (directory / "mem.request").write_text("a" * 32 + " " + "b" * 40 + " " + "c" * 64 + "\n")
            result = subprocess.run([self.binary, "report"], cwd=directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            observation = re.fullmatch(r"API result=0 returned=(\d+) required=(\d+) allocations=1 releases=1 written=0 live=0 exit=1\n", result.stdout)
            self.assertIsNotNone(observation, result.stdout)
            returned, required = observation.groups()
            self.assertEqual(int(returned), int(required) - 1)
            report = (directory / "mem.report").read_text()
            self.assertIn(f"info=0 info_size={returned} info_required={required} free=0", report)
            self.assertIn("mapped=0 touched=0 views=0 available=1 uid=1", report)
            self.assertTrue(report.endswith("END real=PASS sdk=FAIL failures=1\n"), report)
            self.assertFalse((directory / "mem.partial").exists())


if __name__ == "__main__":
    unittest.main()
