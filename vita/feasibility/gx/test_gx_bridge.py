#!/usr/bin/env python3
"""VITA-WW-15 bridge tests: capture files through the whole chain.

The C++ bridge binary replays captured bytes through the pinned frontend,
translates every DrawPlan with gx_vita and generates bounded Cg programs; the
Python wrapper drives it with fixture streams and with real WW05 report
evidence from build-f5 (read-only). A malformed capture is refused; an empty
one reports honestly that no GX bytes exist yet.

Environment:
  WW02_BUILD     build directory (default <main checkout>/build-f2/ww02)
  WW15_EVIDENCE  build-f5 root holding real ww05.report files
"""

import importlib.util
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MAIN_CHECKOUT = Path("/home/fred/projects/zeldaWWHDRecomp")
DEFAULT_BUILD = MAIN_CHECKOUT / "build-f2" / "ww02"
DEFAULT_EVIDENCE = MAIN_CHECKOUT / "build-f5"
SOURCES = [
    ROOT / "vita/renderer/gx_bridge.h",
    ROOT / "vita/renderer/gx_bridge.cpp",
    ROOT / "vita/renderer/gx_bridge_tests.cpp",
    ROOT / "vita/feasibility/gx/gx_bridge_main.cpp",
]

HEADER = "WW05 {token} {source} {package} method=startup-gx-v1 phase=guest platform=VITA\n"
EMPTY_FRONTIER = ("GX_FRONTIER complete=0 opcode=00 bytes=0 pc=00000000 "
                  "completion_pc=00000000 sp=00000000 trailing=0 edge_stop=0 hex=")
COMPLETE_FRONTIER = ("GX_FRONTIER complete=1 opcode=61 bytes=5 pc=80001000 "
                     "completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 "
                     "hex=61FEFFFFFF")


