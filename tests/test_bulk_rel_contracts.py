import json
from pathlib import Path
import struct
import tempfile
import unittest

import test_selected_rel_contracts as fixture

sc = fixture.sc
EXCLUDED = {1, 26, 46, 129, 188, 189, 214, 224, 276, 379}
INHERITED = {9, 19, 60, 81, 84, 168, 400}


class BulkSelection(unittest.TestCase):
    def test_bulk_is_exactly_405_unique_nonexcluded_modules(self):
        members = sc.ACTOR_BATCHES.get('bulk')
        self.assertIsNotNone(members)
        self.assertEqual(len(members), 405)
        self.assertEqual(members, tuple(sorted(set(range(1, 416)) - EXCLUDED)))

    def test_bulk_delta_has_398_members_and_retains_opening_d_toripost(self):
        self.assertEqual(set(sc.ACTOR_CAPTURE_GROUPS['bulk-ww149a']), set(range(1, 416)) - EXCLUDED - (INHERITED - {81}))
        self.assertEqual(sc.selected_module(81)['capture_group'], 'opening-d')
        self.assertEqual(sc.selected_module(81)['magic'], b'TPOST81\0')
        self.assertEqual(sum(sc.selected_module(mid)['capture_group'] == 'bulk-ww149a'
                             for mid in sc.ACTOR_BATCHES['bulk'] if mid not in (60, 168, 400)), 398)

    def test_bulk_catalog_preserves_outlier_layouts(self):
        cases = ((202, (8, 4), 8, 4), (362, (8, 8), 32, 1290), (415, (8, 8), 32, 832968),
                 (10, (4, 8), 8, None), (110, (8, 4), 1, 0))
        for mid, tables, alignment, bss in cases:
            with self.subTest(mid=mid):
                spec = sc.selected_module(mid)
                self.assertEqual(spec['table_sizes'], tables)
                self.assertEqual(spec['bss_alignment'], alignment)
                if bss is not None:
                    self.assertEqual(sum(size for _, offset, size in spec['section_layout'] if not offset), bss)

    def test_catalog_names_and_capture_magics_are_unique(self):
        specs = [sc.selected_module(mid) for mid in sc.ACTOR_BATCHES['bulk']]
        for key in ('name', 'magic'):
            with self.subTest(key=key):
                self.assertEqual(len({s[key] for s in specs}), 405)

    def test_excluded_modules_are_not_approved(self):
        for mid in EXCLUDED:
            with self.subTest(mid=mid), self.assertRaises(ValueError):
                sc.selected_module(mid)

    def test_historical_modes_are_unchanged(self):
        expected = {'opening-a': (60, 84, 168, 400), 'opening-b': (19, 60, 84, 168, 400),
                    'opening-c': (9, 19, 60, 84, 168, 400), 'opening-d': (9, 19, 60, 81, 84, 168, 400)}
        self.assertEqual({k: sc.ACTOR_BATCHES[k] for k in expected}, expected)


