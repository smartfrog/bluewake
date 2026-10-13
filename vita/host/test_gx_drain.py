import subprocess
import unittest

import test_host


class GxDrainTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'gx_drain_probe'), scenario], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_consumed_native_trace_and_fifo_are_reused_without_erasing_cp_bp_xf_or_resolver_state(self):
        self.probe('state')

    def test_more_than_both_fixed_trace_and_fifo_capacities_reaches_the_actual_renderer_interface(self):
        self.probe('long-stream')

    def test_failed_draw_keeps_native_evidence_and_never_reports_a_submission(self):
        self.probe('draw-failure')

    def test_failed_present_keeps_the_copy_evidence_without_a_fake_presentation(self):
        self.probe('present-failure')

    def test_partial_command_is_not_discarded_or_submitted(self):
        self.probe('partial')

    def test_a_host_without_a_renderer_keeps_native_fifo_and_trace_evidence(self):
        self.probe('no-renderer')

    def test_complete_133260_command_prefix_is_streamed_with_reused_native_buffers(self):
        result = subprocess.run([str(test_host.OUT / 'gx_drain_probe'), 'scene-journal'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr.count('[gx-command]'), 133260)
        self.assertIn('[gx-command] index=133260 ', result.stderr)
        self.assertNotIn('[gx-command] index=133261 ', result.stderr)
