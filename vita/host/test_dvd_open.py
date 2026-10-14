from pathlib import Path
import subprocess
import tempfile
import unittest

import test_di_read
import test_host


class DvdOpenTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_di_read.DiscReadTest.setUpClass()

    def run_probe(self, scenario):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='dvd-open-') as directory:
            root = Path(directory)
            if scenario not in ('missing-disc', 'archive-missing-disc'):
                test_di_read.DiscReadTest.image.write_image(root / 'ux0:data/bluewake/GZLE01.iso')
            return subprocess.run([str(test_host.OUT / 'dvd_open_probe'), scenario], cwd=root, check=True,
                                  capture_output=scenario.startswith('rel-'), text=True)

    def test_real_path_opens_through_the_pinned_fst_and_returns_to_the_guest(self):
        self.run_probe('open')

    def test_fast_open_uses_the_same_pinned_file_info_contract(self):
        self.run_probe('fast')

    def test_path_to_entry_uses_the_existing_fst(self):
        self.run_probe('entry')

    def test_missing_file_returns_the_real_not_found_result(self):
        self.run_probe('not-found')

    def test_missing_disc_stops_without_claiming_a_successful_open(self):
        self.run_probe('missing-disc')

    def test_invalid_guest_inputs_and_unowned_edges_are_refused(self):
        for scenario in ('bad-path', 'unterminated', 'bad-file-info', 'alias', 'unowned', 'bad-return'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_open_trace_failure_stops_before_mutating_file_info_or_returning_to_guest(self):
        self.run_probe('trace')

    def test_another_owners_disc_is_not_borrowed_or_closed(self):
        self.run_probe('borrowed')

    def test_unimplemented_rel_materialization_logs_its_name_before_stopping(self):
        result = self.run_probe('rel-frontier')
        self.assertIn('[rel-frontier] name=synthetic_module', result.stderr)

    def test_invalid_rel_frontier_inputs_stop_without_accessing_the_disc(self):
        for scenario in ('rel-bad-object', 'rel-bad-name', 'rel-unterminated'):
            with self.subTest(scenario=scenario):
                result = self.run_probe(scenario)
                self.assertNotIn('[rel-frontier]', result.stderr)

    def test_rel_frontier_trace_failure_is_reported_without_modifying_the_guest(self):
        self.run_probe('rel-trace')

    def test_archive_path_resolves_a_present_file_to_the_signed_entry(self):
        self.run_probe('archive-found')

    def test_archive_path_returns_the_signed_absent_result(self):
        self.run_probe('archive-absent')

    def test_archive_path_refuses_invalid_guest_strings(self):
        for scenario in ('archive-bad-path', 'archive-null', 'archive-unterminated'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_archive_path_requires_the_owned_disc_image(self):
        self.run_probe('archive-missing-disc')

    def test_another_owners_disc_is_not_borrowed_for_archive_path(self):
        self.run_probe('archive-borrowed')

    def test_archive_path_requires_a_module_owned_address(self):
        self.run_probe('archive-unowned')

    def test_archive_path_refuses_an_unaligned_return(self):
        self.run_probe('archive-bad-return')

    def test_archive_path_does_not_treat_r4_as_file_info(self):
        self.run_probe('archive-r4')

    def test_archive_path_trace_failure_stops_before_mutating_the_guest(self):
        self.run_probe('archive-trace')

    def test_unrelated_desktop_edges_are_still_refused(self):
        self.run_probe('archive-unrelated')


if __name__ == '__main__':
    unittest.main()
