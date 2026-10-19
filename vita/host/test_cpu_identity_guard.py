import signal
import subprocess
import unittest

import test_host


class CpuIdentityGuardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        return subprocess.run([str(test_host.OUT / 'cpu_identity_guard_probe'), scenario],
                              capture_output=True, text=True)

    def test_matching_context_passes_through_to_the_real_trampolines(self):
        for scenario, tag in (('host-call-pass', 'host_call'),
                              ('mftb-pass', 'spr_read'),
                              ('fallback-pass', 'instruction_fallback')):
            with self.subTest(scenario=scenario):
                result = self.probe(scenario)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f'[{tag}] invoked=1', result.stderr)

    def test_unadopted_guard_stays_transparent_for_a_host_owned_state(self):
        result = self.probe('host-call-unadopted')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('[host_call] invoked=1', result.stderr)

    def test_wrong_context_stops_closed_with_the_received_state_named(self):
        for scenario, service in (('host-call-wrong-ctx', 'ppc_host_call'),
                                  ('mftb-wrong-ctx', 'ppc_mftb'),
                                  ('fallback-wrong-ctx', 'ppc_fallback_instruction')):
            with self.subTest(scenario=scenario):
                result = self.probe(scenario)
                self.assertEqual(result.returncode, -signal.SIGABRT, result.stderr)
                self.assertIn(f'[cpu-identity] service={service} ctx=', result.stderr)
                self.assertIn('expected=', result.stderr)
                self.assertIn('delta=+4 ', result.stderr)
                self.assertIn('field_offset=0x', result.stderr)
                self.assertIn('pc=802CD580', result.stderr)
                self.assertIn('lr=802CD120', result.stderr)
                self.assertIn('caller=0x', result.stderr)

    def test_adopted_pass_through_is_reported_live_once_per_service(self):
        result = self.probe('host-call-twice')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('[host_call] invoked=2', result.stderr)
        self.assertEqual(result.stderr.count('[cpu-identity] live service=ppc_host_call'), 1)


if __name__ == '__main__':
    unittest.main()
