import json
import shutil
import struct
import subprocess
import unittest

import test_bridge_batch as bridge
import test_selected_rel_contracts as fixture
from test_composite import DOL_TEXT_ADDR, write_rel_module, write_ship_disc

sc = fixture.sc


def toripost_fixture(root):
    spec = sc.selected_module(81)
    raw = bytearray(spec['raw_size'])
    relocation = 0x2B40
    for at, value in ((0, 81), (0xC, spec['section_count']), (0x10, 0x4C), (0x1C, 3),
                      (0x20, 45), (0x24, relocation), (0x28, relocation - 16), (0x2C, 16),
                      (0x38, 44), (0x3C, 88), (0x40, 4), (0x44, 8), (0x48, relocation)):
        struct.pack_into('>I', raw, at, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    for index, offset, size in spec['section_layout']:
        struct.pack_into('>II', raw, 0x4C + index * 8, offset | (index == 1), size)
    raw[0xF4:0x2540] = bytes((i * 19 + 1) & 255 for i in range(9292))
    struct.pack_into('>I', raw, 0x108, 0x48000001)
    struct.pack_into('>4I', raw, relocation - 16, 81, relocation, 0, relocation + 40)
    for i, record in enumerate(((0, 202, 2, 0), (0, 1, 1, 96), (0, 202, 3, 0),
                                 (0, 1, 1, 100), (0, 203, 0, 0), (0, 202, 1, 0),
                                 (20, 10, 0, 0x80241178), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, relocation + i * 8, *record)
    (root / 'rels_bin/0081.rel').write_bytes(raw)
    struct.pack_into('>I', raw, 0x40, 8)
    (root / 'input/toripost-disc.rel').write_bytes(raw)
    write_rel_module(root / 'rels_out', 81, [(0xC18A00F4, 0xC18A2540)])


class ToripostSelection(unittest.TestCase):
    def test_wave_four_retains_every_prior_member(self):
        self.assertEqual(sc.ACTOR_BATCHES.get('opening-d'), (9, 19, 60, 81, 84, 168, 400))

    def test_acquisition_delta_is_only_toripost(self):
        self.assertEqual(sc.ACTOR_CAPTURE_GROUPS.get('opening-d'), (81,))

    def test_toripost_has_its_exact_owned_nonzero_bss_layout(self):
        spec = sc.selected_module(81)
        self.assertEqual(spec['section_layout'][-1], (6, 0, 45))
        self.assertEqual(spec['bss_alignment'], 8)
        self.assertEqual(spec['ctor_return_pc'], 0xC18A010C)


class ToripostBatchContracts(unittest.TestCase):
    def setUp(self):
        bridge.BridgeBatchContracts.setUp(self)
        toripost_fixture(self.root)
        item = fixture.capture_binding(self.root, 81, 'toripost', 0xC18A00F4, 0xF4, b'TPOST81\0')
        directory = self.root / 'toripost-captures'
        directory.mkdir()
        shutil.copy2(self.root / item['capture'], directory / item['capture'])
        item['capture'] = 'toripost-captures/toripost.ship-code-input'
        self.manifest['modules']['81'] = item
        (directory / 'complete.json').write_text(json.dumps({'version': 1, 'batch': 'opening-d',
            'modules': [{'id': 81, 'capture': 'toripost.ship-code-input', 'size': 9320}]}))

    tearDown = fixture.SelectedContracts.tearDown
    contract = fixture.SelectedContracts.contract

    def generate(self, extra_args=(), omit=()):
        return fixture.IkadaContracts.generate(self, ('--actor-rel-batch', 'opening-d', *extra_args), omit)

    def test_cumulative_selection_exports_toripost_and_all_prior_members(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        output = self.root / 'composite'
        selected = json.loads((output / 'generation_receipt.json').read_text())['selected_rel_contracts']
        self.assertEqual(set(selected['contracts']), {'9', '19', '60', '81', '84', '168', '400'})
        canonical = (self.root / 'toripost.ship-code-input').read_bytes()[28:]
        self.assertIn(f'0x{fixture.gc.fnv1a64(canonical):016X}ull', (output / 'module_tables.inc').read_text())

    def test_toripost_tables_remain_independently_bounded(self):
        _, fields, _ = self.contract(81)
        self.assertEqual([(s['index'], s['size']) for s in fields['canonical_sections'] if s['index'] in (2, 3)],
                         [(2, 8), (3, 8)])

    def test_toripost_layout_and_alignment_mutations_refuse(self):
        for at, value in ((0x20, 48), (0x44, 4), (0x40, 4), (0x38, 48),
                          (0x58, 9288), (0x60, 4), (0x68, 4), (0x80, 48)):
            with self.subTest(at=at):
                path = self.root / 'input/toripost-disc.rel'
                original = path.read_bytes()
                data = bytearray(original)
                struct.pack_into('>I', data, at, value)
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.contract(81)
                path.write_bytes(original)

    def test_toripost_capture_mutations_refuse(self):
        path = self.root / 'toripost.ship-code-input'
        original = path.read_bytes()
        for at in (0, 11, 15, 19, 23, 27, 48, len(original) - 1, len(original), -1):
            with self.subTest(at=at):
                data = bytearray(original)
                if at == len(original): data.append(0)
                elif at == -1: data.pop()
                else: data[at] ^= 1
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.contract(81)

    def test_coherent_bss_change_cannot_expand_the_approved_layout(self):
        for suffix in ('rels_bin/0081.rel', 'input/toripost-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x20, 48)
            struct.pack_into('>I', data, 0x80, 48)
            path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'approved section/BSS layout'):
            self.contract(81)

    def test_each_toripost_provenance_binding_is_required(self):
        original = json.dumps(self.manifest)
        for field in ('missing', 'extra', 'raw_sha256', 'processed_rel_sha256', 'capture_sha256',
                      'translation_sha256'):
            with self.subTest(field=field):
                self.manifest = json.loads(original)
                if field == 'missing': del self.manifest['modules']['81']
                elif field == 'extra': self.manifest['modules']['82'] = self.manifest['modules']['81']
                elif field == 'translation_sha256': self.manifest['modules']['81'][field] = {}
                else: self.manifest['modules']['81'][field] = '0' * 64
                result = self.generate()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_every_capture_group_remains_required(self):
        for name in ('complete.json', 'grass-captures/complete.json', 'bridge-captures/complete.json',
                     'toripost-captures/complete.json'):
            with self.subTest(name=name):
                path = self.root / name
                original = path.read_bytes()
                path.unlink()
                result = self.generate()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())
                path.write_bytes(original)

    def test_toripost_capture_group_cannot_be_misbound(self):
        path = self.root / 'toripost-captures/complete.json'
        path.write_text(json.dumps({'version': 1, 'batch': 'opening-c', 'modules': []}))
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_toripost_successor_requires_the_actual_walker_call(self):
        for suffix in ('rels_bin/0081.rel', 'input/toripost-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x2B74, DOL_TEXT_ADDR)
            path.write_bytes(data)
        fixture.capture_binding(self.root, 81, 'toripost', 0xC18A00F4, 0xF4, b'TPOST81\0')
        with self.assertRaisesRegex(ValueError, 'constructor successor'):
            self.contract(81)

    def test_toripost_dtor_table_requires_its_terminator(self):
        for suffix in ('rels_bin/0081.rel', 'input/toripost-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x254C, 0xC18A00F4)
            path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'dtors requires'):
            self.contract(81)

    def test_toripost_archive_member_must_be_unique(self):
        raw = (self.root / 'input/toripost-disc.rel').read_bytes()
        disc = write_ship_disc(self.root, [('d_a_obj_toripost.rel', raw), ('nested/d_a_obj_toripost.rel', raw)])
        with self.assertRaisesRegex(ValueError, 'exactly one disc archive member d_a_obj_toripost.rel'):
            sc.read_disc_member(disc, 81)

    def test_toripost_duplicate_provenance_refuses(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        path = self.root / 'provenance.json'
        path.write_text(path.read_text().replace('"81":', '"81": {}, "81":', 1))
        result = subprocess.run(result.args, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('duplicate producer provenance key: 81', result.stderr)


if __name__ == '__main__':
    unittest.main()
