"""End-to-end tests for scripts/vita/relset/residency_report.py.

The runner joins one boot run's evidence (ww05.log, ww05.report, optional MEM1
snapshots) with the build catalog (translator log, link map, verified raw REL
sizes) and writes residency.json plus summary.txt under --out. Outputs go to
the ignored build tree.
"""

from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
CLI = ROOT / "scripts/vita/relset/residency_report.py"

LOG = (
    "[progress] turns=1 cycles=100 pc=1\n"
    "[rel-frontier] name=f_pc_profile_lst object=803A71C0 module=00000000"
    " pc=80240744\n"
    "[rel-materialized] module=1 name=f_pc_profile_lst bytes=16500"
    " guest=81822E80 owner=803A71C0 source=iso-RELS.arc backend=vita-sceio\n"
    "[rel-link] module=1 owner=803A71C0 guest=81822E80 profile=C1F80178"
    " bytes=2012 contract=static-composite-profile-list\n"
    "[edge-progress] edges=2 cycles=200 pc=1\n")

REPORT = (
    "ALLOC host=1 mem1=25165824 vm=33554432 cache=16384 code_snapshot=1"
    " aram=16777216 aliases=1542820 alias_count=1900 owned_live=1\n"
    "STATE entry=80003140 pc=1 sp=2 edges=3 exception=00000001 cycles=200\n"
    "GX_STREAM mode=continue decoded=10 logged=2 limit=2048 pending=0"
    " read_bytes=8\n"
    "RENDER fifo_bytes=8 events=1 plans=0 draws=0 programs=0 copies=0"
    " display_copies=0 presented=0 gaps=0\n"
    "STOP reason=synthetic stop address=CC005002 pc=1 width=2 value=2\n"
    "END frontier=PASS live=0 rendered=0\n")

TRANSLATE = "  module 1: /rels/f_pc_profile_lst.rel -> base 0xC0400000\n"
MAP = (
    " .text          0x1    0x1b2a a(chunk_0000_rel1_C04000D4.c.obj)\n"
    " .rodata        0x2      0x9c a(chunk_0000_rel1_C04000D4.c.obj)\n")


def run_cli(*args):
    return subprocess.run([sys.executable, str(CLI), *args],
                          capture_output=True, text=True)


class ResidencyReportTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.run_dir = base / "run"
        self.evidence = base / "evidence"
        self.out = base / "out"
        (self.evidence / "logs").mkdir(parents=True)
        rels = self.evidence / "rel-importer-check-synthetic/private/rels"
        rels.mkdir(parents=True)
        self.run_dir.mkdir()
        (self.run_dir / "ww05.log").write_text(LOG)
        (self.run_dir / "ww05.report").write_text(REPORT)
        (self.run_dir / "ww05.mem1-1.bin").write_bytes(bytes([0, 1, 0, 0]))
        (self.run_dir / "ww05.mem1-2.bin").write_bytes(bytes([0, 3, 0, 7]))
        (self.evidence / "logs/translate-rels.log").write_text(TRANSLATE)
        (rels / "f_pc_profile_lst.rel").write_bytes(bytes(16500))
        self.map_path = self.evidence / "ww05.map"
        self.map_path.write_text(MAP)

    def tearDown(self):
        self.tmp.cleanup()

    def test_writes_the_resident_set_and_catalog_totals(self):
        result = run_cli("--run", str(self.run_dir), "--evidence", str(self.evidence),
                         "--map", str(self.map_path), "--out", str(self.out),
                         "--label", "synthetic", "--source", "0" * 40)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((self.out / "residency.json").read_text())
        self.assertEqual(report["label"], "synthetic")
        self.assertEqual([entry["module_id"] for entry in report["resident"]], [1])
        self.assertEqual(report["catalog"]["modules"], 1)
        self.assertEqual(report["catalog"]["compiled_bytes"], 0x1B2A + 0x9C)
        self.assertEqual(report["catalog"]["raw_bytes"], 16500)
        self.assertEqual(report["mem1"]["snapshots"], 2)
        self.assertEqual(report["mem1"]["growth"]["newly_written"], 1)
        self.assertEqual(report["mem1"]["growth"]["changed"], 2)
        self.assertIn("resident", (self.out / "summary.txt").read_text())

    def test_a_run_without_snapshots_still_reports_residency(self):
        (self.run_dir / "ww05.mem1-1.bin").unlink()
        (self.run_dir / "ww05.mem1-2.bin").unlink()
        result = run_cli("--run", str(self.run_dir), "--evidence", str(self.evidence),
                         "--map", str(self.map_path), "--out", str(self.out),
                         "--label", "synthetic", "--source", "0" * 40)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((self.out / "residency.json").read_text())
        self.assertIsNone(report["mem1"])

    def test_a_broken_rel_marker_fails_loudly(self):
        (self.run_dir / "ww05.log").write_text("[rel-materialized] module=x\n")
        result = run_cli("--run", str(self.run_dir), "--evidence", str(self.evidence),
                         "--map", str(self.map_path), "--out", str(self.out))
        self.assertEqual(result.returncode, 1)
        self.assertIn("materialized", result.stderr)


if __name__ == "__main__":
    unittest.main()
