import subprocess
import unittest

import test_host

REGISTERS = (0xCC001000, 0xCC001002, 0xCC001004, 0xCC001006, 0xCC001008)


class PeConfigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def run_probe(self, scenario, address):
        subprocess.run([str(test_host.OUT / 'retained_register_probe'), scenario, hex(address)], check=True)

    def test_direct_pe_config_retains_the_sourced_halfword(self):
        for address in REGISTERS:
            for value in (0, 1, 0xABCD, 0xF357, 65535):
                with self.subTest(address=address, value=value):
                    self.run_probe(f'value-{value}', address)

    def test_pe_config_storage_is_independent(self):
        for address in REGISTERS:
            with self.subTest(address=address):
                self.run_probe('independent', address)

    def test_pe_configuration_does_not_consume_fifo_or_acknowledge_interrupts(self):
        for address in REGISTERS:
            with self.subTest(address=address):
                self.run_probe('active', address)

    def test_pe_config_rejects_unsourced_read_widths(self):
        for address in REGISTERS:
            for width in (1, 4, 8):
                with self.subTest(address=address, width=width):
                    self.run_probe(f'read-width-{width}', address)

    def test_pe_config_rejects_unsourced_write_widths(self):
        for address in REGISTERS:
            for width in (1, 4, 8):
                with self.subTest(address=address, width=width):
                    self.run_probe(f'write-width-{width}', address)

    def test_pe_configuration_credits_cycles_without_renewing_a_spent_quantum(self):
        for address in REGISTERS:
            with self.subTest(address=address):
                self.run_probe('credit', address)


if __name__ == '__main__':
    unittest.main()