class BulkCompletion(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest = {'modules': {}}
        self.completions = {}
        for mid in sc.ACTOR_BATCHES['bulk']:
            if mid in (60, 168, 400):
                continue
            spec = sc.selected_module(mid)
            group = spec['capture_group']
            directory = self.root / group
            directory.mkdir(exist_ok=True)
            name = f'{spec["name"]}-{mid}.ship-code-input'
            path = directory / name
            path.write_bytes(b'\0' * (28 + spec['exec'][3]))
            if mid == 81:
                path.write_bytes(spec['magic'] + struct.pack('>5I', 1, *spec['exec']) + bytes(spec['exec'][3]))
            self.manifest['modules'][str(mid)] = {'capture': str(path)}
            self.completions.setdefault(group, []).append({'id': mid, 'capture': name, 'size': path.stat().st_size})
        spec = sc.selected_module(81)
        path = self.root / 'bulk-ww149a/d_a_obj_toripost-81.ship-code-input'
        inherited = Path(self.manifest['modules']['81']['capture']).read_bytes()
        path.write_bytes(b'REL00081' + inherited[8:])
        self.completions['bulk-ww149a'].append({'id': 81, 'capture': path.name, 'size': path.stat().st_size})
        for group, members in self.completions.items():
            members.sort(key=lambda item: item['id'])
            (self.root / group / 'complete.json').write_text(json.dumps({'version': 1, 'batch': group, 'modules': members}))
        self.path = self.root / 'manifest.json'
        self.path.write_text(json.dumps(self.manifest))

    def test_complete_399_publication_accepts_398_new_certificates(self):
        sc.verify_batch_completion(self.path, 'bulk')

    def test_incomplete_bulk_publication_refuses(self):
        path = self.root / 'bulk-ww149a/complete.json'
        data = json.loads(path.read_text())
        data['modules'] = [item for item in data['modules'] if item['id'] != 81]
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, 'completion receipt'):
            sc.verify_batch_completion(self.path, 'bulk')

    def test_missing_unselected_toripost_capture_refuses(self):
        (self.root / 'bulk-ww149a/d_a_obj_toripost-81.ship-code-input').unlink()
        with self.assertRaises(FileNotFoundError):
            sc.verify_batch_completion(self.path, 'bulk')

    def test_unselected_toripost_capture_cannot_change_publication_bytes(self):
        path = self.root / 'bulk-ww149a/d_a_obj_toripost-81.ship-code-input'
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'toripost publication'):
            sc.verify_batch_completion(self.path, 'bulk')

    def test_duplicate_bulk_completion_member_refuses(self):
        path = self.root / 'bulk-ww149a/complete.json'
        data = json.loads(path.read_text())
        data['modules'].append(data['modules'][0])
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, 'completion receipt'):
            sc.verify_batch_completion(self.path, 'bulk')


