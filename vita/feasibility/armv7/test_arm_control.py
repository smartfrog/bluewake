import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]


class ArmControlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixture = ROOT / "vita/feasibility/armv7"
        output = ROOT / "build-ww08-tests"
        output.mkdir(exist_ok=True)
        cls.binary = output / "control-probe"
        result = subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra",
                                 "-Werror", fixture / "arm_control_probe.c", fixture / "arm_control.c",
                                 "-o", cls.binary], capture_output=True, text=True)
        (output / "control-compile.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def run_probe(self, scenario):
        self.assertEqual(subprocess.run([self.binary, scenario]).returncode, 0)

    def test_rounding_and_fz_mapping_preserve_unrelated_bits(self):
        self.run_probe("mapping")

    def test_initial_environment_is_restored_for_each_runtime_case(self):
        self.run_probe("restore")

    def test_failed_restoration_readback_is_not_success(self):
        self.run_probe("readback-error")

    def test_runtime_observations_do_not_prepare_guest_rounding(self):
        self.run_probe("runtime-observer")
