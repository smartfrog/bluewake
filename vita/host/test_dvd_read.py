from pathlib import Path
import subprocess
import tempfile
import unittest

import test_di_read
import test_host


class DvdReadTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_di_read.DiscReadTest.setUpClass()

    def run_probe(self, scenario):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='dvd-read-') as directory:
            root = Path(directory)
            if scenario != 'missing-disc':
                test_di_read.DiscReadTest.image.write_image(root / 'ux0:data/bluewake/GZLE01.iso')
            subprocess.run([str(test_host.OUT / 'dvd_read_probe'), scenario], cwd=root, check=True)

    def test_sync_read_copies_real_bytes_and_publishes_the_actual_completed_length(self):
        self.run_probe('read')

    def test_sourced_sdk_tail_padding_reads_real_disc_bytes(self):
        self.run_probe('padding')

    def test_async_without_callback_copies_real_bytes_and_returns_queued_success(self):
        for scenario in ('async', 'async-padding'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_async_callback_is_refused_before_copy_or_completion(self):
        self.run_probe('async-callback')

    def test_owned_async_callback_is_queued_after_real_copy_without_early_completion(self):
        self.run_probe('async-owned')

    def test_missing_disc_never_becomes_a_successful_sync_read(self):
        self.run_probe('missing-disc')

    def test_short_read_cannot_publish_a_successful_zero_filled_transfer(self):
        self.run_probe('short-read')

    def test_invalid_ranges_or_aliases_are_refused_before_copy_or_completion(self):
        for scenario in ('bad-file-info', 'bad-destination', 'alias', 'bad-offset', 'bad-length', 'signed-range'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_trace_failure_stops_before_copy_or_file_info_completion(self):
        self.run_probe('trace')


if __name__ == '__main__':
    unittest.main()
