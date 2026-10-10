import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
STACK = Path('/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack')
OUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f5/services-tests')


class StartupHostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run([str(STACK / 'bin/cmake'), '-S', str(ROOT / 'vita/host'), '-B', str(OUT),
                        '-G', 'Ninja', f'-DCMAKE_C_COMPILER={os.environ["CC"]}',
                        f'-DCMAKE_MAKE_PROGRAM={STACK}/bin/ninja',
                        f'-DRECOMPCORE_DIR={ROOT}/ref/vita-ww-01/RecompCore',
                        '-DWW05_TESTS_ONLY=ON'], check=True, capture_output=True)
        with (OUT / 'build.log').open('w') as log:
            subprocess.run([str(STACK / 'bin/cmake'), '--build', str(OUT), '-j', '2'],
                           check=True, stdout=log, stderr=subprocess.STDOUT)

    def test_unknown_mmio_stops_with_context(self):
        subprocess.run([str(OUT / 'host_probe'), 'unknown-mmio'], check=True)

    def test_runtime_pvr_is_preserved(self):
        subprocess.run([str(OUT / 'host_probe'), 'pvr'], check=True)

    def test_unknown_spr_fails(self):
        subprocess.run([str(OUT / 'host_probe'), 'unknown-spr'], check=True)

    def test_timebase_uses_shared_cycle_domain(self):
        subprocess.run([str(OUT / 'host_probe'), 'clock'], check=True)

    def test_fifo_frontier_stops_at_edge(self):
        subprocess.run([str(OUT / 'host_probe'), 'frontier'], check=True)

    def test_unknown_instruction_is_not_skipped(self):
        subprocess.run([str(OUT / 'host_probe'), 'instruction'], check=True)

    def test_unknown_mmio_write_fails(self):
        subprocess.run([str(OUT / 'host_probe'), 'unknown-write'], check=True)

    def test_invalid_module_is_rejected_before_dol_io(self):
        subprocess.run([str(OUT / 'host_probe'), 'module'], check=True)

    def test_allocation_failures_release_earlier_backings(self):
        for allocation in (1, 2, 3):
            with self.subTest(allocation=allocation):
                subprocess.run([str(OUT / 'host_probe'), f'allocation-{allocation}'], check=True)

    def test_relocated_data_and_bss_have_one_owner_and_cleanup(self):
        subprocess.run([str(OUT / 'host_probe'), 'aliases'], cwd=OUT, check=True)

    def test_alias_allocation_failure_is_propagated(self):
        subprocess.run([str(OUT / 'host_probe'), 'alias-failure'], cwd=OUT, check=True)


if __name__ == '__main__':
    unittest.main()
