"""Tests for scripts/vita/relset/boot_residency.py.

The host lane reports REL activity with the markers it actually emits:
[rel-frontier] (the guest asked for a module), [rel-materialized] (the raw
image was copied into owned scratch) and [rel-link] (the owned module-1
contract was published). The ticket-14 [rel-load]/[rel-unload] contract is not
emitted yet; this miner reads what exists, bounds each marker by the recorded
cycles around it, and says so when the contract markers are absent. The
integration tests read the real build-f5 evidence when it is present and skip
elsewhere.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/relset"))

from boot_residency import (  # noqa: E402
    BootResidencyError,
    parse_report,
    parse_residency_events,
    summarize,
)

EVIDENCE = Path("/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05")

LOG = (
    "[progress] turns=306415 edges=874090 cycles=78747007 pc=80307EF4\n"
    "[dvd-completion] stage=return callback=802BCC78 completed=1 dispatches=1"
    " pc=8180FFF0 exception=00000000\n"
    "[rel-frontier] name=f_pc_profile_lst object=803A71C0 module=00000000"
    " pc=80240744\n"
    "[rel-materialized] module=1 name=f_pc_profile_lst bytes=16500"
    " guest=81822E80 owner=803A71C0 source=iso-RELS.arc backend=vita-sceio\n"
    "[rel-link] module=1 owner=803A71C0 guest=81822E80 profile=C1F80178"
    " bytes=2012 contract=static-composite-profile-list\n"
    "[dvd-open] kind=fast path= entry=70 file=803A713C backend=vita-sceio"
    " pc=8030F5A4\n"
    "[edge-progress] edges=874104 cycles=78800368 pc=80307EF4 tb=6562530"
    " dec=020FFDA3 pending=0 ee=1 downcount=0 budget=0\n")

REPORT = (
    "WW05 259f0cfd5978476999326ff87e0854f2 4f5c0745ca802f4183be616e730558841398b1cb"
    " 5be7bc078925a81db76a0a15dc26ebb6ed70ea8fa5aa5612cb72e58e3a06fbd3"
    " method=startup-gx-v1 phase=guest platform=VITA audio=deferred\n"
    "ALLOC host=794392 mem1=25165824 vm=33554432 cache=16384 code_snapshot=3364032"
    " aram=16777216 aliases=1542820 alias_count=1900 owned_live=81215100\n"
    "UNKNOWN residency native_stack_highwater pool_peak GPU\n"
    "STATE entry=80003140 pc=8028E9B8 sp=803EA498 edges=1036079"
    " exception=00000001 cycles=85390497\n"
    "GX_STREAM mode=continue decoded=8963 logged=2048 limit=2048 pending=0"
    " read_bytes=60512\n"
    "RENDER fifo_bytes=60512 events=17252 plans=9 draws=9 programs=9"
    " copies=0 display_copies=8 presented=8 gaps=0\n"
    "STOP reason=deferred DSP post-bootstrap task command not modeled;"
    " audio=deferred address=CC005002 pc=803190B0 width=2"
    " value=0000000000000002\n"
    "END frontier=PASS live=0 rendered=8\n")


class ParseResidencyEventsTest(unittest.TestCase):
    def test_bounds_each_marker_by_the_recorded_cycles_around_it(self):
        events = parse_residency_events(LOG)
        self.assertEqual([event.kind for event in events],
                         ["request", "materialized", "link"])
        for event in events:
            self.assertEqual(event.before_cycles, 78747007)
            self.assertEqual(event.after_cycles, 78800368)

    def test_reads_the_requested_name_and_object(self):
        request = parse_residency_events(LOG)[0]
        self.assertEqual(request.name, "f_pc_profile_lst")
        self.assertEqual(request.object, 0x803A71C0)
        self.assertEqual(request.module, 0)
        self.assertEqual(request.pc, 0x80240744)

    def test_reads_the_materialized_module_and_scratch_address(self):
        materialized = parse_residency_events(LOG)[1]
        self.assertEqual(materialized.module_id, 1)
        self.assertEqual(materialized.bytes, 16500)
        self.assertEqual(materialized.guest, 0x81822E80)
        self.assertEqual(materialized.owner, 0x803A71C0)
        self.assertEqual(materialized.source, "iso-RELS.arc")

    def test_reads_the_link_contract(self):
        link = parse_residency_events(LOG)[2]
        self.assertEqual(link.module_id, 1)
        self.assertEqual(link.profile, 0xC1F80178)
        self.assertEqual(link.bytes, 2012)
        self.assertEqual(link.contract, "static-composite-profile-list")

    def test_refuses_a_rel_marker_that_does_not_parse(self):
        with self.assertRaises(BootResidencyError):
            parse_residency_events("[rel-materialized] module=x\n")

    def test_ignores_lines_without_rel_markers(self):
        self.assertEqual(parse_residency_events("[progress] cycles=1\n"), [])


class ParseReportTest(unittest.TestCase):
    def test_reads_the_report_facts(self):
        report = parse_report(REPORT)
        self.assertEqual(report["cycles"], 85390497)
        self.assertEqual(report["alloc"]["mem1"], 25165824)
        self.assertEqual(report["alloc"]["vm"], 33554432)
        self.assertEqual(report["gx_stream"]["decoded"], 8963)
        self.assertEqual(report["render"]["presented"], 8)
        self.assertEqual(report["end"]["frontier"], "PASS")
        self.assertTrue(report["stop"].startswith("deferred DSP"))

    def test_refuses_a_report_without_state(self):
        with self.assertRaises(BootResidencyError):
            parse_report("ALLOC host=1\n")


class SummarizeTest(unittest.TestCase):
    def test_the_resident_set_is_what_was_materialized_and_not_released(self):
        summary = summarize(LOG, REPORT)
        self.assertEqual([entry["name"] for entry in summary["resident"]],
                         ["f_pc_profile_lst"])
        self.assertEqual(summary["resident"][0]["bytes"], 16500)
        self.assertEqual(summary["resident"][0]["guest"], 0x81822E80)
        self.assertFalse(summary["contract"]["rel_load_marker"])
        self.assertFalse(summary["contract"]["rel_unload_marker"])

    def test_records_the_scratch_capacity_rounded_to_32(self):
        summary = summarize(LOG, REPORT)
        self.assertEqual(summary["resident"][0]["capacity"], 16512)

    def test_a_boot_without_rel_events_has_an_empty_resident_set(self):
        summary = summarize("[progress] cycles=1\n", REPORT)
        self.assertEqual(summary["resident"], [])


class RealEvidenceTest(unittest.TestCase):
    RUN = EVIDENCE / ("vita3k-run-1351063a0837427b87d242d99ace55ec/data/Vita3K"
                      "/Vita3K/ux0/data/bluewake-ww-fixture")

    def setUp(self):
        if not self.RUN.is_dir():
            self.skipTest("the newest ww05 run is not on this machine")

    def test_the_newest_run_materializes_exactly_one_module(self):
        summary = summarize((self.RUN / "ww05.log").read_text(errors="replace"),
                            (self.RUN / "ww05.report").read_text(errors="replace"))
        self.assertEqual([entry["module_id"] for entry in summary["resident"]], [1])
        self.assertEqual(summary["resident"][0]["name"], "f_pc_profile_lst")
        self.assertEqual(summary["resident"][0]["capacity"], 16512)
        materialized = next(event for event in summary["events"]
                            if event["kind"] == "materialized")
        self.assertEqual(materialized["before_cycles"], 78787771)
        self.assertEqual(materialized["after_cycles"], 78800368)


if __name__ == "__main__":
    unittest.main()
