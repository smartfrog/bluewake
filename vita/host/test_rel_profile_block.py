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
            content = archive('success', resident=True)
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
