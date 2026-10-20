import json
import shlex
import subprocess
import unittest
from pathlib import Path

import test_host

SCENARIOS = ('retrace-unowned', 'draw-done-mismatch', 'restore-unowned', 'no-writer', 'delta',
             'unmapped-queue', 'alias-queue', 'thread-valid', 'thread-unmapped',
             'failed-write', 'short-write', 'short-complete', 'second-call', 'compiled-no-record',
             'bounded', 'vrbox2-no-record', 'vrbox-no-record')
REFUSAL_SCENARIOS = ('epilogue-edge-unowned', 'epilogue-edge-context-unowned', 'epilogue-edge-retrace-unowned',
                     'epilogue-edge-retrace-bad-caller', 'epilogue-edge-retrace-bad-queue',
                     'epilogue-edge-retrace-bad-sda', 'epilogue-edge-retrace-alias',
                     'epilogue-edge-compiled', 'epilogue-edge-retrace')


class RejectionCapsuleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()
        cls.output = test_host.OUT.parent / 'rejection-capsule-tests'
        test_host.StartupHostTest.build(cls.output, ('-DWW05_REJECTION_CAPSULE=ON',
                                                   '-DWW_EDGE_OBSERVATION_ADMIT=ON'),
                                        ('rejection_capsule_probe', 'host_probe'))

    def test_bounded_capsule_scenarios(self):
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.output / 'rejection_capsule_probe'), scenario], check=True)

    def test_existing_refusals_keep_their_decision_with_the_option(self):
        for scenario in REFUSAL_SCENARIOS:
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.output / 'host_probe'), scenario], cwd=self.output, check=True)

    def test_vrbox_draw_boundary_contracts_with_admission_enabled(self):
        for actor in ('vrbox', 'vrbox2'):
            for shape in ('owned', 'unowned', 'alias', 'mismatch', 'unsupported', 'sea', 'background'):
                with self.subTest(actor=actor, shape=shape):
                    subprocess.run([str(self.output / 'host_probe'), f'{actor}-edge-{shape}'], check=True)

    def test_default_build_contains_no_capsule_code(self):
        root = test_host.ROOT
        output = test_host.OUT.parent / 'rejection-capsule-off-proof'
        output.mkdir(parents=True, exist_ok=True)
        commands = json.loads((test_host.OUT / 'compile_commands.json').read_text())
        row = next(row for row in commands if row['file'].endswith('/boot_host.c'))
        arguments = shlex.split(row['command'])
        arguments = [value for value in arguments if value != '-c']
        index = arguments.index('-o')
        del arguments[index:index + 2]
        arguments.remove(row['file'])
        arguments.extend(['-I' + str(root / 'vita/host'), '-g0', '-frandom-seed=ww117'])
        baseline = output / 'baseline' / 'boot_host.c'
        baseline.parent.mkdir(exist_ok=True)
        current = Path(row['file']).read_text()
        without_capsule = test_host.without_option_regions(current, ('WW05_REJECTION_CAPSULE',))
        self.assertNotIn('WW05_REJECTION_CAPSULE', without_capsule)
        self.assertNotEqual(current, without_capsule)
        baseline.write_text(without_capsule)
        for label, source in (('baseline', baseline), ('off', Path(row['file']))):
            for suffix, mode in (('i', ['-E', '-P']), ('o', ['-c'])):
                subprocess.run([*arguments, *mode, str(source), '-o', str(output / f'{label}-boot_host.c.{suffix}')],
                               cwd=row['directory'], check=True)
        self.assertEqual((output / 'baseline-boot_host.c.i').read_bytes(),
                         (output / 'off-boot_host.c.i').read_bytes())
        self.assertEqual((output / 'baseline-boot_host.c.o').read_bytes(),
                         (output / 'off-boot_host.c.o').read_bytes())
        self.assertIn('WW05_REJECTION_CAPSULE:BOOL=OFF', (test_host.OUT / 'CMakeCache.txt').read_text())
        self.assertIn('WW05_REJECTION_CAPSULE:BOOL=ON', (self.output / 'CMakeCache.txt').read_text())
        self.assertNotIn('rejection_capsule', (test_host.OUT / 'build.ninja').read_text())
        symbols = subprocess.check_output(['nm', str(test_host.OUT / 'libww05_services.a')], text=True)
        for symbol in ('ww_host_rejection_capsule', 'ww_rejection_capsule_init', 'ww_rejection_capsule_install_file'):
            self.assertNotIn(symbol, symbols)
        self.assertNotIn(b'rejection-capsule.bin', (test_host.OUT / 'libww05_services.a').read_bytes())


if __name__ == '__main__':
    unittest.main()
