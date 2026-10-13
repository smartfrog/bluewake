from contextlib import redirect_stdout
import io
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import bridge


class BridgeVerdictTests(unittest.TestCase):
    def test_normalized_post_fifo_never_reports_plain_ran(self):
        report = ('bridge ok=1 plans_skipped=1 programs_refused=0 copies_refused=0 programs=0\n'
                  'skip post-normalized texture matrix not supported\ngap unsupported_texgen=1\n')
        output = io.StringIO()
        with patch.object(bridge, 'run_stream', return_value=(report, None)), redirect_stdout(output):
            status = bridge.run_capture('fifo', b'fixture', Path('capture'), Path('binary'), None)
        self.assertEqual(status, 1)
        self.assertIn('verdict=ran-with-coverage-gaps\n', output.getvalue())
        self.assertNotIn('verdict=ran\n', output.getvalue())

    def test_normalized_post_report_never_reports_plain_ran(self):
        report = 'bridge ok=1 plans_skipped=1 programs_refused=0 copies_refused=0 programs=0\n'
        output = io.StringIO()
        with patch.object(bridge, 'run_stream', return_value=(report, None)), redirect_stdout(output):
            status = bridge.run_report(SimpleNamespace(complete=True, data=b'fixture'), Path('binary'), None)
        self.assertEqual(status, 1)
        self.assertNotIn('verdict=ran\n', output.getvalue())

    def test_legal_fifo_keeps_plain_ran(self):
        report = 'bridge ok=1 plans_skipped=0 programs_refused=0 copies_refused=0 programs=1\n'
        output = io.StringIO()
        with patch.object(bridge, 'run_stream', return_value=(report, None)), redirect_stdout(output):
            status = bridge.run_capture('fifo', b'fixture', Path('capture'), Path('binary'), None)
        self.assertEqual(status, 0)
        self.assertTrue(output.getvalue().endswith('verdict=ran\n'))


if __name__ == '__main__':
    unittest.main()
