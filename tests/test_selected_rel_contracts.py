import hashlib
from contextlib import contextmanager
import json
import shutil
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import test_composite as fixture

gc = fixture.gc
sc = gc.ship_contract
TITLE_START = 0xC1E000DC
TITLE_SIZE = 7756
DOL_RANGES = [(fixture.DOL_TEXT_ADDR, fixture.DOL_TEXT_ADDR + 128), (0x80241178, 0x8024117C)]
IKADA_START = 0xC15500F4
IKADA_SIZE = 22620


def ikada_fixture(root):
    raw = bytearray(33184)
    for offset, value in ((0, 60), (0xC, 21), (0x10, 0x4C), (0x1C, 3), (0x20, 532),
                          (0x24, 0x6178), (0x28, 0x6168), (0x2C, 16), (0x38, 44),
                          (0x3C, 88), (0x40, 4), (0x44, 8), (0x48, 0x6178)):
        struct.pack_into('>I', raw, offset, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    for index, pair in enumerate(((0, 0), (245, IKADA_SIZE), (0x5950, 8), (0x5958, 8),
                                  (0x5960, 765), (0x5C60, 1288), (0, 532))):
        struct.pack_into('>II', raw, 0x4C + index * 8, *pair)
    raw[244:0x5950] = bytes((i * 7 + 1) & 255 for i in range(IKADA_SIZE))
    struct.pack_into('>I', raw, 264, 0x48000001)
    struct.pack_into('>4I', raw, 0x6168, 60, 0x6178, 0, 0x61A0)
    for index, record in enumerate(((0, 202, 2, 0), (0, 1, 1, 96),
                                    (0, 202, 3, 0), (0, 1, 1, 100), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 0x6178 + index * 8, *record)
    for index, record in enumerate(((0, 202, 1, 0), (20, 10, 0, 0x80241178), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 0x61A0 + index * 8, *record)
    (root / 'rels_bin/0060.rel').write_bytes(raw)
    struct.pack_into('>I', raw, 0x40, 8)
    (root / 'input/ikada-disc.rel').write_bytes(raw)
    fixture.write_rel_module(root / 'rels_out', 60,
                             [(IKADA_START, IKADA_START + 16384),
                               (IKADA_START + 16384, IKADA_START + IKADA_SIZE)])


def wood_fixture(root):
    raw = bytearray(2596)
    for offset, value in ((0, 84), (0xC, 17), (0x10, 0x4C), (0x1C, 3),
                          (0x24, 0x6BC), (0x28, 0x6AC), (0x2C, 16), (0x38, 44),
                          (0x3C, 88), (0x40, 4), (0x44, 1), (0x48, 0x6BC)):
        struct.pack_into('>I', raw, offset, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    for index, pair in enumerate(((0, 0), (0xD5, 1164), (0x560, 4), (0x564, 4), (0x568, 324))):
        struct.pack_into('>II', raw, 0x4C + index * 8, *pair)
    raw[0xD4:0x560] = bytes((i * 11 + 1) & 255 for i in range(1164))
    struct.pack_into('>I', raw, 0xE8, 0x48000001)
    struct.pack_into('>4I', raw, 0x6AC, 84, 0x6BC, 0, 0x6C4)
    struct.pack_into('>HBBI', raw, 0x6BC, 0, 203, 0, 0)
    for index, record in enumerate(((0, 202, 1, 0), (20, 10, 0, 0x80241178), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 0x6C4 + index * 8, *record)
    (root / 'rels_bin/0084.rel').write_bytes(raw)
    struct.pack_into('>I', raw, 0x40, 8)
    (root / 'input/wood-disc.rel').write_bytes(raw)
    fixture.write_rel_module(root / 'rels_out', 84, [(0xC19A00D4, 0xC19A0560)])


def capture_binding(root, mid, prefix, start, offset, magic):
    raw = (root / f'rels_bin/{mid:04d}.rel').read_bytes()
    sections = gc.rel_linked_sections(raw, start - offset)
    canonical = sc.relocate_ship(raw, sections, DOL_RANGES, module_id=mid)
    executable = [s for s in sections if s['executable'] and s['size']]
    sidecar = magic + struct.pack('>I', len(executable))
    for s in executable:
        sidecar += struct.pack('>IIII', s['index'], s['offset'], s['linked_start'], s['size']) + canonical[s['index']]
    path = root / f'{prefix}.ship-code-input'
    path.write_bytes(sidecar)
    translated = root / f'rels_out/d_a_test_{mid}'
    return {'raw_sha256': hashlib.sha256((root / f'input/{prefix}-disc.rel').read_bytes()).hexdigest(),
            'processed_rel_sha256': hashlib.sha256(raw).hexdigest(), 'capture': path.name,
            'capture_sha256': hashlib.sha256(sidecar).hexdigest(),
            'translation_sha256': {str(p.relative_to(translated)): hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in sorted(translated.rglob('*')) if p.is_file()}}


def grass_fixture(root):
    raw = bytearray(8016)
    for offset, value in ((0, 19), (0xC, 20), (0x10, 0x4C), (0x1C, 3), (0x20, 1330),
                          (0x24, 0x16C0), (0x28, 0x16B0), (0x2C, 16), (0x38, 44),
                          (0x3C, 88), (0x40, 4), (0x44, 8), (0x48, 0x16C0)):
        struct.pack_into('>I', raw, offset, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    for index, pair in enumerate(((0, 0), (0xED, 5404), (0x1608, 4), (0x160C, 8),
                                   (0x1618, 8), (0x1620, 144), (0, 1330))):
        struct.pack_into('>II', raw, 0x4C + index * 8, *pair)
    raw[0xEC:0x1608] = bytes((i * 13 + 1) & 255 for i in range(5404))
    struct.pack_into('>I', raw, 0x100, 0x48000001)
    struct.pack_into('>4I', raw, 0x16B0, 19, 0x16C0, 0, 0x16D8)
    for index, record in enumerate(((0, 202, 3, 0), (0, 1, 1, 96), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 0x16C0 + index * 8, *record)
    for index, record in enumerate(((0, 202, 1, 0), (20, 10, 0, 0x80241178), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 0x16D8 + index * 8, *record)
    (root / 'rels_bin/0019.rel').write_bytes(raw)
    struct.pack_into('>I', raw, 0x40, 8)
    (root / 'input/grass-disc.rel').write_bytes(raw)
    fixture.write_rel_module(root / 'rels_out', 19, [(0xC08D00EC, 0xC08D1608)])


def title_fixture(root):
    raw = bytearray(12288)
    struct.pack_into('>I', raw, 0, 400)
    struct.pack_into('>II', raw, 0xC, 18, 0x4C)
    struct.pack_into('>I', raw, 0x1C, 3)
    struct.pack_into('>III', raw, 0x24, 8700, 8640, 16)
    raw[0x30:0x33] = bytes([1, 1, 1])
    struct.pack_into('>III', raw, 0x34, 0, 44, 88)
    struct.pack_into('>II', raw, 0x40, 4, 4)
    for index, pair in enumerate(((0, 0), (221, TITLE_SIZE), (7976, 4),
                                  (7980, 4), (7984, 476), (8464, 160))):
        struct.pack_into('>II', raw, 0x4C + index * 8, *pair)
    raw[220:7976] = bytes((i * 3 + 1) & 255 for i in range(TITLE_SIZE))
    struct.pack_into('>I', raw, 240, 0x48000001)
    struct.pack_into('>IIII', raw, 8640, 400, 8700, 0, 8740)
    for i, record in enumerate(((0, 202, 1, 0), (2, 6, 2, 0),
                                (4, 4, 2, 0), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 8700 + i * 8, *record)
    for i, record in enumerate(((0, 202, 1, 0), (20, 10, 0, 0x80241178), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 8740 + i * 8, *record)
    (root / 'rels_bin/0400.rel').write_bytes(raw)
    struct.pack_into('>I', raw, 0x40, 8)
    (root / 'input/title-disc.rel').write_bytes(raw)
    fixture.write_rel_module(root / 'rels_out', 400, [(TITLE_START, TITLE_START + TITLE_SIZE)])


def setup(root):
    fixture.setup_fixture(root)
    title_fixture(root)
    dol_path = root / 'input/main.dol'
    dol = bytearray(dol_path.read_bytes())
    struct.pack_into('>I', dol, 4, len(dol))
    struct.pack_into('>I', dol, 0x4C, 0x80241178)
    struct.pack_into('>I', dol, 0x94, 4)
    dol += b'\x4e\x80\x00\x20'
    dol_path.write_bytes(dol)
    fixture.write_dol_dir(root / 'dol_out', [(start, end) for start, end in DOL_RANGES])
    for path in (root / 'rels_bin/0168.rel', root / 'input/ship-disc.rel'):
        ship = bytearray(path.read_bytes())
        struct.pack_into('>I', ship, 264, 0x48000001)
        for i, record in enumerate(((0, 202, 1, 0), (8, 10, 0, fixture.DOL_TEXT_ADDR),
                                    (12, 10, 0, 0x80241178), (0, 203, 0, 0))):
            struct.pack_into('>HBBI', ship, 56164 + i * 8, *record)
        path.write_bytes(ship)
    inputs = {}
    for mid, prefix, start in ((168, 'ship', fixture.SHIP_START), (400, 'title', TITLE_START)):
        inputs[str(mid)] = capture_binding(root, mid, prefix, start, 244 if mid == 168 else 220,
                                           b'SHIP168\0' if mid == 168 else b'TITLE400')
    manifest = {'version': 1, 'producer_identity': 'a' * 64,
                'dol_sha256': hashlib.sha256((root / 'input/main.dol').read_bytes()).hexdigest(),
                'modules': inputs}
    (root / 'provenance.json').write_text(json.dumps(manifest))
    return manifest


def compile_export_probe(root, body, cpp=False, wrapped=False):
    (root / 'core').mkdir(exist_ok=True)
    (root / 'core/cpu.h').write_text('typedef struct CPUState CPUState;\n')
    (root / 'StaticRecompABI.h').write_text(
        '#include <stdint.h>\ntypedef uint8_t u8;\ntypedef uint32_t u32;\n'
        'typedef struct StaticRecompModuleDesc StaticRecompModuleDesc;\n')
    glue = (fixture.SCRIPTS_DIR.parent / 'cmake/composite/module_export.c').read_text()
    getters = '\n'.join(re.search(
        rf'RECOMP_MODULE_EXPORT const {kind}\* {name}\(void\)\n\{{.*?\n\}}', glue, re.S)[0]
        for kind, name in [('BlueWakeShipContractV1', 'staticrecomp_get_ship_contract_v1'),
                           ('BlueWakeRelInitContractSetV1', 'staticrecomp_get_rel_init_contracts_v1')])
    source = root / 'view.c'
    source.write_text('#include <assert.h>\n#include <stddef.h>\n#include <string.h>\n'
        '#include "module_exports.h"\n#include "rel_data.inc"\n#define RECOMP_MODULE_EXPORT\n' + getters + '\n'
        + ('' if cpp else 'int main(void) {\n' + body + '\nreturn 0;\n}\n'))
    command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-const-variable',
        '-I', str(root), '-I', str(root / 'composite'), '-I', str(fixture.SCRIPTS_DIR.parent / 'cmake/composite'),
        str(source), '-o', str(root / ('view.o' if cpp else 'view'))]
    if cpp:
        command.append('-c')
    subprocess.run(command, check=True, capture_output=True, text=True)
    if cpp:
        caller = root / 'caller.cpp'
        include = '#include "module_exports.h"\n'
        if wrapped:
            include = 'extern "C" {\n' + include + '}\n'
        caller.write_text('#include <cassert>\n#include <cstddef>\n' + include +
                          'int main() {\n' + body + '\nreturn 0;\n}\n')
        result = subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
            '-I', str(root), '-I', str(fixture.SCRIPTS_DIR.parent / 'cmake/composite'), str(caller),
            str(root / 'view.o'), '-o', str(root / 'view')], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
    subprocess.run([str(root / 'view')], check=True)


class SelectedContracts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.manifest = setup(self.root)

    def tearDown(self):
        self.temporary.cleanup()

    def contract(self, mid=400):
        root = self.root
        processed = (root / f'rels_bin/{mid:04d}.rel').read_bytes()
        spec = sc.selected_module(mid)
        prefix = spec['name']
        _, offset, start, size = spec['exec']
        sections = gc.rel_linked_sections(processed, start - offset)
        gc.apply_rel_data_relocations({mid: processed}, {mid: {'base': start - offset, 'sections': sections}})
        raw = (root / f'input/{prefix}-disc.rel').read_bytes()
        return sc.normal_contract((root / 'input/main.dol').read_bytes(), processed, sections,
            [(start, start + size)], DOL_RANGES, raw,
            module_id=mid, canonical_input=(root / f'{prefix}.ship-code-input').read_bytes())

    def generate(self, extra_args=(), omit=()):
        root = self.root
        members = [(basename, path.read_bytes()) for basename, path in (
            ('d_a_bridge.rel', root / 'input/bridge-disc.rel'),
            ('d_a_obj_toripost.rel', root / 'input/toripost-disc.rel'),
            ('d_a_grass.rel', root / 'input/grass-disc.rel'),
            ('d_a_obj_ikada.rel', root / 'input/ikada-disc.rel'),
            ('d_a_obj_wood.rel', root / 'input/wood-disc.rel')) if path.exists()]
        disc = fixture.write_ship_disc(root, members)
        data = bytearray(disc.read_bytes())
        fst_at = struct.unpack_from('>I', data, 0x424)[0]
        archive_at, archive_size = struct.unpack_from('>II', data, fst_at + 16)
        title = (root / 'input/title-disc.rel').read_bytes()
        title_at = len(data)
        data += title
        fst_at = len(data)
        fst = struct.pack('>12I', 0x01000000, 0, 4, 0, archive_at, archive_size,
                          0x01000009, 0, 4, 14, title_at, len(title))
        fst += b'RELS.arc\0rels\0d_a_title.rel\0'
        data += fst
        struct.pack_into('>II', data, 0x424, fst_at, len(fst))
        disc.write_bytes(data)
        (root / 'provenance.json').write_text(json.dumps(self.manifest))
        command = [sys.executable, '-B', str(fixture.SCRIPTS_DIR / 'generate_composite.py'),
            '--dol-dir', str(root / 'dol_out'), '--rels-dir', str(root / 'rels_out'),
            '--rels-bin-dir', str(root / 'rels_bin'), '--main-dol', str(root / 'input/main.dol'),
            '--ship-disc', str(disc), '--title-rel-sha256', self.manifest['modules']['400']['raw_sha256'],
            '--selected-rel-provenance', str(root / 'provenance.json'), '--output-dir', str(root / 'composite')]
        if (root / 'input/wood-disc.rel').exists():
            specs = {mid: {'raw_sha256': self.manifest['modules'].get(str(mid), {}).get('raw_sha256'),
                           'processed_sha256': self.manifest['modules'].get(str(mid), {}).get('processed_rel_sha256')}
                     for mid in (9, 19, 81, 84) if (root / f'input/{sc.selected_module(mid)["name"]}-disc.rel').exists()}
            command[2:3] = ['-c', 'import sys; sys.path.insert(0, ' + repr(str(fixture.SCRIPTS_DIR)) + '); '
                'import generate_composite as gc; '
                '[(gc.ship_contract.APPROVED_MODULES[mid].pop("stored_sha256"), '
                'gc.ship_contract.APPROVED_MODULES[mid].update(pins)) for mid,pins in ' + repr(specs) + '.items()]; gc.main()']
        for flag in omit:
            index = command.index(flag)
            del command[index:index + 2]
        return subprocess.run(command + list(extra_args), capture_output=True, text=True)

    def test_title_identity_retains_original_alignment(self):
        _, fields, _ = self.contract()
        self.assertEqual(fields['rel_sha256'], self.manifest['modules']['400']['raw_sha256'])
        self.assertEqual(fields['section_alignment'], {'identity': 8, 'processed': 4})

    def test_title_lifecycle_comes_from_its_header(self):
        _, fields, _ = self.contract()
        self.assertEqual(fields['lifecycle'], {'prolog': TITLE_START, 'epilog': TITLE_START + 44,
                                              'unresolved': TITLE_START + 88})

    def test_title_canonical_capture_mismatch_refused(self):
        path = self.root / 'title.ship-code-input'
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'emitter input'):
            self.contract()

    def test_title_non_alignment_raw_difference_refused(self):
        path = self.root / 'input/title-disc.rel'
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'outside header alignment'):
            self.contract()

    def test_unselected_module_refused(self):
        with self.assertRaisesRegex(ValueError, 'selected module'):
            sc.normal_contract(b'', b'', [], [], [], b'', module_id=401)

    def test_title_changed_lifecycle_refused(self):
        for suffix in ('rels_bin/0400.rel', 'input/title-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x38, 48)
            path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'epilog address mismatch'):
            self.contract()

    def test_title_unsupported_relocation_type_refused(self):
        for suffix in ('rels_bin/0400.rel', 'input/title-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            data[8710] = 5
            path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'relocation type'):
            self.contract()

    def test_title_foreign_import_refused(self):
        for suffix in ('rels_bin/0400.rel', 'input/title-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 8640, 401)
            path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'unsupported module'):
            self.contract()

    def test_selected_generator_exports_only_selected_modules(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        text = (self.root / 'composite/rel_data.inc').read_text()
        self.assertIn('static const BlueWakeRelInitContractV1 s_rel_init_contracts[]', text)
        self.assertIn('168u,56200u,s_ship_raw_rel,s_ship_canonical,3u,', text)
        self.assertIn('400u,12288u,s_title_raw_rel,s_title_canonical,5u,', text)

    def test_missing_selected_translation_refused(self):
        shutil.rmtree(self.root / 'rels_out/d_a_test_400')
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('selected contract requires translated module 400', result.stderr)

    def test_missing_selected_raw_module_refused(self):
        (self.root / 'rels_bin/0400.rel').unlink()
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('selected contract requires raw module 400', result.stderr)

    def test_unapproved_provenance_module_refused(self):
        self.manifest['modules']['401'] = self.manifest['modules']['400']
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('exactly selected modules 168 and 400', result.stderr)

    def test_translation_not_bound_to_capture_refused(self):
        path = self.root / 'rels_out/d_a_test_400/chunks/chunk_C1E000DC.c'
        path.write_text('different translation\n')
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('producer translation identity mismatch', result.stderr)

    def test_canonical_mismatch_invalidates_stale_receipt(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        path = self.root / 'title.ship-code-input'
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        self.manifest['modules']['400']['capture_sha256'] = hashlib.sha256(data).hexdigest()
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('emitter input', result.stderr)
        self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_selected_generation_is_deterministic(self):
        self.assertEqual(self.generate().returncode, 0)
        output = self.root / 'composite'
        before = {str(p.relative_to(output)): p.read_bytes() for p in output.rglob('*') if p.is_file()}
        self.assertEqual(self.generate().returncode, 0)
        after = {str(p.relative_to(output)): p.read_bytes() for p in output.rglob('*') if p.is_file()}
        self.assertEqual(before, after)

    def test_legacy_ship_certificate_bytes_preserved(self):
        fixture.run_generator(self.root)
        before = (self.root / 'composite/rel_data.inc').read_text()
        self.assertEqual(self.generate().returncode, 0)
        after = (self.root / 'composite/rel_data.inc').read_text()
        legacy = before[before.index('#define MODULE_SHIP_CONTRACT_PRESENT'):]
        self.assertIn(legacy, after)

    def test_selected_arrays_are_not_duplicated_for_legacy_view(self):
        self.assertEqual(self.generate().returncode, 0)
        text = (self.root / 'composite/rel_data.inc').read_text()
        self.assertEqual(text.count('static const u8 s_ship_raw_dol[]'), 1)
        self.assertEqual(text.count('static const u8 s_ship_raw_rel[]'), 1)
        self.assertEqual(text.count('static const u8 s_ship_canonical_1[]'), 1)
        self.assertNotIn('s_title_raw_dol', text)

    def test_title_canonical_data_reuses_normal_exports(self):
        self.assertEqual(self.generate().returncode, 0)
        text = (self.root / 'composite/rel_data.inc').read_text()
        self.assertNotIn('static const u8 s_title_canonical_2[]', text)
        self.assertIn('400u,2u,0xC1E01F28u,4u,s_rel_data_400_2', text)

    def test_legacy_getter_is_a_view_of_the_selected_ship(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        compile_export_probe(self.root,
            'const BlueWakeShipContractV1* old = staticrecomp_get_ship_contract_v1();\n'
            'const BlueWakeRelInitContractSetV1* set = staticrecomp_get_rel_init_contracts_v1();\n'
            'assert(set && set->version == 1u && set->contract_count == 2u);\n'
            'const BlueWakeRelInitContractV1* ship = &set->contracts[0];\n'
            'assert(ship->module_id == 168u);\n'
            'assert(old->dol_bytes == set->dol_bytes && old->dol_size == set->dol_size);\n'
            'assert(old->rel_bytes == ship->rel_bytes && old->rel_size == ship->rel_size);\n'
            'assert(old->canonical_sections == ship->canonical_sections);\n'
            'assert(old->section_count == ship->section_count);\n'
            'assert(strcmp(old->build_identity, ship->build_identity) == 0);\n'
            'assert(ship->lifecycle->prolog_address == 0xC1B100F4u);\n'
            'assert(set->contracts[1].module_id == 400u);\n'
            'assert(set->contracts[1].lifecycle->unresolved_address == 0xC1E00134u);\n'
            'assert(set->contracts[1].ctor_table_section == 2u);\n'
            'assert(set->contracts[1].ctor_return_pc == 0xC1E000F4u);\n')

    def test_default_generation_has_no_selected_set(self):
        fixture.run_generator(self.root)
        text = (self.root / 'composite/rel_data.inc').read_text()
        self.assertNotIn('MODULE_REL_INIT_CONTRACTS_PRESENT', text)

    def test_old_generated_module_getter_returns_no_selected_set(self):
        fixture.run_generator(self.root)
        compile_export_probe(self.root, 'assert(staticrecomp_get_rel_init_contracts_v1() == NULL);')

    def test_cpp_normal_header_caller_links_to_c_defined_getter(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        compile_export_probe(self.root,
            'const auto* set = staticrecomp_get_rel_init_contracts_v1();\n'
            'assert(set && set->version == 1u && set->contract_count == 2u);', cpp=True)

    def test_cpp_wrapped_header_caller_links_to_c_defined_getter(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        compile_export_probe(self.root,
            'assert(staticrecomp_get_rel_init_contracts_v1() != nullptr);', cpp=True, wrapped=True)

    def test_original_title_pin_rejects_processed_identity(self):
        self.manifest['modules']['400']['raw_sha256'] = self.manifest['modules']['400']['processed_rel_sha256']
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('title disc-member identity SHA-256 mismatch', result.stderr)

    def test_capture_hash_mismatch_refused(self):
        self.manifest['modules']['400']['capture_sha256'] = 'b' * 64
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('producer capture identity mismatch', result.stderr)

    def test_producer_raw_identity_mismatch_refused(self):
        self.manifest['modules']['168']['raw_sha256'] = 'b' * 64
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('producer REL identity mismatch', result.stderr)

    def test_producer_dol_identity_mismatch_refused(self):
        self.manifest['dol_sha256'] = 'b' * 64
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('producer DOL identity mismatch', result.stderr)

    def test_ctor_successor_requires_walker_call(self):
        for suffix in ('rels_bin/0400.rel', 'input/title-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 8752, fixture.DOL_TEXT_ADDR)
            path.write_bytes(data)
        processed = (self.root / 'rels_bin/0400.rel').read_bytes()
        sections = gc.rel_linked_sections(processed, TITLE_START - 220)
        canonical = sc.relocate_ship(processed, sections, DOL_RANGES, module_id=400)
        path = self.root / 'title.ship-code-input'
        sidecar = path.read_bytes()[:28] + canonical[1]
        path.write_bytes(sidecar)
        with self.assertRaisesRegex(ValueError, 'constructor successor'):
            self.contract()

    def test_title_empty_constructor_terminator_tamper_refused(self):
        for suffix in ('rels_bin/0400.rel', 'input/title-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 7976, TITLE_START)
            path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'ctors requires'):
            self.contract()


class IkadaContracts(unittest.TestCase):
    def setUp(self):
        SelectedContracts.setUp(self)
        ikada_fixture(self.root)
        self.manifest['modules']['60'] = capture_binding(
            self.root, 60, 'ikada', IKADA_START, 244, b'IKADA60\0')
        self.ikada_pin = self.manifest['modules']['60']['raw_sha256']

    def generate(self, extra_args=(), omit=()):
        return SelectedContracts.generate(self, ('--ikada-rel-sha256', self.ikada_pin, *extra_args), omit)

    tearDown = SelectedContracts.tearDown
    contract = SelectedContracts.contract

    def test_triple_exports_exact_sorted_members_and_both_canonical_chunk_hashes(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        output = self.root / 'composite'
        receipt = json.loads((output / 'generation_receipt.json').read_text())['selected_rel_contracts']
        self.assertEqual(set(receipt['contracts']), {'60', '168', '400'})
        text = (output / 'rel_data.inc').read_text()
        self.assertIn('60u,33184u,s_ikada_raw_rel,s_ikada_canonical,5u,2u,0u,0xC155010Cu,&s_rel_lifecycle_v2[0]', text)
        for index in range(2, 6):
            self.assertNotIn(f'static const u8 s_ikada_canonical_{index}[]', text)
            self.assertIn(f's_rel_data_60_{index}', text)
        self.assertEqual(text.count('static const u8 s_ship_raw_dol[]'), 1)
        canonical = (self.root / 'ikada.ship-code-input').read_bytes()[28:]
        table = (output / 'module_tables.inc').read_text()
        for part in (canonical[:16384], canonical[16384:]):
            self.assertIn(f'0x{gc.fnv1a64(part):016X}ull', table)

    def test_triple_preserves_pair_contract_identities(self):
        self.assertEqual(self.generate().returncode, 0)
        path = self.root / 'composite/generation_receipt.json'
        triple = json.loads(path.read_text())['selected_rel_contracts']['contracts']
        del self.manifest['modules']['60']
        result = SelectedContracts.generate(self)
        self.assertEqual(result.returncode, 0, result.stderr)
        pair = json.loads(path.read_text())['selected_rel_contracts']['contracts']
        self.assertEqual(pair, {mid: triple[mid] for mid in ('168', '400')})

    def test_nonempty_ikada_constructor_requires_owned_target_and_walker_successor(self):
        _, fields, _ = self.contract(60)
        self.assertEqual(fields['ctors'], 0xC1555950)
        path = self.root / 'ikada.ship-code-input'
        data = bytearray(path.read_bytes())
        data[48:52] = b'\x60\0\0\0'
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'emitter input'):
            self.contract(60)

    def test_ikada_raw_alignment_is_exactly_eight(self):
        path = self.root / 'input/ikada-disc.rel'
        data = bytearray(path.read_bytes())
        struct.pack_into('>I', data, 0x40, 4)
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'original disc alignment 8'):
            self.contract(60)

    def test_ikada_bss_shape_cannot_expand_the_approved_specification(self):
        for suffix in ('rels_bin/0060.rel', 'input/ikada-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x20, 540)
            struct.pack_into('>I', data, 0x80, 540)
            path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'ikada contract approved section/BSS layout'):
            self.contract(60)

    def test_ikada_section_table_or_executable_boundary_mutation_refuses(self):
        for offset, value in ((0x58, IKADA_SIZE - 4), (0x5C, 0x5954)):
            with self.subTest(offset=offset):
                path = self.root / 'rels_bin/0060.rel'
                original = path.read_bytes()
                data = bytearray(original)
                struct.pack_into('>I', data, offset, value)
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.contract(60)
                path.write_bytes(original)

    def test_each_ikada_capture_corruption_refuses_and_removes_success_receipt(self):
        path = self.root / 'ikada.ship-code-input'
        original = path.read_bytes()
        for label, offset, value in (('magic', 0, 0), ('count', 11, 2), ('section', 15, 2),
                                      ('offset', 19, 248), ('address', 23, 248), ('size', 27, 88),
                                      ('payload', 100, original[100] ^ 1), ('trailing', None, 0)):
            with self.subTest(label=label):
                self.assertEqual(self.generate().returncode, 0)
                data = bytearray(original)
                if offset is None:
                    data.append(value)
                else:
                    data[offset] = value
                path.write_bytes(data)
                self.manifest['modules']['60']['capture_sha256'] = hashlib.sha256(data).hexdigest()
                result = self.generate()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())
                path.write_bytes(original)
                self.manifest['modules']['60']['capture_sha256'] = hashlib.sha256(original).hexdigest()

    def test_ikada_provenance_requires_exact_triple_and_complete_bindings(self):
        original = json.dumps(self.manifest)
        for field in ('missing-module', 'extra-module', 'dol', 'raw', 'processed', 'translation',
                      'translation-path', 'capture-pin', 'version', 'producer-identity'):
            with self.subTest(field=field):
                self.manifest = json.loads(original)
                item = self.manifest['modules']['60']
                if field == 'missing-module': del self.manifest['modules']['60']
                elif field == 'extra-module': self.manifest['modules']['61'] = item
                elif field == 'dol': self.manifest['dol_sha256'] = 'b' * 64
                elif field == 'raw': item['raw_sha256'] = 'b' * 64
                elif field == 'processed': item['processed_rel_sha256'] = 'b' * 64
                elif field == 'translation': item['translation_sha256'].pop(next(iter(item['translation_sha256'])))
                elif field == 'translation-path': item['translation_sha256']['../foreign.c'] = 'b' * 64
                elif field == 'capture-pin': item['capture_sha256'] = 'b' * 64
                elif field == 'version': self.manifest['version'] = 2
                else: self.manifest['producer_identity'] = 'untrusted'
                result = self.generate()
                self.assertNotEqual(result.returncode, 0, field)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_duplicate_provenance_module_is_not_silently_accepted(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        path = self.root / 'provenance.json'
        text = path.read_text().replace('"60":', '"60": {}, "60":', 1)
        path.write_text(text)
        result = subprocess.run(result.args, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('duplicate producer provenance key: 60', result.stderr)
        self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_ikada_selection_flags_cannot_imply_prerequisites(self):
        for omit in (('--selected-rel-provenance',), ('--title-rel-sha256',),
                     ('--selected-rel-provenance', '--title-rel-sha256')):
            with self.subTest(omit=omit):
                result = self.generate(omit=omit)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('ikada contract requires', result.stderr)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_empty_ikada_pin_is_not_a_request_for_the_old_pair(self):
        del self.manifest['modules']['60']
        result = SelectedContracts.generate(self, ('--ikada-rel-sha256', ''))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_ikada_archive_member_must_be_unique_by_exact_basename(self):
        raw = (self.root / 'input/ikada-disc.rel').read_bytes()
        disc = fixture.write_ship_disc(self.root, [('d_a_obj_ikada.rel', raw), ('nested/d_a_obj_ikada.rel', raw)])
        with self.assertRaisesRegex(ValueError, 'exactly one disc archive member d_a_obj_ikada.rel'):
            sc.read_disc_member(disc, 60)

    def test_ikada_processed_identity_is_not_the_original_pin(self):
        result = self.generate(('--ikada-rel-sha256', self.manifest['modules']['60']['processed_rel_sha256']))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ikada disc-member identity SHA-256 mismatch', result.stderr)
        self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_triple_getter_preserves_c_and_cpp_abi_and_common_dol(self):
        self.assertEqual(self.generate().returncode, 0)
        for cpp, wrapped in ((False, False), (True, False), (True, True)):
            with self.subTest(cpp=cpp, wrapped=wrapped):
                compile_export_probe(self.root,
                    'const BlueWakeRelInitContractSetV1* set = staticrecomp_get_rel_init_contracts_v1();\n'
                    'assert(set && set->contract_count == 3u);\n'
                    'assert(set->contracts[0].module_id == 60u && set->contracts[1].module_id == 168u && set->contracts[2].module_id == 400u);\n'
                    'assert(set->contracts[0].lifecycle->module_id == 60u);\n' +
                    ('assert(set->dol_bytes == staticrecomp_get_ship_contract_v1()->dol_bytes);' if not cpp else
                     'assert(set->dol_bytes && set->dol_size);'), cpp=cpp, wrapped=wrapped)


class ActorBatchContracts(unittest.TestCase):
    def setUp(self):
        IkadaContracts.setUp(self)
        wood_fixture(self.root)
        self.manifest['modules']['84'] = capture_binding(
            self.root, 84, 'wood', 0xC19A00D4, 0xD4, b'WOOD84\0\0')
        (self.root / 'complete.json').write_text(json.dumps({'version': 1, 'batch': 'opening-a',
            'modules': [{'id': 84, 'capture': 'wood.ship-code-input', 'size': 1192}]}))

    def generate(self, extra_args=(), omit=()):
        return IkadaContracts.generate(self, ('--actor-rel-batch', 'opening-a', *extra_args), omit)

    tearDown = SelectedContracts.tearDown
    contract = SelectedContracts.contract

    def test_quadruple_exports_exact_members_and_wood_canonical_hash(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        output = self.root / 'composite'
        selected = json.loads((output / 'generation_receipt.json').read_text())['selected_rel_contracts']
        self.assertEqual(set(selected['contracts']), {'60', '84', '168', '400'})
        text = (output / 'rel_data.inc').read_text()
        self.assertIn('84u,2596u,s_wood_raw_rel,s_wood_canonical,4u,2u,0u,0xC19A00ECu,&s_rel_lifecycle_v2[1]', text)
        self.assertNotIn('static const u8 s_wood_canonical_2', text)
        self.assertEqual(text.count('static const u8 s_ship_raw_dol[]'), 1)
        canonical = (self.root / 'wood.ship-code-input').read_bytes()
        self.assertEqual(len(canonical), 1192)
        self.assertIn(f'0x{gc.fnv1a64(canonical[28:]):016X}ull', (output / 'module_tables.inc').read_text())

    def test_batch_requires_explicit_old_chain_arguments(self):
        for omit in (('--selected-rel-provenance',), ('--title-rel-sha256',), ('--ikada-rel-sha256',)):
            with self.subTest(omit=omit):
                result = SelectedContracts.generate(self, ('--actor-rel-batch', 'opening-a',
                    '--ikada-rel-sha256', self.ikada_pin) if '--ikada-rel-sha256' not in omit else
                    ('--actor-rel-batch', 'opening-a'), tuple(x for x in omit if x != '--ikada-rel-sha256'))
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_wood_capture_mutations_refuse(self):
        path = self.root / 'wood.ship-code-input'
        original = path.read_bytes()
        for at in (0, 11, 15, 19, 23, 27, 48, len(original), -1):
            with self.subTest(at=at):
                data = bytearray(original)
                if at == len(original): data.append(0)
                elif at == -1: data.pop()
                else: data[at] ^= 1
                path.write_bytes(data)
                with self.assertRaises(ValueError): self.contract(84)
        path.write_bytes(original)

    def test_wood_layout_and_original_alignment_mutations_refuse(self):
        for at, value in ((0x40, 4), (0x38, 48), (0x58, 1160), (0x60, 8), (0x44, 4), (0x20, 4)):
            with self.subTest(at=at):
                path = self.root / 'input/wood-disc.rel'
                original = path.read_bytes()
                data = bytearray(original)
                struct.pack_into('>I', data, at, value)
                path.write_bytes(data)
                with self.assertRaises(ValueError): self.contract(84)
                path.write_bytes(original)

    def test_batch_provenance_requires_exact_members_and_complete_wood_binding(self):
        original = json.dumps(self.manifest)
        for field in ('missing', 'extra', 'raw', 'processed', 'capture_sha256', 'translation_sha256',
                      'translation-path', 'dol', 'producer_identity', 'version'):
            with self.subTest(field=field):
                self.manifest = json.loads(original)
                item = self.manifest['modules']['84']
                if field == 'missing': del self.manifest['modules']['84']
                elif field == 'extra': self.manifest['modules']['85'] = item
                elif field == 'raw': item['raw_sha256'] = 'b' * 64
                elif field == 'processed': item['processed_rel_sha256'] = 'b' * 64
                elif field == 'capture_sha256': item[field] = 'b' * 64
                elif field == 'translation_sha256': item[field].pop(next(iter(item[field])))
                elif field == 'translation-path': item['translation_sha256']['../foreign.c'] = 'b' * 64
                elif field == 'dol': self.manifest['dol_sha256'] = 'b' * 64
                elif field == 'producer_identity': self.manifest[field] = ''
                else: self.manifest[field] = 2
                result = self.generate()
                self.assertNotEqual(result.returncode, 0, field)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_incomplete_batch_publication_cannot_generate_a_certificate(self):
        path = self.root / 'complete.json'
        for data in (None, {}, {'version': 1, 'batch': 'opening-a', 'modules': []}):
            with self.subTest(data=data):
                if data is None: path.unlink()
                else: path.write_text(json.dumps(data))
                result = self.generate()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_wood_successor_requires_the_actual_walker_call(self):
        for suffix in ('rels_bin/0084.rel', 'input/wood-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x6D0, fixture.DOL_TEXT_ADDR)
            path.write_bytes(data)
        capture_binding(self.root, 84, 'wood', 0xC19A00D4, 0xD4, b'WOOD84\0\0')
        with self.assertRaisesRegex(ValueError, 'constructor successor'):
            self.contract(84)

    def test_unknown_or_empty_batch_does_not_fall_back_to_the_triple(self):
        for name in ('', 'all', '84', 'opening-d'):
            with self.subTest(name=name):
                result = self.generate(('--actor-rel-batch', name))
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_quadruple_getter_preserves_c_and_cpp_abi_and_common_dol(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        for cpp, wrapped in ((False, False), (True, False), (True, True)):
            with self.subTest(cpp=cpp, wrapped=wrapped):
                compile_export_probe(self.root,
                    'const BlueWakeRelInitContractSetV1* set = staticrecomp_get_rel_init_contracts_v1();\n'
                    'assert(set && set->contract_count == 4u);\n'
                    'assert(set->contracts[0].module_id == 60u && set->contracts[1].module_id == 84u);\n'
                    'assert(set->contracts[2].module_id == 168u && set->contracts[3].module_id == 400u);\n'
                    'assert(set->contracts[1].lifecycle->prolog_address == 0xC19A00D4u);\n' +
                    ('assert(set->dol_bytes == staticrecomp_get_ship_contract_v1()->dol_bytes);' if not cpp or wrapped else
                     'assert(set->dol_bytes && set->dol_size);'), cpp=cpp, wrapped=wrapped)

    @contextmanager
    def later_wave(self):
        directory = self.root / 'next-wave'
        directory.mkdir()
        capture = directory / 'fixture85.ship-code-input'
        capture.write_bytes(b'game-free capture group fixture')
        (directory / 'complete.json').write_text(json.dumps({'version': 1, 'batch': 'fixture-next',
            'modules': [{'id': 85, 'capture': capture.name, 'size': capture.stat().st_size}]}))
        self.manifest['modules']['85'] = {'capture': str(capture)}
        path = self.root / 'provenance.json'
        path.write_text(json.dumps(self.manifest))
        with patch.dict(sc.ACTOR_BATCHES, {'fixture-next': (60, 84, 85, 168, 400)}), \
             patch.dict(sc.ACTOR_CAPTURE_GROUPS, {'fixture-next': (85,)}), \
             patch.dict(sc.APPROVED_MODULES, {85: {'capture_group': 'fixture-next'}}):
            yield path

    def test_later_wave_retains_the_prior_capture_group_without_recapturing_it(self):
        with self.later_wave() as path:
            sc.verify_batch_completion(path, 'fixture-next')

    def test_later_wave_cannot_discard_an_inherited_capture_group_receipt(self):
        with self.later_wave() as path:
            (self.root / 'complete.json').unlink()
            with self.assertRaises(FileNotFoundError):
                sc.verify_batch_completion(path, 'fixture-next')


class GrassBatchContracts(unittest.TestCase):
    def setUp(self):
        ActorBatchContracts.setUp(self)
        grass_fixture(self.root)
        item = capture_binding(self.root, 19, 'grass', 0xC08D00EC, 0xEC, b'GRASS19\0')
        directory = self.root / 'grass-captures'
        directory.mkdir()
        (self.root / item['capture']).replace(directory / item['capture'])
        item['capture'] = 'grass-captures/grass.ship-code-input'
        self.manifest['modules']['19'] = item
        (directory / 'complete.json').write_text(json.dumps({'version': 1, 'batch': 'opening-b',
            'modules': [{'id': 19, 'capture': 'grass.ship-code-input', 'size': 5432}]}))
        shutil.copy2(directory / 'grass.ship-code-input', self.root / 'grass.ship-code-input')

    tearDown = SelectedContracts.tearDown
    contract = SelectedContracts.contract

    def generate(self, extra_args=(), omit=()):
        return IkadaContracts.generate(self, ('--actor-rel-batch', 'opening-b', *extra_args), omit)

    def test_cumulative_selection_exports_grass_and_every_prior_member(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        output = self.root / 'composite'
        selected = json.loads((output / 'generation_receipt.json').read_text())['selected_rel_contracts']
        self.assertEqual(set(selected['contracts']), {'19', '60', '84', '168', '400'})
        canonical = (self.root / 'grass.ship-code-input').read_bytes()
        self.assertIn(f'0x{gc.fnv1a64(canonical[28:]):016X}ull', (output / 'module_tables.inc').read_text())

    def test_unequal_ctor_dtor_tables_keep_their_independent_sizes(self):
        _, fields, _ = self.contract(19)
        self.assertEqual([(s['index'], s['size']) for s in fields['canonical_sections'] if s['index'] in (2, 3)],
                         [(2, 4), (3, 8)])

    def test_grass_layout_mutations_refuse(self):
        for offset, value in ((0x20, 1338), (0x44, 4), (0x40, 4), (0x38, 48),
                              (0x58, 5400), (0x60, 8), (0x68, 4), (0x80, 1328)):
            with self.subTest(offset=offset):
                path = self.root / 'input/grass-disc.rel'
                original = path.read_bytes()
                data = bytearray(original)
                struct.pack_into('>I', data, offset, value)
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.contract(19)
                path.write_bytes(original)

    def test_grass_capture_mutations_refuse(self):
        path = self.root / 'grass.ship-code-input'
        original = path.read_bytes()
        for at in (0, 11, 15, 19, 23, 27, 48, len(original), -1):
            with self.subTest(at=at):
                data = bytearray(original)
                if at == len(original): data.append(0)
                elif at == -1: data.pop()
                else: data[at] ^= 1
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.contract(19)

    def test_each_grass_provenance_binding_is_required(self):
        original = json.dumps(self.manifest)
        for field in ('missing', 'extra', 'raw_sha256', 'processed_rel_sha256', 'capture_sha256',
                      'translation_sha256'):
            with self.subTest(field=field):
                self.manifest = json.loads(original)
                if field == 'missing': del self.manifest['modules']['19']
                elif field == 'extra': self.manifest['modules']['20'] = self.manifest['modules']['19']
                elif field == 'translation_sha256': self.manifest['modules']['19'][field] = {}
                else: self.manifest['modules']['19'][field] = '0' * 64
                result = self.generate()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_grass_wave_requires_the_inherited_wood_completion(self):
        (self.root / 'complete.json').unlink()
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_grass_wave_rejects_duplicate_provenance(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        path = self.root / 'provenance.json'
        path.write_text(path.read_text().replace('"19":', '"19": {}, "19":', 1))
        result = subprocess.run(result.args, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('duplicate producer provenance key: 19', result.stderr)

    def test_grass_dtor_target_must_still_be_owned_executable(self):
        for suffix in ('rels_bin/0019.rel', 'input/grass-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x16CC, 5404)
            path.write_bytes(data)
        with self.assertRaises(ValueError):
            self.contract(19)

    def test_grass_dtor_table_requires_its_own_terminator(self):
        for suffix in ('rels_bin/0019.rel', 'input/grass-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x1610, 1)
            path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'dtors requires'):
            self.contract(19)

    def test_grass_successor_requires_the_actual_walker_call(self):
        for suffix in ('rels_bin/0019.rel', 'input/grass-disc.rel'):
            path = self.root / suffix
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x16E4, fixture.DOL_TEXT_ADDR)
            path.write_bytes(data)
        capture_binding(self.root, 19, 'grass', 0xC08D00EC, 0xEC, b'GRASS19\0')
        with self.assertRaisesRegex(ValueError, 'constructor successor'):
            self.contract(19)

    def test_grass_capture_group_cannot_be_missing_or_misbound(self):
        path = self.root / 'grass-captures/complete.json'
        for data in ({}, {'version': 1, 'batch': 'opening-a', 'modules': []}):
            with self.subTest(data=data):
                path.write_text(json.dumps(data))
                result = self.generate()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'composite/generation_receipt.json').exists())

    def test_grass_archive_member_must_be_unique(self):
        raw = (self.root / 'input/grass-disc.rel').read_bytes()
        disc = fixture.write_ship_disc(self.root, [('d_a_grass.rel', raw), ('nested/d_a_grass.rel', raw)])
        with self.assertRaisesRegex(ValueError, 'exactly one disc archive member d_a_grass.rel'):
            sc.read_disc_member(disc, 19)


if __name__ == '__main__':
    unittest.main()
