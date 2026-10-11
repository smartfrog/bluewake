import subprocess
import unittest

import test_host


class DiInquiryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        subprocess.run([str(test_host.OUT / 'di_inquiry_probe'), scenario], check=True)

    def test_inquiry_copies_only_the_three_sourced_words_without_adding_clock_credit(self):
        self.probe('response')

    def test_inquiry_completion_uses_the_existing_masked_ee_gate_and_write_one_to_clear(self):
        self.probe('irq')

    def test_inquiry_refuses_unsupported_dma_shapes_before_changing_guest_memory(self):
        for shape in ('no-dma', 'write', 'empty', 'long', 'range', 'alias', 'subcommand'):
            with self.subTest(shape=shape):
                self.probe(f'reject-{shape}')


if __name__ == '__main__':
    unittest.main()
