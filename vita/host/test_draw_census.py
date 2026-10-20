import subprocess
import unittest

import test_host


class DrawCensusTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.build(targets=('gx_draw_census_tests', 'vita_gl_census_tests'))

    def test_description_counting_and_formatting(self):
        subprocess.run([str(test_host.STACK / 'bin/ctest'), '--test-dir', str(test_host.OUT),
                        '-R', '^draw-census-', '--output-on-failure'], check=True)

    def summary(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'vita_gl_census_tests'), 'census-' + scenario],
                                check=True, capture_output=True, text=True)
        return [dict(field.split('=') for field in line.split()[1:])
                for line in result.stderr.splitlines() if line.startswith('[draw-census] ')]

    def test_actual_backend_binding_outcomes(self):
        cases = {
            'valid': {'tq': '1', 'tx': '1', 'invalid': '0', 'skip': '0',
                      'no_valid': '0', 'decode_failed': '0', 'bind_valid': '1', 'bind_skipped': '0'},
            'skipped': {'tx': '1', 'invalid': '1', 'skip': '1',
                        'no_valid': '1', 'decode_failed': '1', 'bind_valid': '0', 'bind_skipped': '1'},
            'untextured': {'uq': '1', 'tx': '0', 'skip': '0', 'draws': '-',
                           'no_valid': '0', 'decode_failed': '0', 'bind_valid': '0', 'bind_skipped': '1'},
            'multimap': {'tx': '1', 'invalid': '1', 'skip': '1',
                         'no_valid': '0', 'decode_failed': '1', 'bind_valid': '1', 'bind_skipped': '1'},
            'bind-failed': {'present': '0', 'partial': '1', 'bindfail': '1', 'tx': '0'},
            'upload-failed': {'present': '0', 'partial': '1', 'uploadfail': '1', 'tx': '0'},
            'sampler-failed': {'present': '0', 'partial': '1', 'bindfail': '1', 'tx': '0'},
        }
        for scenario, expected in cases.items():
            with self.subTest(scenario=scenario):
                lines = self.summary(scenario)
                self.assertEqual(len(lines), 1)
                for key, value in expected.items():
                    self.assertEqual(lines[0][key], value, key)

    def test_present_resets_frame_counts(self):
        lines = self.summary('reset')
        self.assertEqual([(line['present'], line['tx'], line['draws']) for line in lines],
                         [('1', '1', '00000000@0,0x0,0,1,1,0,0*1'), ('2', '0', '-')])

    def test_refused_plan_is_counted_without_a_draw_and_resets_at_present(self):
        lines = self.summary('refused')
        self.assertEqual([(line['present'], line['refused'], line['tx'], line['draws']) for line in lines],
                         [('1', '1', '0', '-'), ('2', '0', '0', '-')])

    def test_output_is_bounded(self):
        lines = self.summary('bound')
        self.assertEqual(len(lines), 1024)
        self.assertEqual(lines[-1]['present'], '1024')

    def test_inventory_format(self):
        line = self.summary('valid')[0]
        self.assertEqual(line['draws'], '00000000@0,0x0,0,1,1,0,0*1')

    def test_inventory_aggregates_repeated_draws(self):
        line = self.summary('repeat')[0]
        self.assertEqual(line['tx'], '12')
        self.assertEqual(line['rest'], '0')
        self.assertEqual(line['draws'], '00000000@0,0x0,0,1,1,0,0*12')

    def test_inventory_bounds_distinct_draws(self):
        line = self.summary('overflow')[0]
        self.assertEqual(line['tx'], '12')
        self.assertEqual(line['rest'], '4')
        self.assertEqual(len(line['draws'].split('|')), 8)

    def test_inventory_preserves_all_sampled_slots(self):
        line = self.summary('multimap')[0]
        self.assertEqual(line['draws'],
                         '00000000@0,0x0,0,1,1,0,0*1|00000000@3,0x0,0,0,0,0,0*1')


if __name__ == '__main__':
    unittest.main()
