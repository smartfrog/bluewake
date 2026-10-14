import unittest

import test_di_read
import test_ship_prolog


POST_LINK_SITES = ('resolver', 'icbi', 'ownership', 'ctor-start', 'ctor-resume')


class RelSectionGuardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_di_read.DiscReadTest.setUpClass()

    def check_form(self, form, accepted, sites=POST_LINK_SITES):
        for site in sites:
            with self.subTest(site=site, form=form):
                log = test_ship_prolog.ShipPrologTest().run_probe(f'sections-{site}:{form}')
                self.assertIn(f'PASS sections site={site} form={form} {"accepted" if accepted else "refused"}', log)
                if site in ('resolver', 'icbi'):
                    self.assertEqual('[rel-section-mismatch]' in log, not accepted, log)

    def test_pristine_passes_at_link_and_post_link_sites(self):
        self.check_form('pristine', True, ('link',) + POST_LINK_SITES)

    def test_exact_relocated_form_passes_at_post_link_sites(self):
        self.check_form('relocated', True)

    def test_link_time_rejects_the_exact_relocated_form(self):
        self.check_form('relocated', False, ('link',))

    def test_relocated_bss_accepts_zero_and_aligned_main_ram_boundaries(self):
        for form in ('bss-zero', 'bss-low', 'bss-last'):
            self.check_form(form, True)

    def test_relocated_bss_rejects_outside_main_ram_and_misalignment(self):
        for form in ('bss-below', 'bss-end', 'bss-ship', 'bss-tagged', 'bss-misaligned'):
            self.check_form(form, False)

    def test_empty_zero_offset_entry_rejects_an_aligned_main_ram_address(self):
        self.check_form('empty', False)

    def test_tampered_size_fails_closed(self):
        self.check_form('size', False)

    def test_tampered_count_fails_closed(self):
        self.check_form('count', False)

    def test_inexact_relocated_entry_fails_closed(self):
        self.check_form('offset', False)

    def test_flipped_executable_bit_fails_closed(self):
        self.check_form('exec', False)

    def test_inexact_relocated_table_pointer_fails_closed(self):
        self.check_form('table', False)

    def test_mixed_pristine_and_relocated_forms_fail_closed(self):
        for form in ('mixed-pristine-table', 'mixed-relocated-table'):
            self.check_form(form, False)

    def test_constructor_whole_image_hash_remains_fail_closed(self):
        self.check_form('raw-hash', False, ('ctor-start', 'ctor-resume'))


if __name__ == '__main__':
    unittest.main()
