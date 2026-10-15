import os
import subprocess
import unittest
from unittest.mock import patch

import test_host
import test_rel_lifecycle


OPTIONS = ('-DWW05_ENABLE_SHIP_PROLOG=ON', '-DWW05_ENABLE_TITLE_CONTRACT=ON',
           '-DWW05_ENABLE_IKADA_CONTRACT=ON', '-DWW05_ENABLE_ACTOR_BATCH_CONTRACTS=ON')


class ActorBatchConfiguration(unittest.TestCase):
    def test_each_chain_prerequisite_is_explicit(self):
        for name in ('SHIP_PROLOG', 'TITLE_CONTRACT', 'IKADA_CONTRACT'):
            with self.subTest(name=name):
                output = test_host.ROOT / 'build-f5' / ('batch-invalid-' + name.lower())
                result = subprocess.run([str(test_host.STACK / 'bin/cmake'), '-S', str(test_host.ROOT / 'vita/host'),
                    '-B', str(output), '-G', 'Ninja', '-DWW05_TESTS_ONLY=ON',
                    f'-DRECOMPCORE_DIR={test_host.ROOT}/ref/vita-ww-01/RecompCore',
                    *OPTIONS, f'-DWW05_ENABLE_{name}=OFF'], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Actor batch admission requires explicit ship, title and ikada options', result.stderr)


class ActorBatchAdmission(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output = test_host.OUT.parent / 'actor-batch-enabled'
        test_host.StartupHostTest.build(cls.output, OPTIONS, ('rel_lifecycle_probe', 'io_probe'))
        test_rel_lifecycle.RelLifecycleTest.setUpClass()

    def run_probe(self, scenario):
        with patch.object(test_host, 'OUT', self.output):
            return test_rel_lifecycle.RelLifecycleTest.run_probe(self, 'batch-' + scenario)

    def test_exact_batch_chain_initializes_two_new_synthetic_actors(self):
        log = self.run_probe('enabled')
        for mid in (84, 85):
            self.assertIn(f'[rel-sdk] module={mid} stage=verified', log)
            self.assertIn(f'[rel-prolog] module={mid} stage=caller-returned', log)

    def test_atomic_activation_refuses_each_bad_context(self):
        for field in ('missing-ship', 'missing-title', 'missing-ikada', 'missing-set', 'missing-member',
                      'extra-member', 'missing-retained-member', 'duplicate', 'bad-pin', 'bad-set', 'bad-dol', 'prior-image',
                      'receipt-member', 'receipt-final', 'repeated', 'invalid-certificate', 'wrong-chunk',
                      'pin-60', 'pin-84', 'pin-85', 'pin-168', 'pin-400'):
            with self.subTest(field=field): self.run_probe(field)

    def test_registered_batch_without_activation_remains_denied(self):
        self.run_probe('off')

    def test_startup_activation_precedes_dispatch(self):
        import test_io
        with patch.dict(os.environ, {'WW05_IO_IKADA': 'valid'}):
            status, report, log = test_io.MainIoTest().run_fixture(output=self.output, return_log=True)
        self.assertEqual(status, 0, log)
        self.assertLess(log.index('[rel-batch-admission]'), log.index('[dispatch]'))

    def test_startup_batch_mismatch_stops_before_dispatch(self):
        import test_io
        for mode in ('batch-missing-member', 'batch-bad-pin', 'batch-bad-set'):
            with self.subTest(mode=mode), patch.dict(os.environ, {'WW05_IO_IKADA': mode}):
                status, report, log = test_io.MainIoTest().run_fixture(output=self.output, return_log=True)
                self.assertEqual(status, 1, log)
                self.assertIn('edges=0 exception=00000001 cycles=0', report)
                self.assertNotIn('[dispatch]', log)

    def test_startup_receipt_failures_stop_before_dispatch(self):
        import test_io
        for failure in ('batch-member-log', 'batch-admission-log'):
            with self.subTest(failure=failure), patch.dict(os.environ, {'WW05_IO_IKADA': 'valid'}):
                status, report, log = test_io.MainIoTest().run_fixture(output=self.output, return_log=True, failure=failure)
                self.assertEqual(status, 1, log)
                self.assertNotIn('[dispatch]', log)

    def test_wood_zero_bss_sdk_and_lifecycle_mutations_refuse(self):
        for field in ('sdk-nonzero-zero-bss', 'sdk-wrong-argument', 'sdk-frozen-bss', 'sdk-untouched-byte',
                      'sdk-previous', 'sdk-neighbor-write', 'sdk-prepared-trace', 'sdk-verified-trace',
                      'copied-certificate', 'policy-disabled', 'entry-lr', 'entry-r3', 'entry-r31',
                      'epilog', 'unresolved', 'unlink', 'sdk-allocation'):
            with self.subTest(field=field): self.run_probe(field)

    def test_wood_materialization_failure_does_not_publish_a_slot(self):
        for field in ('raw-identity', 'materialize-allocation', 'materialize-trace'):
            with self.subTest(field=field): self.run_probe(field)


class ActorBatchOff(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output = test_host.OUT.parent / 'actor-batch-disabled'
        test_host.StartupHostTest.build(cls.output, (*OPTIONS[:-1], '-DWW05_ENABLE_ACTOR_BATCH_CONTRACTS=OFF'),
                                      ('rel_lifecycle_probe',))
        test_rel_lifecycle.RelLifecycleTest.setUpClass()

    def test_extra_registered_actors_remain_inert_with_batch_compiled_out(self):
        with patch.object(test_host, 'OUT', self.output):
            test_rel_lifecycle.RelLifecycleTest.run_probe(self, 'batch-off')


if __name__ == '__main__':
    unittest.main()