def synthetic_contract(mid):
    spec = sc.selected_module(mid)
    raw = bytearray(spec['raw_size'])
    relocation = len(raw) - 80
    bss = sum(size for _, offset, size in spec['section_layout'] if not offset)
    for at, value in ((0, mid), (0xC, spec['section_count']), (0x10, 0x4C), (0x1C, 3),
                      (0x20, bss), (0x24, relocation), (0x28, relocation - 16), (0x2C, 16),
                      (0x38, 44), (0x3C, 88), (0x40, 4), (0x44, spec['bss_alignment']), (0x48, relocation)):
        struct.pack_into('>I', raw, at, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    for index, offset, size in spec['section_layout']:
        struct.pack_into('>II', raw, 0x4C + index * 8, offset | (index == 1), size)
        if offset:
            assert offset + size <= relocation - 16
    struct.pack_into('>I', raw, spec['exec'][1] + 20, 0x48000001)
    records = []
    for index, size in zip((2, 3), spec['table_sizes']):
        if size == 8:
            records.extend(((0, 202, index, 0), (0, 1, 1, 96)))
    records.append((0, 203, 0, 0))
    struct.pack_into('>4I', raw, relocation - 16, mid, relocation, 0, relocation + 8 * len(records))
    records.extend(((0, 202, 1, 0), (20, 10, 0, 0x80241178), (0, 203, 0, 0)))
    for i, record in enumerate(records):
        struct.pack_into('>HBBI', raw, relocation + 8 * i, *record)
    base = spec['exec'][2] - spec['exec'][1]
    sections = fixture.gc.rel_linked_sections(raw, base)
    fixture.gc.apply_rel_data_relocations({mid: raw}, {mid: {'base': base, 'sections': sections}})
    canonical = sc.relocate_ship(raw, sections, fixture.DOL_RANGES, mid)
    identity = bytearray(raw)
    struct.pack_into('>I', identity, 0x40, spec['image_alignment'])
    capture = spec['magic'] + struct.pack('>5I', 1, *spec['exec']) + canonical[1]
    chunks = [(start, min(start + 0x4000, spec['exec'][2] + spec['exec'][3]))
              for start in range(spec['exec'][2], spec['exec'][2] + spec['exec'][3], 0x4000)]
    return raw, identity, sections, chunks, capture


class BulkCanonicalContracts(unittest.TestCase):
    def contract(self, mid, data):
        raw, identity, sections, chunks, capture = data
        return sc.normal_contract(b'synthetic DOL', raw, sections, chunks, fixture.DOL_RANGES,
                                  identity, mid, capture)

    def test_outlier_layouts_replay_with_complete_chunks_and_null_bss_exports(self):
        for mid in (2, 4, 10, 110, 202, 362, 415):
            with self.subTest(mid=mid):
                data = synthetic_contract(mid)
                text, fields, executable = self.contract(mid, data)
                self.assertEqual(len(executable), sc.selected_module(mid)['exec'][3])
                self.assertEqual(sum(end-start for start,end in fields['chunks']), len(executable))
                self.assertNotIn(6, [s['index'] for s in fields['canonical_sections']])
                self.assertNotIn('canonical_6', text)

    def test_both_unequal_table_directions_check_each_terminator(self):
        for mid in (10, 110):
            for index in (2, 3):
                with self.subTest(mid=mid, index=index):
                    data = synthetic_contract(mid)
                    section = data[2][index]
                    for raw in data[:2]:
                        struct.pack_into('>I', raw, section['offset'] + section['size'] - 4,
                                         sc.selected_module(mid)['exec'][2])
                    with self.assertRaises(ValueError):
                        self.contract(mid, data)

    def test_all_original_alignment_32_rows_reject_alignment_8(self):
        mids = [mid for mid in sc.ACTOR_BATCHES['bulk'] if sc.selected_module(mid).get('image_alignment') == 32]
        self.assertEqual(len(mids), 9)
        for mid in mids:
            with self.subTest(mid=mid):
                data = synthetic_contract(mid)
                self.contract(mid, data)
                struct.pack_into('>I', data[1], 0x40, 8)
                with self.assertRaisesRegex(ValueError, 'original disc alignment'):
                    self.contract(mid, data)

    def test_long_delta_nop_controls_accumulate_without_patching_the_cursor(self):
        raw, _, sections, _, _ = synthetic_contract(202)
        relocation = len(raw) - 128
        records = ((0, 202, 5, 0), (65535, 201, 0, 0), (65535, 201, 0, 0),
                   (6, 1, 1, 96), (0, 203, 0, 0), (0, 203, 0, 0))
        struct.pack_into('>III', raw, 0x24, relocation, relocation - 16, 16)
        struct.pack_into('>4I', raw, relocation - 16, 202, relocation, 0, relocation + 40)
        for i, record in enumerate(records):
            struct.pack_into('>HBBI', raw, relocation + i * 8, *record)
        canonical = sc.relocate_ship(raw, sections, fixture.DOL_RANGES, 202)
        self.assertEqual(struct.unpack_from('>I', canonical[5], 131076)[0], sc.selected_module(202)['exec'][2] + 96)
        self.assertEqual(canonical[5][:131076], bytes(131076))


class BulkPolicyGeneration(unittest.TestCase):
    def test_policy_generator_rejects_missing_or_extra_certificate_members(self):
        import generate_bulk_actor_policy as policy
        for members in ({}, {'1': {}}):
            with self.subTest(members=members), self.assertRaisesRegex(ValueError, 'exactly the 405'):
                policy.render({'selected_rel_contracts': {'contracts': members, 'build_identity': 'a' * 64}})

    def test_bulk_catalog_generator_rejects_an_unpinned_specification(self):
        import generate_bulk_rel_catalog as catalog
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'spec.json'
            path.write_text('{}')
            with self.assertRaisesRegex(ValueError, 'frozen WW147A'):
                catalog.render(path)


if __name__ == '__main__':
    unittest.main()
