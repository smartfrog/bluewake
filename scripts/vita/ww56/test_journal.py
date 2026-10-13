"""Tests for the incremental journal tail.

The watcher reads whole lines from growing logs without re-reading the past
and without trusting a replaced or truncated file: a broken stream is not a
measurement.
"""

from pathlib import Path
import tempfile
import unittest

from journal import IncrementalLog, count_marker_lines


class CountMarkerTests(unittest.TestCase):
    def test_counts_lines_containing_the_marker(self):
        text = '[frame-present] frame=1 us=1\nnoise\n[frame-present] frame=2 us=2\n'
        self.assertEqual(count_marker_lines(text, '[frame-present]'), 2)
        self.assertEqual(count_marker_lines(text, '[rel-frontier]'), 0)


class IncrementalLogTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.path = Path(self.temporary.name) / 'ww05.log'

    def tearDown(self):
        self.temporary.cleanup()

    def test_reads_only_new_complete_lines(self):
        log = IncrementalLog(self.path)
        self.assertEqual(log.read(), ([], 0))
        self.path.write_text('[progress] turns=1\n[progress] turns=2\npartial')
        lines, count = log.read()
        self.assertEqual(lines, ['[progress] turns=1', '[progress] turns=2'])
        self.assertEqual(count, len(self.path.read_bytes()))
        with self.path.open('a') as stream:
            stream.write(' line\n[rel-frontier] name=x\n')
        lines, count = log.read()
        self.assertEqual(lines, ['partial line', '[rel-frontier] name=x'])
        self.assertGreater(count, 0)

    def test_missing_file_is_not_an_error(self):
        self.assertEqual(IncrementalLog(self.path).read(), ([], 0))

    def test_truncation_is_refused(self):
        log = IncrementalLog(self.path)
        self.path.write_text('first\nsecond\n')
        log.read()
        self.path.write_text('shorter\n')
        with self.assertRaisesRegex(ValueError, 'replaced or truncated'):
            log.read()

    def test_replacement_is_refused(self):
        log = IncrementalLog(self.path)
        self.path.write_text('original\n')
        log.read()
        replacement = Path(self.temporary.name) / 'replacement.log'
        replacement.write_text('other content that is at least as long\n')
        replacement.replace(self.path)
        with self.assertRaisesRegex(ValueError, 'replaced or truncated'):
            log.read()


if __name__ == '__main__':
    unittest.main()
