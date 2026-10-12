import subprocess
import unittest

import test_host


class GxDrawFramingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'gx_draw_probe'), scenario], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stderr

    def test_draw_uses_prior_cp_layout_and_waits_for_the_last_vertex_byte(self):
        self.probe('draw')

    def test_primitive_uses_its_actual_vertex_format(self):
        self.probe('format')

    def test_a_later_vat_change_updates_draw_byte_length(self):
        self.probe('layout-change')

    def test_missing_layout_is_not_invented(self):
        self.probe('missing')

    def test_reserved_primitive_opcode_remains_refused(self):
        self.probe('reserved')

    def test_an_oversize_primitive_stops_before_buffer_overflow_or_publication(self):
        self.probe('oversize')

    def test_zero_vertex_draw_is_refused_without_a_fake_draw_event(self):
        self.probe('zero')

    def test_first_draw_records_the_exact_preceding_commands_and_effective_cp_layout(self):
        log = self.probe('draw')
        self.assertIn('[gx-draw-layout] index=5 opcode=90 vertices=3 format=0 stride=12 bytes=39', log)
        self.assertIn('[gx-before-draw] index=4 opcode=08 bytes=6', log)
        self.assertIn('hex=087000000009', log)
        self.assertIn('[gx-cp-before-draw] index=2 register=50 value=00000200', log)

    def test_first_draw_history_remains_bounded_and_ordered_after_wrap(self):
        log = self.probe('history-wrap')
        self.assertEqual(log.count('[gx-before-draw]'), 16)
        self.assertEqual(log.count('[gx-cp-before-draw]'), 64)
        self.assertIn('[gx-before-draw] index=89 ', log)
        self.assertIn('[gx-cp-before-draw] index=104 ', log)

    def test_first_draw_trace_failure_cannot_publish_a_completed_draw(self):
        self.probe('trace')

    def test_full_journal_keeps_the_draw_at_990_with_its_native_pinned_layout_witness(self):
        log = self.probe('journal')
        self.assertIn('[gx-command-layout] index=990 format=0 vertices=3 stride=12 bytes=39', log)
        self.assertIn('[gx-command] index=990 opcode=90 bytes=39 ', log)

    def test_layout_journal_failure_cannot_publish_a_logged_draw(self):
        log = self.probe('layout-journal-failure')
        self.assertNotIn('[gx-command] index=5 ', log)


if __name__ == '__main__':
    unittest.main()
