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

    def test_registered_original_CARDProbe_is_admitted_as_guest_body_without_a_card_result_or_callback_injection(self):
        self.assertIn('mode=guest-body result_injection=none storage=none', self.probe('card-original'))

    def test_CARDProbe_admission_rejects_changed_or_unregistered_original_code(self):
        for scenario in ('card-code', 'card-code-tail', 'card-unregistered', 'card-short-range'):
            with self.subTest(scenario=scenario):
                self.probe(scenario)

    def test_CARDProbe_admission_requires_the_real_guest_prologue_stack_range(self):
        for scenario in ('card-stack', 'card-stack-high'):
            with self.subTest(scenario=scenario):
                self.probe(scenario)

    def test_CARDProbe_trace_failure_stops_before_the_original_body_is_admitted(self):
        self.probe('card-trace')

    def test_repeated_CARDProbe_calls_execute_normally_without_repeating_the_initial_observation(self):
        self.assertEqual(self.probe('card-repeat').count('[card-probe]'), 1)

    def test_post_card_deferred_idle_records_the_actual_waiting_threads_at_32_presentations(self):
        log = self.probe('late-snapshot')
        self.assertEqual(log.count('[scheduler-idle]'), 2)
        self.assertIn('stage=post-card audio=deferred', log)
        self.assertEqual(log.count('thread=803A2960 state=4 suspend=0 queue=80006008'), 2)

    def test_post_card_idle_observation_remains_bounded_to_one_additional_snapshot(self):
        self.assertEqual(self.probe('late-repeat').count('stage=post-card'), 1)

    def test_post_card_idle_observation_requires_the_explicit_diagnostic_milestones(self):
        for scenario in ('late-strict', 'late-no-continuation', 'late-no-card', 'late-early'):
            with self.subTest(scenario=scenario):
                log = self.probe(scenario)
                self.assertEqual(log.count('[scheduler-idle]'), 1)
                self.assertNotIn('stage=post-card', log)

    def test_post_card_idle_observation_does_not_admit_an_unlinked_boot(self):
        self.assertNotIn('[scheduler-idle]', self.probe('late-unlinked'))

    def test_post_card_idle_trace_failure_stops_without_waking_the_guest(self):
        self.probe('late-trace')
