from pathlib import Path
import subprocess
import tempfile
import unittest

import test_di_read
import test_host


class DvdCallbackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_di_read.DiscReadTest.setUpClass()

    def run_probe(self, scenario):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='dvd-callback-') as directory:
            root = Path(directory)
            test_di_read.DiscReadTest.image.write_image(root / 'ux0:data/bluewake/GZLE01.iso')
            subprocess.run([str(test_host.OUT / 'dvd_callback_probe'), scenario], cwd=root, check=True)

    def test_real_copied_bytes_complete_only_after_the_deadline_with_one_guest_callback(self):
        self.run_probe('deliver')

    def test_callback_waits_until_ee_is_enabled(self):
        self.run_probe('masked')

    def test_callback_waits_out_a_partial_scheduler_context(self):
        self.run_probe('scheduler')

    def test_full_queue_refuses_the_read_before_copying(self):
        self.run_probe('full')

    def test_callback_deadline_overflow_is_refused_before_copying(self):
        self.run_probe('overflow')

    def test_unmapped_callback_stack_stops_before_guest_callback_execution(self):
        self.run_probe('stack')

    def test_completion_log_failure_cannot_execute_an_unrecorded_callback(self):
        self.run_probe('trace')

    def test_incomplete_callback_is_a_loud_failure_not_a_successful_delivery(self):
        self.run_probe('incomplete')


if __name__ == '__main__':
    unittest.main()
