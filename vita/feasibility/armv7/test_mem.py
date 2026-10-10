import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]


def compile_probe(binary, sources, options=()):
    runtime = ROOT / "ref/vita-ww-01/RecompCore/GXRuntime"
    runtime_sources = ["core/cpu.c", "core/cpu_exception.c", "core/cpu_interpreter.c",
                       "core/cpu_interpreter_table.c", "core/cpu_interpreter_float.c",
                       "core/cpu_interpreter_integer.c", "guest_memory.c", "guest_memory_dirty.c"]
    fixture = ROOT / "vita/feasibility/armv7"
    command = [os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra",
               "-ffp-contract=off", "-fno-fast-math", "-I", str(runtime / "include"),
               *[str(fixture / name) for name in sources],
               str(fixture / "mem_scenarios.c"), str(fixture / "mem_platform.c"),
               *[str(runtime / "src" / name) for name in runtime_sources],
               *options, "-lm", "-o", str(binary)]
    result = subprocess.run(command, capture_output=True, text=True)
    binary.with_suffix(".compile.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)


class MemoryProbeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output = ROOT / "build-vita-ww-04/probe"
        output.mkdir(parents=True, exist_ok=True)
        cls.binary = output / "mem_probe"
        compile_probe(cls.binary, ["mem_probe.c"], ["-Wl,--wrap=calloc", "-Wl,--wrap=free"])

    def run_probe(self, scenario):
        result = subprocess.run([self.binary, scenario], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_backings_have_one_owner_and_release_to_zero(self):
        self.run_probe("real")

    def test_each_real_allocation_failure_releases_prior_owners(self):
        for allocation in range(1, 5):
            with self.subTest(allocation=allocation):
                self.run_probe(f"allocate-{allocation}")

    def test_checked_addition_rejects_overflow_without_changing_output(self):
        self.run_probe("overflow")

    def test_pattern_rejects_invalid_ranges(self):
        self.run_probe("pattern-invalid")

    def test_pattern_verification_rejects_corruption_at_last_edge(self):
        self.run_probe("pattern-corrupt")

    def test_snapshot_rejects_invalid_availability(self):
        self.run_probe("snapshot-invalid")

    def test_raw_invalid_snapshots_are_rejected(self):
        for scenario in ("snapshot-error", "snapshot-negative", "snapshot-size"):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_sdk_sequential_owners_release_to_zero(self):
        self.run_probe("sdk-healthy")

    def test_sdk_allocation_failures_release_prior_owners(self):
        for pool in range(3):
            with self.subTest(pool=pool):
                self.run_probe(f"sdk-alloc-{pool}")

    def test_sdk_base_failures_release_current_owner(self):
        for pool in range(3):
            with self.subTest(pool=pool):
                self.run_probe(f"sdk-base-{pool}")

    def test_sdk_info_failures_release_current_owner(self):
        for pool in range(3):
            with self.subTest(pool=pool):
                self.run_probe(f"sdk-info-{pool}")

    def test_failed_sdk_free_retains_live_owner_and_stops(self):
        for pool in range(3):
            with self.subTest(pool=pool):
                self.run_probe(f"sdk-free-{pool}")

    def test_missing_sdk_snapshot_is_not_successful_measurement(self):
        self.run_probe("sdk-snapshot-unknown")


if __name__ == "__main__":
    unittest.main()