def load_module(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def report(frontier):
    return (HEADER.format(token="a" * 32, source="b" * 40, package="c" * 64) +
            "ABI pointer=4 cpu=3496 version=6\n" +
            "STATE entry=80003140 pc=80003140 sp=817FFF00 edges=1 exception=00000001 cycles=0\n" +
            frontier + "\n" +
            "STOP reason=unsupported host call address=80003140 pc=80003140\n" +
            "END frontier=FAIL live=0 rendered=0\n")


def build_dir():
    return Path(os.environ.get("WW02_BUILD", DEFAULT_BUILD))


def check_binary(binary):
    """A missing binary skips; a stale one fails (never a silent old pass)."""
    if not binary.is_file():
        raise unittest.SkipTest(f"{binary.name} not built; run scripts/vita/gx/build.py")
    newest = max(source.stat().st_mtime for source in SOURCES if source.is_file())
    if binary.stat().st_mtime < newest:
        raise AssertionError(f"stale binary {binary}; run scripts/vita/gx/build.py")


class CoreChecksTest(unittest.TestCase):
    """The C++ bridge checks on fixture streams."""

    @classmethod
    def setUpClass(cls):
        binary = build_dir() / "gx-host" / "gx_bridge_tests"
        check_binary(binary)
        result = subprocess.run([str(binary)], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        cls.output = result.stdout
        cls.stderr = result.stderr
        cls.returncode = result.returncode

    def test_core_checks_pass(self):
        self.assertEqual(self.returncode, 0, self.stderr)
        self.assertIn("gx_bridge: all checks ok", self.output)

    def test_empty_stream_is_an_honest_no_op(self):
        self.assertIn("bridge empty ok", self.output)

    def test_fixture_stream_decodes_and_generates(self):
        self.assertIn("bridge textured_triangle ok", self.output)

    def test_indirect_refusal_keeps_its_reason(self):
        self.assertIn("bridge indirect refused: indirect TEV lookups not supported",
                      self.output)


class FixtureCliTest(unittest.TestCase):
    """The CLI on 02 fixture streams: decoded, translated, generated."""

    @classmethod
    def setUpClass(cls):
        cls.binary = build_dir() / "gx-host" / "gx_bridge"
        check_binary(cls.binary)

    def run_cli(self, *arguments):
        result = subprocess.run([str(self.binary), *arguments],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return result

    def test_textured_triangle_reaches_one_cg_program(self):
        result = self.run_cli("--fixture", "textured_triangle")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("bridge ok=1", result.stdout)
        self.assertIn("plans=1 plans_ok=1 plans_skipped=0", result.stdout)
        self.assertIn("programs=1 programs_refused=0", result.stdout)

    def test_indirect_triangle_is_refused_with_a_reason(self):
        result = self.run_cli("--fixture", "indirect_triangle")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("programs_refused=1", result.stdout)
        self.assertIn("refused reason=indirect TEV lookups not supported", result.stdout)

    def test_unknown_fixture_is_a_usage_error(self):
        result = self.run_cli("--fixture", "no_such_scenario")
        self.assertEqual(result.returncode, 2)

    def test_usage_error_without_an_input(self):
        result = self.run_cli()
        self.assertEqual(result.returncode, 2)


class CaptureBridgeTest(unittest.TestCase):
    """bridge.py: capture file in, combined report out."""

    @classmethod
    def setUpClass(cls):
        cls.bridge = ROOT / "scripts/vita/gx/bridge.py"
        check_binary(build_dir() / "gx-host" / "gx_bridge")
        cls.environment = dict(os.environ)

    def run_bridge(self, content, name="capture.txt"):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / name
            path.write_bytes(content)
            result = subprocess.run(["python3", str(self.bridge), str(path)],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    text=True, env=self.environment)
            return result

    def test_empty_report_reports_no_bytes_yet(self):
        result = self.run_bridge(report(EMPTY_FRONTIER).encode(), "empty.report")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("capture kind=report complete=0 bytes=0", result.stdout)
        self.assertIn("verdict=no-bytes-yet", result.stdout)

    def test_complete_report_runs_the_chain(self):
        result = self.run_bridge(report(COMPLETE_FRONTIER).encode(), "complete.report")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("capture kind=report complete=1 bytes=5 opcode=61", result.stdout)
        self.assertIn("bridge ok=1 fifo_bytes=5", result.stdout)
        self.assertIn("verdict=ran", result.stdout)

    def test_gather_log_batches_reach_the_chain(self):
        log = b"[boot] entry=0x80003140\n[gather-pipe] bytes=2 hex=61FE\n[gather-pipe] bytes=3 hex=FFFFFF\n"
        result = self.run_bridge(log, "gather.log")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("capture kind=gather bytes=5", result.stdout)
        self.assertIn("verdict=ran", result.stdout)

    def test_binary_fifo_dump_reaches_the_chain(self):
        result = self.run_bridge(bytes.fromhex("61FEFFFFFF"), "capture.fifo")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("capture kind=binary bytes=5", result.stdout)
        self.assertIn("verdict=ran", result.stdout)

    def test_malformed_report_is_refused(self):
        bad = report(COMPLETE_FRONTIER.replace("bytes=5", "bytes=4"))
        result = self.run_bridge(bad.encode(), "bad.report")
        self.assertEqual(result.returncode, 1)
        self.assertIn("verdict=rejected", result.stdout)
        self.assertIn("refusal reason=", result.stdout)

    def test_unrecognized_capture_is_refused(self):
        result = self.run_bridge(b"just some text\n", "notes.txt")
        self.assertEqual(result.returncode, 1)
        self.assertIn("verdict=rejected", result.stdout)

    def test_rejected_stream_keeps_the_frontend_detail(self):
        result = self.run_bridge(bytes.fromhex("800001"), "bad.fifo")
        self.assertEqual(result.returncode, 1)
        self.assertIn("verdict=rejected", result.stdout)
        self.assertIn("opcode=0x80", result.stdout)


class RealEvidenceTest(unittest.TestCase):
    """The real build-f5 reports, read-only: every one is empty today."""

    @classmethod
    def setUpClass(cls):
        cls.bridge = ROOT / "scripts/vita/gx/bridge.py"
        evidence = Path(os.environ.get("WW15_EVIDENCE", DEFAULT_EVIDENCE))
        cls.reports = sorted(evidence.glob("ww05/vita3k-run-*/data/**/ww05.report"))
        if not cls.reports:
            raise unittest.SkipTest(f"no real ww05.report evidence under {evidence}")
        check_binary(build_dir() / "gx-host" / "gx_bridge")

    def test_real_report_reports_no_bytes_yet(self):
        result = subprocess.run(["python3", str(self.bridge), str(self.reports[0])],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("capture kind=report complete=0 bytes=0", result.stdout)
        self.assertIn("verdict=no-bytes-yet", result.stdout)


if __name__ == "__main__":
    unittest.main()
