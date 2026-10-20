import subprocess
import unittest

import test_host


class DrawCensusTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.build(targets=('gx_draw_census_tests', 'vita_gl_census_tests'))

    def test_counting_and_detection(self):
        subprocess.run([str(test_host.STACK / 'bin/ctest'), '--test-dir', str(test_host.OUT),
                        '-R', '^draw-census-', '--output-on-failure'], check=True)

    def summary(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'vita_gl_census_tests'), 'census-' + scenario],
                                check=True, capture_output=True, text=True)
        return [dict(field.split('=') for field in line.split()[1:])
                for line in result.stderr.splitlines() if line.startswith('[draw-census] ')]

    def test_actual_backend_binding_outcomes(self):
        cases = {
            'valid': {'tq': '1', 'logo': '1', 'logo_valid': '1', 'bind_valid': '1', 'required_skipped': '0'},
            'skipped': {'logo': '1', 'logo_valid': '0', 'logo_skipped': '1', 'decode_failed': '1', 'no_valid': '1'},
            'untextured': {'uq': '1', 'bind_skipped': '1', 'required_skipped': '0', 'no_valid': '0'},
            'multimap': {'logo': '1', 'logo_valid': '0', 'logo_skipped': '1', 'bind_valid': '1', 'incomplete': '1', 'no_valid': '0'},
            'bind-failed': {'present': '0', 'partial': '1', 'bind_failed': '1', 'logo': '0'},
            'upload-failed': {'present': '0', 'partial': '1', 'upload_failed': '1', 'logo': '0'},
            'sampler-failed': {'present': '0', 'partial': '1', 'bind_failed': '1', 'logo': '0'},
        }
        for scenario, expected in cases.items():
            with self.subTest(scenario=scenario):
                lines = self.summary(scenario)
                self.assertEqual(len(lines), 1)
                for key, value in expected.items():
                    self.assertEqual(lines[0][key], value, key)

    def test_present_resets_frame_counts(self):
        lines = self.summary('reset')
        self.assertEqual([(line['present'], line['logo']) for line in lines], [('1', '1'), ('2', '0')])

    def test_output_is_bounded(self):
        lines = self.summary('bound')
        self.assertEqual(len(lines), 1024)
        self.assertEqual(lines[-1]['present'], '1024')


if __name__ == '__main__':
    unittest.main()
