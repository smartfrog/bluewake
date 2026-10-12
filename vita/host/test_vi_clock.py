import subprocess
import unittest

import test_host


class ViClockTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'vi_clock_probe'), scenario],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_vi_clock_is_off_by_default(self):
        self.probe('off')

    def test_opt_in_clock_asserts_only_at_the_sourced_guest_cycle_threshold(self):
        self.probe('threshold')

    def test_next_retrace_bounds_the_remaining_dispatch_without_timebase_credit(self):
        self.probe('deadline')

    def test_vi_mask_defers_the_latched_device_interrupt(self):
        self.probe('masked')

    def test_pi_mask_defers_an_enabled_vi_interrupt(self):
        self.probe('pi-masked')

    def test_msr_ee_defers_delivery_to_the_existing_external_vector(self):
        self.probe('ee')

    def test_a_prior_guest_exception_is_not_overwritten_by_vi_delivery(self):
        self.probe('exception')

    def test_the_real_vi_acknowledgement_precedes_a_later_retrace(self):
        self.probe('ack')

    def test_elapsed_periods_are_counted_without_writing_guest_messages_or_frames(self):
        self.probe('multiple')

    def test_zero_charge_edges_do_not_create_retraces(self):
        self.probe('zero')

    def test_timebase_writes_cannot_invent_a_retrace(self):
        self.probe('timebase-write')

    def test_failed_vi_observation_stops_loudly(self):
        self.probe('trace')


if __name__ == '__main__':
    unittest.main()
