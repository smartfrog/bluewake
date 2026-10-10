import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOKEN, SOURCE, PACKAGE = 'a' * 32, 'b' * 40, 'c' * 64


class ReportTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('ww05_report', ROOT / 'scripts/vita/ww05/report.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.validate = module.validate
        self.content = f'''WW05 {TOKEN} {SOURCE} {PACKAGE} method=startup-gx-v1 phase=guest platform=VITA
ABI pointer=4 cpu=3496 version=6
ALLOC host=800000 mem1=25165824 vm=33554432 cache=16384 aliases=12 alias_count=2 owned_live=59536652
UNKNOWN residency native_stack_highwater pool_peak GPU
STATE entry=80003140 pc=80001008 sp=817FFF00 edges=2 exception=00000000 cycles=120
GX_FRONTIER complete=1 opcode=61 bytes=5 pc=80001000 completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 hex=61FEFFFFFF
STOP reason=first complete GX command; no renderer address=00000000 pc=00000000
END frontier=PASS live=0 rendered=0
'''

    def test_closed_frontier_is_not_a_rendered_scene(self):
        self.assertEqual(self.validate(self.content, TOKEN, SOURCE, PACKAGE), 'PASS')

    def test_truncated_report_is_rejected(self):
        with self.assertRaises(ValueError):
            self.validate(self.content.rsplit('END', 1)[0], TOKEN, SOURCE, PACKAGE)

    def test_host_report_cannot_certify_guest(self):
        with self.assertRaises(ValueError):
            self.validate(self.content.replace('platform=VITA', 'platform=HOST'), TOKEN, SOURCE, PACKAGE)

    def test_partial_command_cannot_claim_pass(self):
        content = self.content.replace('bytes=5', 'bytes=4').replace('61FEFFFFFF', '61FEFFFF')
        with self.assertRaises(ValueError):
            self.validate(content, TOKEN, SOURCE, PACKAGE)

    def test_unstopped_frontier_cannot_claim_pass(self):
        with self.assertRaises(ValueError):
            self.validate(self.content.replace('edge_stop=1', 'edge_stop=0'), TOKEN, SOURCE, PACKAGE)

    def test_unknown_service_can_publish_negative_evidence(self):
        content = self.content.replace('first complete GX command; no renderer', 'unsupported MMIO read')
        content = content.replace('frontier=PASS', 'frontier=FAIL')
        self.assertEqual(self.validate(content, TOKEN, SOURCE, PACKAGE), 'FAIL')

    def test_code_snapshot_owner_is_included_in_the_allocation_ledger(self):
        content = self.content.replace('cache=16384 aliases=', 'cache=16384 code_snapshot=4 aliases=')
        content = content.replace('owned_live=59536652', 'owned_live=59536656')
        self.assertEqual(self.validate(content, TOKEN, SOURCE, PACKAGE), 'PASS')

    def test_code_snapshot_owner_cannot_be_omitted_from_owned_live(self):
        content = self.content.replace('cache=16384 aliases=', 'cache=16384 code_snapshot=4 aliases=')
        with self.assertRaisesRegex(ValueError, 'ledger'):
            self.validate(content, TOKEN, SOURCE, PACKAGE)


if __name__ == '__main__':
    unittest.main()
