from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import test_di_read
import test_host
from test_rel_contract import archive


class RelLifecycleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_di_read.DiscReadTest.setUpClass()

    def run_probe(self, scenario):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='rel-lifecycle-') as directory:
            root = Path(directory)
            (root / 'synthetic.dol').write_bytes(bytes([1, 2, 3, 4]))
            image = bytearray(test_di_read.DiscReadTest.image.build_image())
            content = archive('catalog-id' if scenario == 'title-catalog-id' else 'success',
                              (test_host.OUT / 'lifecycle_title.rel').read_bytes(),
                              resident='small' if scenario in ('title-enabled', 'title-copy', 'title-catalog-id') else False)
            fst = test_di_read.DiscReadTest.image.FST_OFFSET
            image[fst + 5 * 12 + 1:fst + 5 * 12 + 9] = b'RELS.arc'
            struct.pack_into('>II', image, fst + 16, len(image), len(content))
            image.extend(content)
            disc = root / 'ux0:data/bluewake/GZLE01.iso'
            disc.parent.mkdir(parents=True)
            disc.write_bytes(image)
            result = subprocess.run([str(test_host.OUT / 'rel_lifecycle_probe'), scenario],
                                    cwd=root, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_translated_ship_then_title_empty_walker_preserves_all_three_owners(self):
        self.run_probe('success')

    def test_legacy_ship_policy_uses_the_shared_engine(self):
        self.run_probe('legacy')

    def test_registered_metadata_and_ship_policy_do_not_admit_title(self):
        for policy in ('registered-only', 'ship-only'):
            with self.subTest(policy=policy):
                self.run_probe(policy)

    def test_real_zero_budget_entry_and_return_yields_are_idempotent(self):
        self.run_probe('observations')

    def test_entry_context_is_exact(self):
        for field in ('pc', 'lr', 'r3', 'r31'):
            with self.subTest(field=field):
                self.run_probe('entry-' + field)

    def test_duplicate_entry_requires_the_complete_unchanged_frame(self):
        for field in ('lr', 'r1', 'r2', 'r13', 'r3', 'r31'):
            with self.subTest(field=field):
                self.run_probe('duplicate-' + field)

    def test_entry_after_observed_progress_is_denied(self):
        self.run_probe('entry-after-progress')

    def test_entry_after_translated_walker_completion_is_denied(self):
        self.run_probe('entry-after-walker')

    def test_partial_pristine_and_failed_sdk_states_are_not_call_authority(self):
        for phase in ('pristine', 'partial', 'failed-sdk'):
            with self.subTest(phase=phase):
                self.run_probe(phase)

    def test_walker_requires_a_matching_active_call(self):
        self.run_probe('walker-without-active')

    def test_walker_requires_the_certified_table_and_successor(self):
        for field in ('table', 'successor'):
            with self.subTest(field=field):
                self.run_probe('walker-' + field)

    def test_walker_reentry_after_progress_is_denied(self):
        self.run_probe('walker-reentry')

    def test_constructor_table_tampering_before_entry_and_during_walk_is_denied(self):
        for stage in ('ctor-before-entry', 'ctor-terminator', 'ctor-during-walk'):
            with self.subTest(stage=stage):
                self.run_probe(stage)

    def test_return_requires_actual_translated_walker_completion(self):
        self.run_probe('return-without-walker')

    def test_return_cannot_skip_an_outstanding_walker(self):
        self.run_probe('return-outstanding-walker')

    def test_entry_failure_preserves_already_certified_sdk_authority(self):
        self.run_probe('entry-certified-r3')

    def test_return_requires_the_saved_registers(self):
        for field in ('r1', 'r2', 'r13', 'r31', 'lr'):
            with self.subTest(field=field):
                self.run_probe('return-' + field)

    def test_live_images_do_not_authorize_the_successor_without_a_pending_call(self):
        self.run_probe('no-pending')

    def test_external_return_restoration_requires_both_translated_rfis(self):
        self.run_probe('interrupt-external')

    def test_decrementer_return_restoration_requires_both_translated_rfis(self):
        self.run_probe('interrupt-decrementer')

    def test_sequential_interrupts_are_supported_after_restoration(self):
        self.run_probe('interrupt-sequential')

    def test_raw_misaligned_srr0_is_rejected_before_architectural_rfi(self):
        self.run_probe('rfi-misaligned')

    def test_final_rfi_preserves_fp_and_ee_in_the_restorable_mask(self):
        for bit in ('fp', 'ee'):
            with self.subTest(bit=bit):
                self.run_probe('rfi-' + bit)

    def test_final_rfi_requires_complete_dol_text_containment_and_fresh_bytes(self):
        for source in ('outside-dol', 'text-tamper'):
            with self.subTest(source=source):
                self.run_probe('rfi-' + source)

    def test_final_rfi_without_verified_vector_handoff_is_denied(self):
        self.run_probe('rfi-no-stage')

    def test_final_rfi_cannot_divert_or_reenter_prolog(self):
        for target in ('diverted', 'prolog'):
            with self.subTest(target=target):
                self.run_probe('rfi-' + target)

    def test_rfi_requires_the_frozen_register_frame(self):
        for field in ('r1', 'r2', 'r13', 'r31', 'lr'):
            with self.subTest(field=field):
                self.run_probe('rfi-register-' + field)

    def test_suspended_dispatch_cannot_restore_return_permission(self):
        for target in ('direct', 'prolog'):
            with self.subTest(target=target):
                self.run_probe('suspended-' + target)

    def test_nested_exceptions_and_interrupts_fail_closed(self):
        for stage in ('vector', 'handler', 'interrupt'):
            with self.subTest(stage=stage):
                self.run_probe('nested-' + stage)

    def test_verified_vector_must_have_matching_bytes_and_class(self):
        for field in ('tamper', 'wrong-class'):
            with self.subTest(field=field):
                self.run_probe('vector-' + field)

    def test_suspension_freezes_context_and_handler_inputs(self):
        for field in ('context', 'handler'):
            with self.subTest(field=field):
                self.run_probe('suspended-' + field)

    def test_tampering_never_rebaselines_active_ready_or_suspended_authority(self):
        for phase in ('active', 'ready', 'suspended'):
            for field in ('header', 'code', 'owner', 'neighbor', 'certificate'):
                with self.subTest(phase=phase, field=field):
                    self.run_probe(phase + '-tamper-' + field)

    def test_fallible_traces_publish_no_new_call_capability(self):
        for stage in ('entry', 'completion', 'return'):
            with self.subTest(stage=stage):
                self.run_probe(stage + '-trace')

    def test_caller_advance_expires_authority(self):
        self.run_probe('expiry')

    def test_a_completed_image_cannot_initialize_twice(self):
        self.run_probe('repeat-init')

    def test_epilog_and_unresolved_canonical_and_scratch_entries_stay_denied(self):
        for entry in ('epilog', 'unresolved'):
            for address in ('canonical', 'scratch'):
                with self.subTest(entry=entry, address=address):
                    self.run_probe(entry + '-' + address)

    def test_api_and_native_unlink_keep_all_ownership(self):
        for entry in ('unlink', 'native-unlink'):
            with self.subTest(entry=entry):
                self.run_probe(entry)


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--build-dir':
        test_host.OUT = Path(sys.argv[2]).resolve()
        test_host.StartupHostTest.setUpClass = classmethod(lambda cls: None)
        sys.argv = sys.argv[:1]
    unittest.main()
