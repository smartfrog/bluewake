import signal
import subprocess
import unittest

import test_host


class DispatchRingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        return subprocess.run([str(test_host.OUT / 'dispatch_ring_probe'), scenario],
                              capture_output=True, text=True)

    def test_ring_keeps_the_last_32_entries_oldest_first(self):
        result = self.probe('wrap')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('[probe] count=32', result.stdout)
        self.assertIn('[probe] first_pc=80003120 last_pc=8000319C', result.stdout)
        self.assertIn('[dispatch-ring] dump tag=wrap entries=32 capacity=32', result.stdout)
        self.assertIn('[dispatch-ring] entry=00 pc=80003120 sp=80170C40 lr=80003108', result.stdout)
        self.assertIn('[dispatch-ring] entry=31 pc=8000319C sp=80170C40 lr=80003127', result.stdout)
        self.assertEqual(result.stdout.count('[dispatch-ring] entry='), 32)

    def test_unrouted_dispatch_is_ignored_while_the_ring_is_empty(self):
        result = self.probe('routed-empty')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('[probe] count=1', result.stdout)
        self.assertEqual(result.stderr, '')

    def test_physical_alias_dispatch_counts_as_routed(self):
        result = self.probe('routed-physical-alias')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('[probe] count=2', result.stdout)
        self.assertEqual(result.stderr, '')

    def test_unrouted_dispatch_stops_closed_with_the_ring_dumped(self):
        result = self.probe('unrouted-after-entry')
        self.assertEqual(result.returncode, -signal.SIGABRT, result.stderr)
        self.assertIn('[dispatch-ring] tripwire=unresolved-pc reason=dispatch-entry entries=2',
                      result.stderr)
        self.assertIn('[dispatch-ring] offending pc=DEAD8000 sp=80170C40 lr=80003104',
                      result.stderr)
        self.assertIn('[dispatch-ring] guest pc=DEAD8000 sp=80170C40 lr=80003104', result.stderr)
        self.assertIn('[dispatch-ring] entry=00 pc=800396E0 sp=80170C40 lr=80003100', result.stderr)
        self.assertEqual(result.stderr.count('[dispatch-ring] entry='), 2)

    def test_stack_out_of_band_is_ignored_before_the_ring_settles(self):
        result = self.probe('stack-before-settled')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('[probe] count=7', result.stdout)
        self.assertEqual(result.stderr, '')

    def test_arena_boot_stack_outside_the_band_is_not_drift(self):
        result = self.probe('stack-arena-boot')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('[probe] count=32', result.stdout)
        self.assertEqual(result.stderr, '')

    def test_stack_out_of_band_stops_closed_once_settled(self):
        result = self.probe('stack-settled')
        self.assertEqual(result.returncode, -signal.SIGABRT, result.stderr)
        self.assertIn('[dispatch-ring] tripwire=stack-out-of-band reason=dispatch-entry entries=8',
                      result.stderr)
        self.assertIn('[dispatch-ring] offending pc=8000311C sp=8015FFF0 lr=80003108',
                      result.stderr)
        self.assertIn('[dispatch-ring] entry=07 pc=8000311C sp=8015FFF0 lr=80003108',
                      result.stderr)
        self.assertEqual(result.stderr.count('[dispatch-ring] entry='), 8)

    def test_leaving_the_module_stack_band_after_using_it_stops_closed(self):
        result = self.probe('stack-band-then-drift')
        self.assertEqual(result.returncode, -signal.SIGABRT, result.stderr)
        self.assertIn('[dispatch-ring] tripwire=stack-out-of-band reason=dispatch-entry entries=8',
                      result.stderr)
        self.assertIn('[dispatch-ring] offending pc=8000311C sp=8015FFF0 lr=80003107',
                      result.stderr)
        self.assertEqual(result.stderr.count('[dispatch-ring] entry='), 8)

    def test_stack_band_accepts_both_edges(self):
        result = self.probe('stack-band-edges')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('[probe] count=9', result.stdout)
        self.assertEqual(result.stderr, '')

    def test_stopped_ring_records_nothing_further(self):
        result = self.probe('stopped-silent')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('[probe] count=1', result.stdout)
        self.assertEqual(result.stderr, '')


if __name__ == '__main__':
    unittest.main()
