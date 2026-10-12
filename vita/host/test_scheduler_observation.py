import subprocess
import unittest

import test_host


class SchedulerObservationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'scheduler_observation_probe'), scenario],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stderr

    def test_post_link_idle_snapshot_names_the_actual_waiting_threads_without_mutating_them(self):
        log = self.probe('snapshot')
        self.assertIn('[scheduler-idle] run_bits=00000000', log)
        self.assertIn('thread=803A2960 state=4 suspend=0 queue=80006008', log)
        self.assertIn('sp=80007000 lr=80001234', log)

    def test_only_one_snapshot_is_emitted_for_the_observed_idle_episode(self):
        log = self.probe('repeat')
        self.assertEqual(log.count('[scheduler-idle]'), 1)

    def test_an_unlinked_boot_does_not_emit_a_post_link_snapshot(self):
        self.assertNotIn('[scheduler-idle]', self.probe('unlinked'))

    def test_log_failure_stops_loudly_without_waking_the_guest(self):
        self.probe('trace')
