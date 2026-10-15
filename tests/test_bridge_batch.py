import json
import shutil
import struct
import subprocess
import unittest

import test_selected_rel_contracts as fixture
from test_composite import DOL_TEXT_ADDR, write_rel_module, write_ship_disc

sc = fixture.sc


def bridge_fixture(root):
    spec = sc.selected_module(9)
    raw = bytearray(spec['raw_size'])
    relocation = 0x50E8
    for at, value in ((0, 9), (0xC, spec['section_count']), (0x10, 0x4C), (0x1C, 3),
                      (0x20, 88), (0x24, relocation), (0x28, relocation - 16), (0x2C, 16),
                      (0x38, 44), (0x3C, 88), (0x40, 4), (0x44, 8), (0x48, relocation)):
        struct.pack_into('>I', raw, at, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    for index, offset, size in spec['section_layout']:
        struct.pack_into('>II', raw, 0x4C + index * 8, offset | (index == 1), size)
    raw[0xE4:0x4D98] = bytes((i * 17 + 1) & 255 for i in range(19636))
    struct.pack_into('>I', raw, 0xF8, 0x48000001)
    struct.pack_into('>4I', raw, relocation - 16, 9, relocation, 0, relocation + 8)
    for i, record in enumerate(((0, 203, 0, 0), (0, 202, 1, 0),
                                 (20, 10, 0, 0x80241178), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, relocation + i * 8, *record)
    (root / 'rels_bin/0009.rel').write_bytes(raw)
    struct.pack_into('>I', raw, 0x40, 8)
    (root / 'input/bridge-disc.rel').write_bytes(raw)
    write_rel_module(root / 'rels_out', 9,
        [(0xC06500E4, 0xC06540E4), (0xC06540E4, 0xC0654D98)])


class BridgeSelection(unittest.TestCase):
    def test_wave_three_retains_every_prior_member(self):
        self.assertEqual(sc.ACTOR_BATCHES.get('opening-c'), (9, 19, 60, 84, 168, 400))

    def test_acquisition_delta_is_only_bridge(self):
        self.assertEqual(sc.ACTOR_CAPTURE_GROUPS.get('opening-c'), (9,))

    def test_bridge_has_its_exact_owned_nonzero_bss_layout(self):
        spec = sc.selected_module(9)
        self.assertEqual(spec['section_layout'][-1], (6, 0, 88))
        self.assertEqual(spec['bss_alignment'], 8)
        self.assertEqual(spec['ctor_return_pc'], 0xC06500FC)


class BridgeBatchContracts(unittest.TestCase):
    def setUp(self):
        fixture.GrassBatchContracts.setUp(self)
        bridge_fixture(self.root)
        item = fixture.capture_binding(self.root, 9, 'bridge', 0xC06500E4, 0xE4, b'BRIDGE9\0')
        directory = self.root / 'bridge-captures'
        directory.mkdir()
        shutil.copy2(self.root / item['capture'], directory / item['capture'])
        item['capture'] = 'bridge-captures/bridge.ship-code-input'
        self.manifest['modules']['9'] = item
        (directory / 'complete.json').write_text(json.dumps({'version': 1, 'batch': 'opening-c',
            'modules': [{'id': 9, 'capture': 'bridge.ship-code-input', 'size': 19664}]}))

    tearDown = fixture.SelectedContracts.tearDown
    contract = fixture.SelectedContracts.contract

    def generate(self, extra_args=(), omit=()):
        return fixture.IkadaContracts.generate(self, ('--actor-rel-batch', 'opening-c', *extra_args), omit)

    def test_cumulative_selection_exports_bridge_and_all_prior_members(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        output = self.root / 'composite'
        selected = json.loads((output / 'generation_receipt.json').read_text())['selected_rel_contracts']
        self.assertEqual(set(selected['contracts']), {'9', '19', '60', '84', '168', '400'})
        canonical = (self.root / 'bridge.ship-code-input').read_bytes()[28:]
        table = (output / 'module_tables.inc').read_text()
        for part in (canonical[:16384], canonical[16384:]):
            self.assertIn(f'0x{fixture.gc.fnv1a64(part):016X}ull', table)

    def test_bridge_tables_remain_independently_bounded(self):
        _, fields, _ = self.contract(9)
        self.assertEqual([(s['index'], s['size']) for s in fields['canonical_sections'] if s['index'] in (2, 3)],
                         [(2, 4), (3, 4)])

    def test_bridge_layout_and_alignment_mutations_refuse(self):
        for at, value in ((0x20, 96), (0x44, 4), (0x40, 4), (0x38, 48),
                          (0x58, 19632), (0x60, 8), (0x68, 8), (0x80, 96)):
            with self.subTest(at=at):
                path = self.root / 'input/bridge-disc.rel'
                original = path.read_bytes()
                data = bytearray(original)
                struct.pack_into('>I', data, at, value)
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.contract(9)
                path.write_bytes(original)

    def test_bridge_capture_mutations_refuse(self):
        path = self.root / 'bridge.ship-code-input'
        original = path.read_bytes()
        for at in (0, 11, 15, 19, 23, 27, 48, len(original) - 1, len(original), -1):
            with self.subTest(at=at):
                data = bytearray(original)
                if at == len(original): data.append(0)
                elif at == -1: data.pop()
                else: data[at] ^= 1
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.contract(9)

    def test_coherent_bss_change_cannot_expand_the_approved_layout(self):
        for suffix in ('rels_bin/0009.rel', 'input/bridge-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x20, 96)
            struct.pack_into('>I', data, 0x80, 96)
            path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'approved section/BSS layout'):
            self.contract(9)

    def test_each_bridge_provenance_binding_is_required(self):
        original = json.dumps(self.manifest)
        for field in ('missing', 'extra', 'raw_sha256', 'processed_rel_sha256', 'capture_sha256',
                      'translation_sha256'):
            with self.subTest(field=field):
                self.manifest = json.loads(original)
                if field == 'missing': del self.manifest['modules']['9']
                elif field == 'extra': self.manifest['modules']['10'] = self.manifest['modules']['9']
                elif field == 'translation_sha256': self.manifest['modules']['9'][field] = {}
                else: self.manifest['modules']['9'][field] = '0' * 64
                result = self.generate()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_every_capture_group_remains_required(self):
        for name in ('complete.json', 'grass-captures/complete.json', 'bridge-captures/complete.json'):
            with self.subTest(name=name):
                path = self.root / name
                original = path.read_bytes()
                path.unlink()
                result = self.generate()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())
                path.write_bytes(original)

    def test_bridge_capture_group_cannot_be_misbound(self):
        path = self.root / 'bridge-captures/complete.json'
        path.write_text(json.dumps({'version': 1, 'batch': 'opening-b', 'modules': []}))
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_bridge_successor_requires_the_actual_walker_call(self):
        for suffix in ('rels_bin/0009.rel', 'input/bridge-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x50FC, DOL_TEXT_ADDR)
            path.write_bytes(data)
        fixture.capture_binding(self.root, 9, 'bridge', 0xC06500E4, 0xE4, b'BRIDGE9\0')
        with self.assertRaisesRegex(ValueError, 'constructor successor'):
            self.contract(9)

    def test_bridge_dtor_table_requires_its_terminator(self):
        for suffix in ('rels_bin/0009.rel', 'input/bridge-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x4D9C, 0xC06500E4)
            path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'dtors requires'):
            self.contract(9)

    def test_bridge_archive_member_must_be_unique(self):
        raw = (self.root / 'input/bridge-disc.rel').read_bytes()
        disc = write_ship_disc(self.root, [('d_a_bridge.rel', raw), ('nested/d_a_bridge.rel', raw)])
        with self.assertRaisesRegex(ValueError, 'exactly one disc archive member d_a_bridge.rel'):
            sc.read_disc_member(disc, 9)

    def test_bridge_duplicate_provenance_refuses(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        path = self.root / 'provenance.json'
        path.write_text(path.read_text().replace('"9":', '"9": {}, "9":', 1))
        result = subprocess.run(result.args, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('duplicate producer provenance key: 9', result.stderr)


if __name__ == '__main__':
    unittest.main()
