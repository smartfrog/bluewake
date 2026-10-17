from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

import test_di_read
import test_host
from test_rel_contract import archive


class ResidentProfileBlockTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_di_read.DiscReadTest.setUpClass()

    def run_probe(self, scenario):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='profile-block-') as directory:
            root = Path(directory)
            (root / 'synthetic.dol').write_bytes(bytes([1, 2, 3, 4]))
            image = bytearray(test_di_read.DiscReadTest.image.build_image())
            content = archive('success', (test_host.OUT / 'block_title.rel').read_bytes(), resident=True)
            fst = test_di_read.DiscReadTest.image.FST_OFFSET
            image[fst + 5 * 12 + 1:fst + 5 * 12 + 9] = b'RELS.arc'
            struct.pack_into('>II', image, fst + 16, len(image), len(content))
            image.extend(content)
            disc = root / 'ux0:data/bluewake/GZLE01.iso'
            disc.parent.mkdir(parents=True)
            disc.write_bytes(image)
            result = subprocess.run([str(test_host.OUT / 'rel_profile_block_probe'), scenario],
                                    cwd=root, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stderr

    def test_title_complete_fixed_chain(self):
        self.run_probe('title-chain')

    def test_title_exact_above_mem1_inline_pointer(self):
        self.run_probe('title-inline')

    def test_title_immutable_fix_size_survives_live_sdk_rebase(self):
        self.run_probe('title-raw-fix')

    def test_title_self_first_imports_are_fixed_trimmed(self):
        self.run_probe('title-trim')

    def test_title_logical_shrink_preserves_complete_backing(self):
        self.run_probe('title-shrink')

    def test_title_append_preserves_profile_and_ship_authority(self):
        self.run_probe('title-neighbor')

    def test_title_boundary_routes_and_duplicate_windows(self):
        for route in ('host', 'edge', 'dispatch', 'duplicate', 'pending-exception'):
            with self.subTest(route=route):
                self.run_probe('title-route-' + route)

    def test_adjacent_ordinary_allocation_falls_through_without_state_changes(self):
        self.run_probe('title-adjacent-ordinary')

    def test_title_input_refusal_receipt(self):
        log = self.run_probe('title-bad-r4')
        self.assertIn('[rel-sdk-input-refusal]', log)
        self.assertIn('name_base=80006000', log)
        self.assertIn('owner_bss=00000000', log)
        self.assertIn('fixed=1 input_form=1', log)
        self.assertIn('bss=81832A24 bss_size=0', log)
        self.assertIn('expected_bss=81832A20 actual_r4=81832A24', log)
        self.assertEqual(log.count('[rel-sdk-input-refusal]'), 1)

    def test_title_exposure_validates_immutable_retained_bounds(self):
        for field in ('fix-overflow', 'fix-small', 'extent-at-bytes', 'metadata-tail', 'section-tail', 'bss-alignment', 'trace'):
            with self.subTest(field=field):
                self.run_probe('title-expose-' + field)

    def test_title_fixed_append_mutations_fail_closed(self):
        for field in ('normal', 'fallback', 'missing-query', 'pc', 'lr', 'r3', 'r4', 'r31', 'heap',
                      'owner-bss', 'header-bss', 'section-bss', 'fix-overflow', 'extent', 'slack',
                      'region', 'interior', 'one-past', 'neighbor-alias', 'mirror', 'mapping',
                      'duplicate-module', 'publication', 'certificate-missing', 'certificate-copy',
                      'certificate-reassociate', 'raw', 'sections', 'names', 'name-overflow', 'name-unbounded',
                      'result', 'pre-trim', 'nonfixed', 'header', 'relocation', 'untouched',
                      'head', 'tail', 'previous', 'next', 'skip-profile', 'skip-ship', 'ship-incomplete',
                      'prepare', 'finish', 'prolog', 'resize', 'skip-shrink', 'skip-accounting',
                      'query-reentry', 'return-reentry', 'constructor', 'caller-return', 'exception',
                      'trace-prepare', 'trace-finish', 'allocation', 'sdk-reentry', 'inline-interior',
                      'inline-one-past', 'inline-neighbor', 'inline-mirror', 'input-form', 'duplicate-names',
                      'duplicate-r4', 'duplicate-neighbor', 'trace-refusal', 'trace-query', 'trace-shrink', 'trace-accounting'):
            with self.subTest(field=field):
                log = self.run_probe('title-bad-' + field)
                if field == 'name-overflow':
                    self.assertIn('name=000000010000001F', log)

    def test_null_current_heap_reaches_fixed_without_allocating(self):
        self.run_probe('dvd')

    def test_translated_guest_creates_initially_empty_singleton(self):
        self.run_probe('singleton')

    def test_whole_image_is_independently_verified_before_constructor(self):
        for field in ('header', 'section', 'relocation', 'untouched', 'foreign'):
            with self.subTest(field=field):
                self.run_probe('image-' + field)

    def test_exact_shrink_changes_accounting_not_storage_capacity(self):
        self.run_probe('shrink')

    def test_plain_guest_completion_is_not_certified_pending_initialization(self):
        self.run_probe('completion')

    def test_forged_singleton_is_not_a_resident_completion_receipt(self):
        self.run_probe('forged-seed')

    def test_wrong_sequence_is_refused(self):
        for field in ('normal', 'sdk-argument', 'sdk-module', 'sdk-caller', 'sdk-reentry', 'sdk-result',
                      'constructor', 'return', 'skip-completion', 'return-r1', 'return-r2', 'return-r13',
                      'return-r31', 'return-lr', 'return-exception'):
            with self.subTest(field=field):
                self.run_probe('sequence-' + field)

    def test_ownership_bounds_and_frozen_inputs_fail_closed(self):
        for field in ('interior', 'owner', 'global', 'ambiguous', 'extent', 'resize', 'raw', 'names'):
            with self.subTest(field=field):
                self.run_probe('ownership-' + field)

    def test_ordinary_blocks_fall_through_without_cpu_or_memory_changes(self):
        self.run_probe('ordinary')

    def test_both_production_admissions_are_required(self):
        for policy in ('off', 'ship', 'title'):
            with self.subTest(policy=policy):
                self.run_probe('policy-' + policy)

    def test_profile_completion_preserves_non_fixed_ship_append_contract(self):
        self.run_probe('ship')

    def test_profile_completion_cannot_satisfy_ship_or_title_pending_call(self):
        for module in (168, 400):
            with self.subTest(module=module):
                self.run_probe('pending-' + str(module))

    def test_allocation_and_trace_failures_cannot_promote_integrity(self):
        for stage in ('allocation', 'allocation-sdk', 'trace-entry', 'trace-sdk', 'trace-shrink', 'trace-return',
                      'trace-ship-prepare', 'trace-ship-finish'):
            with self.subTest(stage=stage):
                self.run_probe('failure-' + stage)

    def test_duplicate_boundaries_are_idempotent_but_progress_closes_windows(self):
        for boundary in ('duplicate', 'query-reentry', 'sdk-return-reentry', 'prolog-reentry', 'return-reentry',
                         'route-host', 'route-edge', 'route-dispatch', 'pending-exception'):
            with self.subTest(boundary=boundary):
                self.run_probe(boundary)

    def test_unlinked_block_accounting_is_admitted_once(self):
        self.run_probe('title-unlink-accounting')

    def test_unlinked_block_accounting_second_site_stays_denied(self):
        self.run_probe('title-unlink-accounting-second-site')

    def test_unlinked_block_accounting_requires_the_unload_status(self):
        self.run_probe('title-unlink-accounting-r31')

    def test_unlinked_block_accounting_requires_zero_arguments(self):
        self.run_probe('title-unlink-accounting-r4')

    def test_linked_block_accounting_unload_shape_stays_denied(self):
        self.run_probe('title-unlink-accounting-linked')

    def test_unlinked_block_accounting_is_one_shot(self):
        self.run_probe('title-unlink-accounting-repeat')

    def test_unlinked_block_accounting_requires_its_restored_image(self):
        self.run_probe('title-unlink-accounting-tampered')
