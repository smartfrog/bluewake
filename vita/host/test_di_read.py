import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_host


class DiscReadTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()
        spec = importlib.util.spec_from_file_location('ww13_synthetic', test_host.ROOT / 'scripts/vita/dvd/make_synthetic_image.py')
        cls.image = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.image)

    def run_probe(self, scenario):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='di-read-') as directory:
            root = Path(directory)
            if scenario != 'missing':
                self.image.write_image(root / 'ux0:data/bluewake/GZLE01.iso')
            subprocess.run([str(test_host.OUT / 'di_read_probe'), scenario], cwd=root, check=True)

    def test_di_read_uses_the_pinned_fst_and_real_bytes_from_the_existing_disc_seam(self):
        self.run_probe('response')

    def test_real_read_completion_uses_the_existing_ee_gated_di_irq_path(self):
        self.run_probe('irq')

    def test_missing_disc_stops_instead_of_acknowledging_an_empty_dma(self):
        self.run_probe('missing')

    def test_short_file_read_cannot_be_reported_as_a_successful_zero_filled_dma(self):
        self.run_probe('short-read')

    def test_file_failure_after_a_successful_read_propagates_and_preserves_dma_registers(self):
        self.run_probe('io-failure')

    def test_bad_command_shapes_and_aliases_stop_before_disc_dma(self):
        for scenario in ('no-dma', 'write', 'range', 'empty', 'subcommand', 'alias', 'alias-late'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_read_trace_failure_cannot_mutate_the_dma_destination(self):
        self.run_probe('trace')
