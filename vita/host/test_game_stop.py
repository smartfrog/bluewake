import subprocess
import unittest

import test_host
import test_io


class GameStopTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario, *arguments):
        result = subprocess.run([str(test_host.OUT / 'game_stop_probe'), scenario,
                                 *map(str, arguments)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stderr

    def test_OSPanic_entry_captures_desktop_argument_registers_and_text_before_body_changes(self):
        log = self.probe('panic')
        self.assertIn('[panic] OSPanic file=0x80008000 line=321 format=0x80008100', log)
        self.assertIn('r6=0x12345678 r7=0x87654321 r8=0xAABBCCDD lr=0x80001234', log)
        self.assertIn('pc=0x80006C4C retraces=0', log)
        self.assertIn('r26=0xB000001A', log)
        self.assertIn('r31=0xB000001F file="synthetic_assert.cpp" format_text="ship assertion %s:%u"', log)

    def test_OSPanic_entry_is_journaled_once(self):
        self.assertEqual(self.probe('panic-repeat').count('[panic] OSPanic file='), 1)

    def test_null_text_pointers_match_desktop_empty_text(self):
        self.assertIn('file="" format_text=""', self.probe('text-null'))

    def test_text_is_truncated_and_terminated_at_desktop_buffer_capacities(self):
        self.assertIn(f'file="{"F" * 159}" format_text="{"M" * 255}"', self.probe('text-long'))

    def test_text_can_be_read_from_an_existing_linked_alias(self):
        self.assertIn('format_text="linked assertion"', self.probe('text-alias'))

    def test_invalid_text_stops_without_reading_mmio_or_wrapping_the_pointer(self):
        for scenario in ('text-invalid', 'text-wrap'):
            with self.subTest(scenario=scenario):
                self.probe(scenario)

    def test_PPCHalt_entry_captures_desktop_state_and_stops_before_the_spin_loop(self):
        for scenario, pc in (('halt', '8030150C'), ('halt-nop', '80301510')):
            with self.subTest(scenario=scenario):
                log = self.probe(scenario)
                self.assertIn(f'[halt] PPCHalt entry pc=0x{pc} lr=0x80001234 r1=0x80007000', log)
                self.assertIn('current_thread=0x803A2960 current_context=0x803A2970 run_bits=0x40000000', log)
                self.assertIn('[halt] frame0 sp=0x80007000 back=0x80007010 lr=0x80002000', log)
                self.assertIn('[halt] frame1 sp=0x80007010 back=0x00000000 lr=0x80002004', log)

    def test_back_chain_stops_on_game_terminators_and_desktop_invalid_successors(self):
        for terminator in (0, 0xFFFFFFFF, 0x80007010, 0x80007000, 0x82000000):
            with self.subTest(terminator=terminator):
                log = self.probe('walk-next', hex(terminator))
                self.assertEqual(log.count('[halt] frame'), 2)
                self.assertIn(f'back=0x{terminator:08X} lr=0x80002004', log)

    def test_initial_null_or_sentinel_stack_pointer_is_not_dereferenced(self):
        for terminator in (0, 0xFFFFFFFF):
            with self.subTest(terminator=terminator):
                self.assertNotIn('[halt] frame', self.probe('walk-initial', hex(terminator)))

    def test_back_chain_is_limited_to_eight_frames(self):
        log = self.probe('walk-next', '0x80007020')
        self.assertEqual(log.count('[halt] frame'), 8)
        self.assertIn('[halt] frame7 sp=0x80007070 back=0x80007080 lr=0x8000201C', log)

    def test_marker_stops_as_panic_after_journaling_captured_text_and_stack_without_backing_low_memory(self):
        log = self.probe('marker')
        self.assertLess(log.index('[panic] OSPanic file='), log.index('[halt] OSPanic marker'))
        self.assertIn('file="synthetic_assert.cpp" format_text="ship assertion %s:%u"', log)
        self.assertIn('[panic] OSPanic marker', log)
        self.assertIn('entry_captured=1', log)
        self.assertIn('[halt] frame1', log)

    def test_marker_without_entry_evidence_does_not_guess_clobbered_arguments(self):
        log = self.probe('marker-late')
        self.assertIn('entry_captured=0', log)
        self.assertNotIn('[panic] OSPanic file=', log)

    def test_unrelated_marker_address_value_width_or_pc_still_fails_closed(self):
        cases = ((0x01234566, 0x01234567, 4, 0x80006D64),
                 (0x01234567, 0x01234566, 4, 0x80006D64),
                 (0x01234567, 0x01234567, 1, 0x80006D64),
                 (0x01234567, 0x01234567, 2, 0x80006D64),
                 (0x01234567, 0x01234567, 8, 0x80006D64),
                 (0x01234567, 0x01234567, 4, 0x80006C48),
                 (0x01234567, 0x01234567, 4, 0x80006D84),
                 (0xCC000008, 0x01234567, 4, 0x80006D64))
        for case in cases:
            with self.subTest(case=case):
                self.assertNotIn('[halt]', self.probe('marker-reject', *map(hex, case)))

    def test_unrelated_edge_does_not_emit_panic_or_halt_evidence(self):
        log = self.probe('unrelated-edge')
        self.assertNotIn('[panic]', log)
        self.assertNotIn('[halt]', log)

    def test_journal_failures_stop_loudly(self):
        for scenario in ('panic-trace', 'halt-trace', 'halt-frame-trace', 'marker-frame-trace'):
            with self.subTest(scenario=scenario):
                self.probe(scenario)


class GameStopReportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    run_fixture = test_io.MainIoTest.run_fixture

    def test_panic_report_has_distinct_stop_reason_and_closed_failure_record(self):
        status, report = self.run_fixture(game_stop='panic')
        self.assertEqual(status, 1)
        self.assertIn('STOP reason=game panic (OSPanic) address=01234567 pc=80006D64 width=4 value=0000000001234567', report)
        self.assertTrue(report.endswith('END frontier=FAIL live=0 rendered=0\n'))

    def test_halt_report_has_distinct_stop_reason_and_closed_failure_record(self):
        status, report = self.run_fixture(game_stop='halt')
        self.assertEqual(status, 1)
        self.assertIn('STOP reason=game halt (PPCHalt) address=8030150C pc=8030150C', report)
        self.assertTrue(report.endswith('END frontier=FAIL live=0 rendered=0\n'))
