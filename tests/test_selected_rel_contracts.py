import hashlib
import json
import shutil
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import test_composite as fixture

gc = fixture.gc
sc = gc.ship_contract
TITLE_START = 0xC1E000DC
TITLE_SIZE = 7756
DOL_RANGES = [(fixture.DOL_TEXT_ADDR, fixture.DOL_TEXT_ADDR + 128), (0x80241178, 0x8024117C)]


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
        raw = (root / f'rels_bin/{mid:04d}.rel').read_bytes()
        sections = gc.rel_linked_sections(raw, start - (244 if mid == 168 else 220))
        canonical = sc.relocate_ship(raw, sections, DOL_RANGES, module_id=mid)
        sidecar = bytearray(b'SHIP168\0' if mid == 168 else b'TITLE400')
        executable = [s for s in sections if s['executable'] and s['size']]
        sidecar += struct.pack('>I', len(executable))
        for s in executable:
            sidecar += struct.pack('>IIII', s['index'], s['offset'], s['linked_start'], s['size']) + canonical[s['index']]
        path = root / f'{prefix}.ship-code-input'
        path.write_bytes(sidecar)
        translated = root / f'rels_out/d_a_test_{mid}'
        inputs[str(mid)] = {'raw_sha256': hashlib.sha256((root / f'input/{prefix}-disc.rel').read_bytes()).hexdigest(),
            'processed_rel_sha256': hashlib.sha256(raw).hexdigest(), 'capture': str(path),
            'capture_sha256': hashlib.sha256(sidecar).hexdigest(),
            'translation_sha256': {str(p.relative_to(translated)): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in sorted(translated.rglob('*')) if p.is_file()}}
    manifest = {'version': 1, 'producer_identity': 'a' * 64,
                'dol_sha256': hashlib.sha256((root / 'input/main.dol').read_bytes()).hexdigest(),
                'modules': inputs}
    (root / 'provenance.json').write_text(json.dumps(manifest))
    return manifest


def compile_export_probe(root, body):
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
        'int main(void) {\n' + body + '\nreturn 0;\n}\n')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-const-variable',
        '-I', str(root), '-I', str(root / 'composite'), '-I', str(fixture.SCRIPTS_DIR.parent / 'cmake/composite'),
        str(source), '-o', str(root / 'view')], check=True, capture_output=True, text=True)
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
        prefix, start, offset, size = ('title', TITLE_START, 220, TITLE_SIZE) if mid == 400 else (
            'ship', fixture.SHIP_START, 244, fixture.SHIP_SIZE)
        sections = gc.rel_linked_sections(processed, start - offset)
        gc.apply_rel_data_relocations({mid: processed}, {mid: {'base': start - offset, 'sections': sections}})
        raw = (root / f'input/{prefix}-disc.rel').read_bytes()
        return sc.normal_contract((root / 'input/main.dol').read_bytes(), processed, sections,
            [(start, start + size)], DOL_RANGES, raw,
            module_id=mid, canonical_input=(root / f'{prefix}.ship-code-input').read_bytes())

    def generate(self):
        root = self.root
        disc = fixture.write_ship_disc(root)
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
        return subprocess.run(command, capture_output=True, text=True)

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


if __name__ == '__main__':
    unittest.main()
