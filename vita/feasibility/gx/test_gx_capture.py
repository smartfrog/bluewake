#!/usr/bin/env python3
"""VITA-WW-15 capture parser tests.

The bridge consumes what the WW05 boot host captures: the report's
GX_FRONTIER hex= field (the first complete GX command), gather-pipe batch
logs, or a raw binary FIFO dump. Every malformed shape is refused with a
ValueError; an empty capture (the real state today) and a partial capture
(host stopped mid-command) parse honestly.

Environment:
  WW15_EVIDENCE  build-f5 root holding real ww05.report files
                 (default <main checkout>/build-f5)
"""

import importlib.util
import os
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MAIN_CHECKOUT = Path("/home/fred/projects/zeldaWWHDRecomp")
DEFAULT_EVIDENCE = MAIN_CHECKOUT / "build-f5"

HEADER = "WW05 {token} {source} {package} method=startup-gx-v1 phase=guest platform=VITA\n"
EMPTY_FRONTIER = ("GX_FRONTIER complete=0 opcode=00 bytes=0 pc=00000000 "
                  "completion_pc=00000000 sp=00000000 trailing=0 edge_stop=0 hex=")


def load_module():
    spec = importlib.util.spec_from_file_location(
        "ww15_capture", ROOT / "scripts/vita/gx/capture.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def report(frontier, token="a" * 32, source="b" * 40, package="c" * 64):
    return (HEADER.format(token=token, source=source, package=package) +
            "ABI pointer=4 cpu=3496 version=6\n" +
            "STATE entry=80003140 pc=80003140 sp=817FFF00 edges=1 exception=00000001 cycles=0\n" +
            frontier + "\n" +
            "STOP reason=unsupported host call address=80003140 pc=80003140\n" +
            "END frontier=FAIL live=0 rendered=0\n")


class EmptyCaptureTest(unittest.TestCase):
    """The real state today: no GX command yet, hex= empty."""

    @classmethod
    def setUpClass(cls):
        cls.capture = load_module()

    def test_empty_frontier_parses_as_no_bytes(self):
        parsed = self.capture.parse_report(report(EMPTY_FRONTIER))
        self.assertFalse(parsed.complete)
        self.assertEqual(parsed.data, b"")
        self.assertEqual(parsed.size, 0)
        self.assertEqual(parsed.opcode, 0)

    def test_empty_frontier_keeps_its_provenance(self):
        parsed = self.capture.parse_report(report(EMPTY_FRONTIER), source="run-1.report")
        self.assertEqual(parsed.source, "run-1.report")


class CompleteCaptureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.capture = load_module()

    def test_complete_bp_write_decodes_its_bytes(self):
        frontier = ("GX_FRONTIER complete=1 opcode=61 bytes=5 pc=80001000 "
                    "completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 "
                    "hex=61FEFFFFFF")
        parsed = self.capture.parse_report(report(frontier))
        self.assertTrue(parsed.complete)
        self.assertEqual(parsed.data, bytes.fromhex("61FEFFFFFF"))
        self.assertEqual(parsed.opcode, 0x61)
        self.assertEqual(parsed.size, 5)
        self.assertEqual(parsed.pc, 0x80001000)
        self.assertEqual(parsed.completion_pc, 0x80001004)

    def test_trailing_bytes_are_reported_not_folded_in(self):
        frontier = ("GX_FRONTIER complete=1 opcode=08 bytes=6 pc=80001000 "
                    "completion_pc=80001008 sp=817FFF00 trailing=7 edge_stop=1 "
                    "hex=080012345678")
        parsed = self.capture.parse_report(report(frontier))
        self.assertEqual(parsed.trailing, 7)
        self.assertEqual(len(parsed.data), 6)

    def test_partial_cp_command_is_not_folded_into_a_complete_frame(self):
        frontier = ("GX_FRONTIER complete=0 opcode=08 bytes=3 pc=80001000 "
                    "completion_pc=00000000 sp=817FFF00 trailing=0 edge_stop=0 "
                    "hex=080012")
        parsed = self.capture.parse_report(report(frontier))
        self.assertFalse(parsed.complete)
        self.assertTrue(parsed.partial)
        self.assertEqual(parsed.data, bytes.fromhex("080012"))


class RefusalTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.capture = load_module()

    def test_missing_frontier_record_is_refused(self):
        with self.assertRaises(ValueError):
            self.capture.parse_report("STATE entry=80003140\nEND frontier=FAIL\n")

    def test_byte_count_must_match_the_hex_length(self):
        frontier = ("GX_FRONTIER complete=1 opcode=61 bytes=5 pc=80001000 "
                    "completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 "
                    "hex=61FEFFFF")
        with self.assertRaises(ValueError):
            self.capture.parse_report(report(frontier))

    def test_lowercase_hex_is_refused(self):
        frontier = ("GX_FRONTIER complete=1 opcode=61 bytes=5 pc=80001000 "
                    "completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 "
                    "hex=61feffffff")
        with self.assertRaises(ValueError):
            self.capture.parse_report(report(frontier))

    def test_complete_capture_without_bytes_is_refused(self):
        frontier = ("GX_FRONTIER complete=1 opcode=00 bytes=0 pc=80001000 "
                    "completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 hex=")
        with self.assertRaises(ValueError):
            self.capture.parse_report(report(frontier))

    def test_incomplete_capture_with_full_frame_is_refused(self):
        frontier = ("GX_FRONTIER complete=0 opcode=61 bytes=5 pc=80001000 "
                    "completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=0 "
                    "hex=61FEFFFFFF")
        with self.assertRaises(ValueError):
            self.capture.parse_report(report(frontier))

    def test_opcode_must_be_the_first_byte(self):
        frontier = ("GX_FRONTIER complete=1 opcode=08 bytes=6 pc=80001000 "
                    "completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 "
                    "hex=61FEFFFFFF")
        with self.assertRaises(ValueError):
            self.capture.parse_report(report(frontier))

    def test_unsupported_first_opcode_cannot_claim_complete(self):
        frontier = ("GX_FRONTIER complete=1 opcode=FF bytes=5 pc=80001000 "
                    "completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 "
                    "hex=FFFEFFFFFF")
        with self.assertRaises(ValueError):
            self.capture.parse_report(report(frontier))

    def test_xf_frame_must_carry_its_counted_words(self):
        frontier = ("GX_FRONTIER complete=1 opcode=10 bytes=13 pc=80001000 "
                    "completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 "
                    "hex=10000010000100000000000000")
        with self.assertRaises(ValueError):
            self.capture.parse_report(report(frontier))

    def test_two_frontier_records_are_ambiguous_and_refused(self):
        frontier = ("GX_FRONTIER complete=0 opcode=00 bytes=0 pc=00000000 "
                    "completion_pc=00000000 sp=00000000 trailing=0 edge_stop=0 hex=")
        with self.assertRaises(ValueError):
            self.capture.parse_report(report(EMPTY_FRONTIER) + report(frontier))


class GatherLogTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.capture = load_module()

    def test_batches_concatenate_in_stream_order(self):
        log = ("[boot] entry=0x80003140\n"
               "[gather-pipe] bytes=4 hex=61FEFFFF\n"
               "[exi] channel=0\n"
               "[gather-pipe] bytes=1 hex=FF\n")
        self.assertEqual(self.capture.parse_gather_log(log),
                         bytes.fromhex("61FEFFFFFF"))

    def test_log_without_batches_is_empty_not_an_error(self):
        self.assertEqual(self.capture.parse_gather_log("[boot] entry=0x80003140\n"), b"")

    def test_batch_size_must_match_the_hex_length(self):
        with self.assertRaises(ValueError):
            self.capture.parse_gather_log("[gather-pipe] bytes=2 hex=AB\n")

    def test_empty_batch_is_refused(self):
        with self.assertRaises(ValueError):
            self.capture.parse_gather_log("[gather-pipe] bytes=0 hex=\n")


class ClassifyCaptureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.capture = load_module()

    def test_report_text_classifies_as_report(self):
        kind, payload = self.capture.classify_capture(report(EMPTY_FRONTIER).encode())
        self.assertEqual(kind, "report")
        self.assertFalse(payload.complete)

    def test_gather_log_classifies_as_gather(self):
        kind, payload = self.capture.classify_capture(b"[gather-pipe] bytes=1 hex=FF\n")
        self.assertEqual(kind, "gather")
        self.assertEqual(payload, b"\xff")

    def test_non_text_bytes_classify_as_binary(self):
        kind, payload = self.capture.classify_capture(bytes.fromhex("61FEFFFFFF"))
        self.assertEqual(kind, "binary")
        self.assertEqual(payload, bytes.fromhex("61FEFFFFFF"))

    def test_text_without_capture_markers_is_refused(self):
        with self.assertRaises(ValueError):
            self.capture.classify_capture(b"just some text\n")


class RealEvidenceTest(unittest.TestCase):
    """The real reports in build-f5: empty hex= must parse honestly."""

    def setUp(self):
        self.capture = load_module()
        evidence = Path(os.environ.get("WW15_EVIDENCE", DEFAULT_EVIDENCE))
        self.reports = sorted(evidence.glob("ww05/vita3k-run-*/data/**/ww05.report"))
        if not self.reports:
            raise unittest.SkipTest(f"no real ww05.report evidence under {evidence}")

    def test_every_real_report_parses_and_stays_empty(self):
        for path in self.reports[:8]:
            parsed = self.capture.parse_report(path.read_text(), source=path.name)
            self.assertFalse(parsed.complete, path)
            self.assertEqual(parsed.data, b"", path)


if __name__ == "__main__":
    unittest.main()
