import os
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
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
            self.assertIn(f'[rel-actor-block] stage=exposed module={mid}', log)
            self.assertRegex(log, rf'\[rel-sdk\] module={mid} stage=prepared .*fixed=1 input_form=2 ')

    def test_atomic_activation_refuses_each_bad_context(self):
        for field in ('missing-ship', 'missing-title', 'missing-ikada', 'missing-set', 'missing-member',
                      'extra-member', 'missing-retained-member', 'duplicate', 'bad-pin', 'bad-set', 'bad-dol', 'prior-image',
                      'receipt-member', 'receipt-final', 'repeated', 'invalid-certificate', 'wrong-chunk',
                      'pin-60', 'pin-84', 'pin-85', 'pin-168', 'pin-400', 'bookkeeping-allocation', 'receipt-flush'):
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

    def test_inline_policy_and_observed_sequence_cannot_be_transplanted_or_skipped(self):
        for field in ('unknown-form', 'title-form', 'title-transplant', 'ship-transplant', 'profile-transplant',
                      'direct-prepare', 'direct-finish', 'unprepared-finish', 'missing-query', 'normal',
                      'fallback-a', 'fallback-b', 'zero-fallback', 'r3', 'r31', 'interior', 'mirror', 'one-past',
                      'conflicting-owner', 'owner-bss', 'publication', 'name-overflow', 'name-unbounded',
                      'exception', 'result', 'frozen-form', 'frozen-unknown', 'frozen-title',
                      'skip-shrink', 'resize', 'skip-accounting', 'return-replay', 'policy-identity',
                      'missing-exposure', 'old-actor-input', 'old-actor-byte', 'query-trace', 'shrink-trace',
                      'accounting-trace', 'expose-trace', 'refusal-trace', 'query-flush', 'prepare-flush',
                      'verify-flush', 'shrink-flush', 'accounting-flush', 'expose-flush'):
            with self.subTest(field=field): self.run_probe('inline-' + field)

    def test_bound_records_route_refusals_even_after_policy_or_certificate_loss(self):
        for field in ('disabled-wrong-r3', 'copied-wrong-r3', 'copied-direct', 'wrong-module', 'ambiguous-records'):
            with self.subTest(field=field): self.run_probe('inline-' + field)

    def test_inline_boundary_routes_duplicates_and_repeat_publication(self):
        for route in ('host', 'edge', 'dispatch'):
            with self.subTest(route=route): self.run_probe('inline-positive-' + route)

    def test_inline_immutable_layout_is_qualified_before_activation(self):
        for field in ('version', 'header-bss', 'section-bss', 'fix-small', 'fix-overflow',
                      'extent-at-bytes', 'metadata-tail', 'import-tail', 'section-tail'):
            with self.subTest(field=field): self.run_probe('layout-' + field)

    def test_source_policy_rejects_unknown_inconsistent_and_unpinned_classifications(self):
        source = test_host.ROOT / 'vita/host'
        policy = (source / 'actor_batch_policy.h').read_text()
        mutations = (
            ('unknown', 'true, WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO}', 'true, (WwActorSdkContract)99}'),
            ('inconsistent', 'true, WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO}', 'false, WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO}'),
            ('changed-contract', 'true, WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO}', 'true, WW_ACTOR_SDK_OWNER_PUBLISHED}'),
            ('identity', '0xecb7de83dc2b9e50ull', '0xecb7de83dc2b9e51ull'))
        commands = json.loads((self.output / 'compile_commands.json').read_text())
        row = next(row for row in commands if row['file'].endswith('/rel_contract.c'))
        link = subprocess.check_output([str(test_host.STACK / 'bin/ninja'), '-t', 'commands', 'rel_lifecycle_probe'],
                                       cwd=self.output, text=True).splitlines()[-1]
        for name, old, new in mutations:
            with self.subTest(name=name), tempfile.TemporaryDirectory(dir=self.output, prefix='policy-') as directory:
                root = Path(directory)
                for path in self.output.iterdir():
                    if path.is_file() and path.suffix in ('.rel', '.inc'):
                        (root / path.name).symlink_to(path)
                (root / 'actor_batch_policy.h').write_text(policy.replace(old, new, 1))
                (root / 'rel_contract.c').write_bytes((source / 'rel_contract.c').read_bytes())
                args = shlex.split(row['command'])
                args[args.index(row['file'])] = str(root / 'rel_contract.c')
                obj = root / 'rel_contract.o'
                args[args.index('-o') + 1] = str(obj)
                for flag in ('-MF', '-MT'):
                    if flag in args:
                        index = args.index(flag)
                        del args[index:index + 2]
                subprocess.run(args, cwd=self.output, check=True, capture_output=True)
                args = shlex.split(link.removeprefix(': && ').removesuffix(' && :'))
                args = [arg for arg in args if '--dependency-file=' not in arg]
                args[args.index('-o') + 1] = str(root / 'rel_lifecycle_probe')
                subprocess.run([args[0], str(obj), *args[1:]], cwd=self.output, check=True, capture_output=True)
                with patch.object(test_host, 'OUT', root):
                    test_rel_lifecycle.RelLifecycleTest.run_probe(self, 'batch-policy-invalid')


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
