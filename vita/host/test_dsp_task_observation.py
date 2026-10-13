import subprocess
import unittest

import test_host


class DspTaskObservationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'scheduler_observation_probe'), scenario],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stderr

    def test_original_registered_task_entry_runs_as_guest_without_mutating_registers_memory_or_DSP_state(self):
        self.assertIn('[dsp-task-boot]', self.probe('dsp-entry'))

    def test_changed_code_or_unregistered_entry_is_not_silently_admitted(self):
        for scenario in ('dsp-code', 'dsp-unregistered'):
            with self.subTest(scenario=scenario):
                self.assertNotIn('[dsp-task-boot]', self.probe(scenario))

    def test_invalid_task_or_stack_mapping_is_refused_before_private_memory_is_read(self):
        for scenario in ('dsp-unmapped', 'dsp-stack'):
            with self.subTest(scenario=scenario):
                self.assertNotIn('[dsp-task-boot]', self.probe(scenario))

    def test_task_observation_is_bounded_and_does_not_rearm_replies_on_repeat(self):
        self.assertEqual(self.probe('dsp-repeat').count('[dsp-task-boot]'), 1)

    def test_observation_write_failure_stops_without_admitting_the_task(self):
        self.assertNotIn('[dsp-task-boot]', self.probe('dsp-trace'))

    def test_empty_task_mailbox_without_supported_due_work_stops_instead_of_manufacturing_a_reply(self):
        self.probe('dsp-empty')

    def test_actual_busy_mail_remains_readable_without_a_replacement_message(self):
        self.probe('dsp-available')

    def test_existing_verified_running_ROM_mail_source_is_not_blocked_by_task_observation(self):
        self.probe('dsp-rom-pending')

    def test_existing_verified_pending_init_work_remains_a_real_clocked_mail_source(self):
        self.probe('dsp-init-pending')

    def test_halted_pending_ROM_flag_does_not_become_a_fictitious_ready_reply(self):
        self.probe('dsp-halted-pending')
