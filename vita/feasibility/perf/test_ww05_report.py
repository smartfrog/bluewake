"""Tests for scripts/vita/perf/ww05_report.py: boot-frontier evidence.

The WW05 report is the only real counter the Vita lane has today: one STATE
line per run with the guest's edge count and absolute cycles at the stop, and
a verdict that always says rendered=0. The parser extracts those numbers for a
run series; it does not replace scripts/vita/ww05/report.py, which certifies
the frontier. The integration tests read the real build-f5 evidence when it is
present on this machine and skip elsewhere.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/perf"))

from ww05_report import BootReportError, boot_series, parse_boot_report  # noqa: E402

EVIDENCE = Path("/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05")
TOKEN, SOURCE, PACKAGE = 'a' * 32, 'b' * 40, 'c' * 64

PASS_REPORT = f'''WW05 {TOKEN} {SOURCE} {PACKAGE} method=startup-gx-v1 phase=guest platform=VITA
ABI pointer=4 cpu=3496 version=6
ALLOC host=800000 mem1=25165824 vm=33554432 cache=16384 aliases=12 alias_count=2 owned_live=59536652
UNKNOWN residency native_stack_highwater pool_peak GPU
STATE entry=80003140 pc=80001008 sp=817FFF00 edges=2 exception=00000000 cycles=120
GX_FRONTIER complete=1 opcode=61 bytes=5 pc=80001000 completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 hex=61FEFFFFFF
STOP reason=first complete GX command; no renderer address=00000000 pc=00000000
END frontier=PASS live=0 rendered=0
'''

SHORT_REPORT = f'''WW05 {TOKEN} {SOURCE} {PACKAGE} method=startup-gx-v1 phase=guest platform=VITA
STOP reason=host allocation failed
END frontier=FAIL live=0 rendered=0
'''

FAIL_REPORT = PASS_REPORT.replace(
    'STATE entry=80003140 pc=80001008 sp=817FFF00 edges=2 exception=00000000 cycles=120',
    'STATE entry=80003140 pc=8030464C sp=8040CF58 edges=788 exception=00000001 cycles=116417'
).replace('complete=1 opcode=61', 'complete=0 opcode=00').replace(
    'reason=first complete GX command; no renderer', 'reason=unsupported host call').replace(
    'frontier=PASS', 'frontier=FAIL')


def real_reports():
    return sorted(EVIDENCE.glob('vita3k-run-*/data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.report'))


class BootReportTest(unittest.TestCase):
    def test_parses_a_complete_pass_report(self):
        report = parse_boot_report(PASS_REPORT)
        self.assertEqual((report.token, report.source, report.package), (TOKEN, SOURCE, PACKAGE))
        self.assertEqual((report.edges, report.cycles, report.exception), (2, 120, 0))
        self.assertEqual(report.verdict, 'PASS')
        self.assertEqual((report.rendered, report.frontier_complete, report.frontier_opcode), (0, True, 0x61))

    def test_parses_a_short_stop_report_without_state(self):
        report = parse_boot_report(SHORT_REPORT)
        self.assertEqual(report.verdict, 'FAIL')
        self.assertIsNone(report.edges)
        self.assertIsNone(report.cycles)
        self.assertEqual(report.reason, 'host allocation failed')

    def test_refuses_a_report_without_a_verdict(self):
        with self.assertRaises(BootReportError):
            parse_boot_report(PASS_REPORT.rsplit('END', 1)[0])

    def test_series_aggregates_edges_and_cycles(self):
        series = boot_series([parse_boot_report(PASS_REPORT), parse_boot_report(FAIL_REPORT)])
        self.assertEqual((series['runs'], series['pass'], series['fail']), (2, 1, 1))
        self.assertEqual(series['rendered_frames'], 0)
        self.assertEqual(series['edges'], {'min': 2, 'median': 395.0, 'max': 788})
        self.assertEqual(series['cycles'], {'min': 120, 'median': 58268.5, 'max': 116417})


class RealEvidenceTest(unittest.TestCase):
    def setUp(self):
        reports = real_reports()
        if not reports:
            self.skipTest('the build-f5 ww05 evidence is not on this machine')
        self.reports = reports

    def test_real_ww05_evidence_parses_into_a_run_series(self):
        series = boot_series([parse_boot_report(path.read_text()) for path in self.reports])
        self.assertEqual(series['runs'], len(self.reports))
        self.assertGreaterEqual(series['edges']['max'], 1)
        self.assertGreaterEqual(series['cycles']['max'], 0)

    def test_real_ww05_evidence_holds_no_rendered_frame(self):
        reports = [parse_boot_report(path.read_text()) for path in self.reports]
        self.assertEqual([report.rendered for report in reports], [0] * len(reports))


if __name__ == '__main__':
    unittest.main()
