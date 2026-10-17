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
        self.module = module
        self.content = f'''WW05 {TOKEN} {SOURCE} {PACKAGE} method=startup-gx-v1 phase=guest platform=VITA
ABI pointer=4 cpu=3496 version=6
ALLOC host=800000 mem1=25165824 vm=33554432 cache=16384 aliases=12 alias_count=2 owned_live=59536652
UNKNOWN residency native_stack_highwater pool_peak GPU
STATE entry=80003140 pc=80001008 sp=817FFF00 edges=2 exception=00000000 cycles=120
GX_FRONTIER complete=1 opcode=61 bytes=5 pc=80001000 completion_pc=80001004 sp=817FFF00 trailing=0 edge_stop=1 hex=61FEFFFFFF
STOP reason=first complete GX command; no renderer address=00000000 pc=00000000
RENDER fifo_bytes=0 events=0 plans=0 plans_skipped=0 draws=0 programs=0 programs_distinct=0 programs_refused=0 copies=0 copies_refused=0 display_copies=0 presented=0 gaps=0
END frontier=PASS live=0 rendered=0
'''

    def test_closed_frontier_is_not_a_rendered_scene(self):
        self.assertEqual(self.validate(self.content, TOKEN, SOURCE, PACKAGE), 'PASS')

    def test_deferred_audio_report_requires_matching_explicit_profile_authorization(self):
        for declared, requested in ((True, True), (True, False), (False, True)):
            with self.subTest(declared=declared, requested=requested):
                content = self.continuation_report()
                if declared:
                    content = content.replace('platform=VITA\n', 'platform=VITA audio=deferred\n')
                if declared == requested:
                    self.assertEqual(self.validate(content, TOKEN, SOURCE, PACKAGE, continue_gx=64, defer_audio=requested), 'PASS')
                else:
                    with self.assertRaises(ValueError):
                        self.validate(content, TOKEN, SOURCE, PACKAGE, continue_gx=64, defer_audio=requested)

    def test_real_present_count_is_accepted_in_a_continuation(self):
        content = self.continuation_report().replace('display_copies=0 presented=0', 'display_copies=2 presented=2')
        content = content.replace('rendered=0', 'rendered=2')
        self.assertEqual(self.validate(content, TOKEN, SOURCE, PACKAGE, continue_gx=64), 'PASS')

    def test_end_count_cannot_disagree_with_renderer_presentations(self):
        with self.assertRaisesRegex(ValueError, 'renderer'):
            self.validate(self.continuation_report().replace('rendered=0', 'rendered=2'),
                          TOKEN, SOURCE, PACKAGE, continue_gx=64)

    def test_renderer_fields_are_strictly_validated(self):
        with self.assertRaisesRegex(ValueError, 'renderer'):
            self.validate(self.content.replace('events=0', 'events=unknown'), TOKEN, SOURCE, PACKAGE)

    def test_renderer_diagnostics_are_accepted_and_validated(self):
        content = self.continuation_report().replace(
            'RENDER fifo_bytes=0 events=0 plans=0 plans_skipped=0 draws=0 programs=0 programs_distinct=0 '
            'programs_refused=0 copies=0 copies_refused=0 display_copies=0 presented=0 gaps=0',
            'RENDER fifo_bytes=0 events=0 plans=0 plans_skipped=0 draws=0 programs=0 programs_distinct=0 '
            'programs_refused=1 copies=0 copies_refused=0 display_copies=0 presented=0 gaps=1\n'
            'RENDER-REFUSAL group=shader reason=indirect texture resource unavailable\n'
            'RENDER-GAP name=indirect_ignored value=1')
        self.assertEqual(self.validate(content, TOKEN, SOURCE, PACKAGE, continue_gx=64), 'PASS')

    def test_renderer_diagnostic_records_are_strictly_validated(self):
        for bad in ('RENDER-REFUSAL group=plan', 'RENDER-REFUSAL group=other reason=x',
                    'RENDER-REFUSAL group=shader reason=', 'RENDER-GAP name=indirect_ignored',
                    'RENDER-GAP value=1', 'RENDER-GAP name=Indirect value=1'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.validate(self.continuation_report().replace(
                    'END frontier=PASS live=0 rendered=0', bad + '\nEND frontier=PASS live=0 rendered=0'),
                    TOKEN, SOURCE, PACKAGE, continue_gx=64)

    def continuation_report(self):
        return self.content.replace('STOP reason=first complete GX command; no renderer address=00000000 pc=00000000',
                                    'GX_STREAM mode=continue decoded=1 logged=1 limit=64 pending=0 read_bytes=32\n'
                                    'STOP reason=unsupported MMIO read address=CC000006 pc=80001008')

    def test_continuation_keeps_first_frontier_pass_separate_from_later_service_failure(self):
        self.assertEqual(self.validate(self.continuation_report(), TOKEN, SOURCE, PACKAGE, continue_gx=64), 'PASS')

    def test_continuation_cannot_be_accepted_as_the_default_first_command_mode(self):
        with self.assertRaises(ValueError):
            self.validate(self.continuation_report(), TOKEN, SOURCE, PACKAGE)

    def test_continuation_report_must_match_its_opt_in_limit_and_real_counters(self):
        for old, new in (('limit=64', 'limit=32'), ('decoded=1', 'decoded=0'),
                         ('logged=1', 'logged=2'), ('read_bytes=32', 'read_bytes=0'),
                         ('pending=0', 'pending=1026')):
            with self.subTest(new=new), self.assertRaises(ValueError):
                self.validate(self.continuation_report().replace(old, new), TOKEN, SOURCE, PACKAGE, continue_gx=64)

    def test_command_journal_must_bind_its_first_record_to_the_frontier(self):
        journal = '[gx-command] index=1 opcode=61 bytes=5 pc=80001000 completion_pc=80001004 sp=817FFF00 hex=61FEFFFFFF\n'
        self.assertEqual(self.module.validate_commands(journal, self.continuation_report(), 64), 1)

    def draw_journal(self):
        report = self.continuation_report().replace('decoded=1 logged=1 limit=64', 'decoded=2 logged=2 limit=2048')
        report = report.replace('read_bytes=32', 'read_bytes=64')
        first = '[gx-command] index=1 opcode=61 bytes=5 pc=80001000 completion_pc=80001004 sp=817FFF00 hex=61FEFFFFFF\n'
        witness = '[gx-command-layout] index=2 format=0 vertices=3 stride=12 bytes=39\n'
        draw = '[gx-command] index=2 opcode=90 bytes=39 pc=80001008 completion_pc=8000100C sp=817FFF00 hex=900003' + '00' * 36 + '\n'
        return report, first, witness, draw

    def test_full_journal_accepts_draw_bytes_bound_to_the_pinned_layout_witness(self):
        report, first, witness, draw = self.draw_journal()
        self.assertEqual(self.module.validate_commands(first + witness + draw, report, 2048), 2)

    def test_missing_or_malformed_draw_witness_cannot_certify_the_journal(self):
        report, first, witness, draw = self.draw_journal()
        for changed in ('', witness + witness, witness.replace('index=2', 'index=3'),
                        witness.replace('format=0', 'format=1'), witness.replace('vertices=3', 'vertices=0'),
                        witness.replace('stride=12', 'stride=8'), witness.replace('bytes=39', 'bytes=40')):
            with self.subTest(witness=changed), self.assertRaises(ValueError):
                self.module.validate_commands(first + changed + draw, report, 2048)

    def test_journal_cannot_claim_more_command_bytes_than_the_real_cp_reads(self):
        report, first, witness, draw = self.draw_journal()
        with self.assertRaises(ValueError):
            self.module.validate_commands(first + witness + draw, report.replace('read_bytes=64', 'read_bytes=32'), 2048)

    def test_non_draw_commands_cannot_carry_a_layout_witness(self):
        report, first, witness, draw = self.draw_journal()
        with self.assertRaises(ValueError):
            self.module.validate_commands(first + witness.replace('index=2', 'index=1') + draw, report, 2048)

    def test_journal_rejects_missing_reordered_changed_or_unframed_command_evidence(self):
        journal = '[gx-command] index=1 opcode=61 bytes=5 pc=80001000 completion_pc=80001004 sp=817FFF00 hex=61FEFFFFFF\n'
        for content in ('', journal.replace('index=1', 'index=2'), journal.replace('61FEFFFFFF', '610F0000FF'),
                        journal.replace('bytes=5', 'bytes=4'), journal + journal):
            with self.subTest(content=content), self.assertRaises(ValueError):
                self.module.validate_commands(content, self.continuation_report(), 64)

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

    def test_aram_owner_is_included_in_the_allocation_ledger(self):
        content = self.content.replace('cache=16384 aliases=', 'cache=16384 aram=16777216 aliases=')
        content = content.replace('owned_live=59536652', 'owned_live=76313868')
        self.assertEqual(self.validate(content, TOKEN, SOURCE, PACKAGE), 'PASS')

    def test_aram_owner_cannot_be_omitted_from_owned_live(self):
        content = self.content.replace('cache=16384 aliases=', 'cache=16384 aram=16777216 aliases=')
        with self.assertRaises(ValueError):
            self.validate(content, TOKEN, SOURCE, PACKAGE)

    def test_negative_report_retains_the_observed_service_write(self):
        content = self.content.replace('first complete GX command; no renderer', 'SI transfer unsupported')
        content = content.replace('frontier=PASS', 'frontier=FAIL')
        content = content.replace('pc=00000000\nRENDER', 'pc=00000000 width=4 value=0000000040000201\nRENDER')
        self.assertEqual(self.validate(content, TOKEN, SOURCE, PACKAGE), 'FAIL')

    def test_service_write_width_and_value_must_agree(self):
        content = self.content.replace('frontier=PASS', 'frontier=FAIL')
        content = content.replace('pc=00000000\nRENDER', 'pc=00000000 width=1 value=0000000040000201\nRENDER')
        with self.assertRaisesRegex(ValueError, 'service write'):
            self.validate(content, TOKEN, SOURCE, PACKAGE)

    def test_pass_cannot_contain_an_unsupported_service_write(self):
        content = self.content.replace('pc=00000000\nRENDER', 'pc=00000000 width=4 value=0000000040000201\nRENDER')
        with self.assertRaises(ValueError):
            self.validate(content, TOKEN, SOURCE, PACKAGE)


if __name__ == '__main__':
    unittest.main()
