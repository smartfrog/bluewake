import subprocess
import unittest

import test_host

MEMBERS = {0x800D8DB8: 'daBg_Draw', 0x8015D80C: 'daSea_Draw',
           0x8015E3F0: 'daVrbox_Draw', 0x8015EA5C: 'daVrbox2_Draw'}


class DrawEdgeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output = test_host.OUT.parent / 'draw-edge-tests-on'
        test_host.StartupHostTest.build(cls.output, ('-DWW_EDGE_OBSERVATION_ADMIT=ON',), ('draw_edge_probe',))
        cls.off_output = test_host.OUT.parent / 'draw-edge-tests-off'
        test_host.StartupHostTest.build(cls.off_output, ('-DWW_EDGE_OBSERVATION_ADMIT=OFF',), ('draw_edge_probe',))

    def run_probe(self, scenario, address, output=None):
        return subprocess.run([str((output or self.output) / 'draw_edge_probe'), scenario, f'{address:08X}'],
                              check=True, capture_output=True, text=True)

    def test_owned_class_members_are_admitted_without_changing_guest_work_or_return_registers(self):
        for address, symbol in MEMBERS.items():
            with self.subTest(symbol=symbol):
                result = self.run_probe('admit', address)
                self.assertIn(f'address={address:08X} symbol={symbol} count=1', result.stderr)

    def test_class_members_require_ownership_matching_pc_and_no_overlapping_alias(self):
        for address in MEMBERS:
            for scenario in ('unowned', 'pc-mismatch', 'alias'):
                with self.subTest(address=address, scenario=scenario):
                    self.run_probe(scenario, address)

    def test_nonclass_observation_and_host_service_keep_the_original_refusal(self):
        for address in (0x800A0B60, 0x802315A8, 0x80322B20):
            with self.subTest(address=address):
                self.run_probe('refuse', address)

    def test_logging_occurs_once_for_each_distinct_edge(self):
        result = self.run_probe('log-once', 0x8015E3F0)
        lines = [line for line in result.stderr.splitlines() if line.startswith('[edge-admit]')]
        self.assertEqual(len(lines), 2)
        for address in (0x8015E3F0, 0x8015EA5C):
            self.assertTrue(any(f'address={address:08X} symbol={MEMBERS[address]} count=1' in line for line in lines))

    def test_log_failure_refuses_instead_of_silently_admitting(self):
        self.run_probe('log-failure', 0x8015E3F0)

    def test_option_off_keeps_class_members_refused_without_logging(self):
        for address in MEMBERS:
            with self.subTest(address=address):
                result = self.run_probe('admit', address, self.off_output)
                self.assertNotIn('[edge-admit]', result.stderr)


if __name__ == '__main__':
    unittest.main()
