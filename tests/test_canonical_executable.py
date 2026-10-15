import hashlib
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import verify_canonical_executable as oracle


def fixture(kind=1, symbol=0x80018000, delta=0, word=0x48000001, module=0):
    raw = bytearray(0xD0)
    struct.pack_into('>I', raw, 0, 7)
    struct.pack_into('>II', raw, 0xC, 3, 0x4C)
    struct.pack_into('>III', raw, 0x24, 0xB0, 0xA0, 8)
    struct.pack_into('>II', raw, 0x40, 4, 8)
    struct.pack_into('>II', raw, 0x54, 0x81, 16)
    struct.pack_into('>II', raw, 0x5C, 0x90, 8)
    struct.pack_into('>I', raw, 0x80, word)
    struct.pack_into('>II', raw, 0xA0, module, 0xB0)
    struct.pack_into('>HBBI', raw, 0xB0, 0, 202, 1, 0)
    struct.pack_into('>HBBI', raw, 0xB8, delta, kind, 0, symbol)
    struct.pack_into('>HBBI', raw, 0xC0, 0, 203, 0, 0)
    sections = [dict(index=0, offset=0, size=0, executable=False, linked_start=0),
                dict(index=1, offset=0x80, size=16, executable=True, linked_start=0xC0000080),
                dict(index=2, offset=0x90, size=8, executable=False, linked_start=0xC0000090)]
    return raw, sections


class RelocationTests(unittest.TestCase):
    def replay(self, **kwargs):
        raw, sections = fixture(**kwargs)
        return oracle.relocate(bytes(raw), sections, 7, [(0x80000000, 0x81800000)])[0][1]

    def test_addr32(self):
        self.assertEqual(self.replay()[:4], bytes.fromhex('80018000'))

    def test_lo(self):
        self.assertEqual(self.replay(kind=4)[:4], bytes.fromhex('80000001'))

    def test_ha_carry(self):
        self.assertEqual(self.replay(kind=6)[:4], bytes.fromhex('80020001'))

    def test_ha_without_carry(self):
        self.assertEqual(self.replay(kind=6, symbol=0x80017FFF)[:2], bytes.fromhex('8001'))

    def test_branch_alias_and_link_bit(self):
        self.assertEqual(self.replay(kind=10, symbol=0x80000084)[:4], bytes.fromhex('48000005'))

    def test_branch_without_link(self):
        self.assertEqual(self.replay(kind=10, symbol=0x80000084, word=0x48000000)[:4],
                         bytes.fromhex('48000004'))

    def test_signed_branch_boundaries(self):
        for pc, target, expected in [(0xC2000080, 0xC0000080, '4a000001'),
                                     (0xC0000080, 0xC200007C, '49fffffd')]:
            with self.subTest(pc=pc):
                raw, sections = fixture(kind=10, symbol=0, module=7)
                raw[0xBB] = 2
                sections[1]['linked_start'] = pc
                sections[2]['linked_start'] = target
                sections[2]['executable'] = True
                struct.pack_into('>I', raw, 0x5C, 0x91)
                result, _ = oracle.relocate(raw, sections, 7, [])
                self.assertEqual(result[1][:4], bytes.fromhex(expected))

    def test_refusals(self):
        for kwargs in [dict(module=8), dict(kind=2), dict(delta=16), dict(delta=1),
                       dict(kind=10, word=0x48000002), dict(kind=10, word=0),
                       dict(kind=10, symbol=0x80000081)]:
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                self.replay(**kwargs)

    def test_unterminated_stream(self):
        raw, sections = fixture()
        raw = raw[:0xC0]
        with self.assertRaisesRegex(ValueError, 'terminat'):
            oracle.relocate(raw, sections, 7, [])

    def test_branch_outside_signed_boundaries(self):
        for pc, target in [(0xC2000084, 0xC0000080), (0xC0000080, 0xC2000080)]:
            with self.subTest(pc=pc), self.assertRaisesRegex(ValueError, 'displacement'):
                raw, sections = fixture(kind=10, symbol=0, module=7)
                raw[0xBB] = 2
                sections[1]['linked_start'] = pc
                sections[2]['linked_start'] = target
                sections[2]['executable'] = True
                struct.pack_into('>I', raw, 0x5C, 0x91)
                oracle.relocate(raw, sections, 7, [])

    def test_external_branch_requires_dol_text(self):
        raw, sections = fixture(kind=10, symbol=0x80000084)
        with self.assertRaisesRegex(ValueError, 'text'):
            oracle.relocate(raw, sections, 7, [])

    def test_missing_section_selection(self):
        raw, sections = fixture()
        struct.pack_into('>HBBI', raw, 0xB0, 0, 201, 0, 0)
        with self.assertRaisesRegex(ValueError, 'patch section'):
            oracle.relocate(raw, sections, 7, [])

    def test_invalid_section_selection(self):
        raw, sections = fixture()
        raw[0xB3] = 3
        with self.assertRaisesRegex(ValueError, 'section absent'):
            oracle.relocate(raw, sections, 7, [])

    def test_self_target_bounds(self):
        raw, sections = fixture(symbol=8, module=7)
        raw[0xBB] = 2
        with self.assertRaisesRegex(ValueError, 'target section bounds'):
            oracle.relocate(raw, sections, 7, [])

    def test_section_metadata_mismatch(self):
        raw, sections = fixture()
        sections[1]['offset'] += 4
        with self.assertRaisesRegex(ValueError, 'section metadata'):
            oracle.relocate(raw, sections, 7, [])

    def test_import_cursor_bounds(self):
        raw, sections = fixture()
        struct.pack_into('>I', raw, 0xA4, 0xAF)
        with self.assertRaisesRegex(ValueError, 'stream bounds'):
            oracle.relocate(raw, sections, 7, [])

    def test_section_delta_and_end(self):
        raw, sections = fixture(kind=201, delta=4)
        struct.pack_into('>HBBI', raw, 0xC0, 4, 4, 0, 0x80001234)
        struct.pack_into('>HBBI', raw, 0xC8, 0, 203, 0, 0)
        result, _ = oracle.relocate(raw, sections, 7, [])
        self.assertEqual(result[1], bytes.fromhex('48000001000000001234000000000000'))

    def test_self_section_target(self):
        raw, sections = fixture(symbol=4, module=7)
        raw[0xBB] = 2
        result, _ = oracle.relocate(raw, sections, 7, [])
        self.assertEqual(result[1][:4], bytes.fromhex('c0000094'))


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.row = dict(id=7, basename='sample.rel', exec=dict(index=1, offset=128,
                        linked_start=0xC0000080, size=4), capture_magic_hex=b'REL00007'.hex(),
                        translation=dict(chunk_ranges=[[0xC0000080, 0xC0000084]]))
        self.expected = b'\x80\x01\x80\x00'
        self.capture = b'REL00007' + struct.pack('>IIIII', 1, 1, 128, 0xC0000080, 4) + self.expected
        self.data = '''static const u8 s_sample_canonical_1[] = {128,1,128,0,};
static const u8 wrong[] = {0,0,0,0,};
static const BlueWakeRelData s_sample_canonical[] = {
{7u,1u,0xC0000080u,4u,s_sample_canonical_1},
};
static const BlueWakeRelInitContractV1 s_rel_init_contracts[] = {
{7u,208u,s_sample_raw_rel,s_sample_canonical,1u,2u,0u,0xC0000098u,&s_rel_lifecycle_v2[0],"pin"},
};
#define MODULE_REL_INIT_CONTRACTS_PRESENT 1
static const BlueWakeRelInitContractSetV1 s_rel_init_contract_set = {
1u,264u,s_ship_raw_dol,s_rel_init_contracts,1u,"set-pin"
};'''
        self.tables = '''static const StaticRecompRange s_chunk_ranges[] = {
{0xC0000080u, 0xC0000084u},
};
static const u64 s_chunk_hashes[] = {
0x401EFFAAB252C94Aull,
};
#define MODULE_CHUNK_RANGE_COUNT 1u'''

    def check(self):
        return oracle.bind(self.row, self.expected, self.capture, self.data, self.tables)

    def test_manual_positive(self):
        self.assertEqual(self.check()['executable_sha256'], hashlib.sha256(self.expected).hexdigest())

    def test_modified_capture(self):
        self.capture = self.capture[:-1] + b'\x01'
        with self.assertRaisesRegex(ValueError, 'capture'):
            self.check()

    def test_trailing_capture(self):
        self.capture += b'\x00'
        with self.assertRaises(ValueError):
            self.check()

    def test_malformed_capture_headers(self):
        for offset in (0, 11, 15, 19, 23, 27):
            with self.subTest(offset=offset):
                original = self.capture
                corrupt = bytearray(original)
                corrupt[offset] ^= 1
                self.capture = bytes(corrupt)
                with self.assertRaisesRegex(ValueError, 'capture'):
                    self.check()
                self.capture = original

    def test_truncated_capture(self):
        self.capture = self.capture[:-1]
        with self.assertRaisesRegex(ValueError, 'capture'):
            self.check()

    def test_wrong_array_association(self):
        self.data = self.data.replace('4u,s_sample_canonical_1}', '4u,wrong}')
        with self.assertRaisesRegex(ValueError, 'descriptor'):
            self.check()

    def test_modified_array(self):
        self.data = self.data.replace('{128,1,128,0,}', '{128,1,128,1,}')
        with self.assertRaisesRegex(ValueError, 'array'):
            self.check()

    def test_octal_canonical_array_byte_rejected(self):
        self.expected = bytes.fromhex('70018000')
        self.capture = self.capture[:-4] + self.expected
        self.data = self.data.replace('{128,1,128,0,}', '{112,1,128,0,}')
        self.tables = self.tables.replace('401EFFAAB252C94A', '41C93D0FC187DBFA')
        self.assertEqual(self.check()['executable_sha256'], hashlib.sha256(self.expected).hexdigest())
        self.data = self.data.replace('{112,1,128,0,}', '{0112,1,128,0,}')
        self.assertNotEqual(int('0112', 8), self.expected[0])
        with self.assertRaisesRegex(ValueError, 'array syntax'):
            self.check()

    def test_canonical_array_remains_decimal_only(self):
        self.data = self.data.replace('{128,1,128,0,}', '{0x80,1,128,0,}')
        with self.assertRaisesRegex(ValueError, 'array syntax'):
            self.check()

    def test_wrong_module_association(self):
        self.data = self.data.replace('s_sample_raw_rel,s_sample_canonical,', 's_sample_raw_rel,wrong,')
        with self.assertRaisesRegex(ValueError, 'association'):
            self.check()

    def test_wrong_runtime_hash(self):
        self.tables = self.tables.replace('401EFFAAB252C94A', '0000000000000000')
        with self.assertRaisesRegex(ValueError, 'hash'):
            self.check()

    def test_shift_missing_overlap_ranges(self):
        for replacement in ['{0xC0000084u, 0xC0000088u},', '',
                            '{0xC0000080u, 0xC0000084u},\n{0xC0000080u, 0xC0000084u},']:
            with self.subTest(replacement=replacement):
                original = self.tables
                self.tables = original.replace('{0xC0000080u, 0xC0000084u},', replacement)
                with self.assertRaises(ValueError):
                    self.check()
                self.tables = original

    def test_frozen_ranges_must_cover_section(self):
        self.row['translation']['chunk_ranges'] = [[0xC0000080, 0xC0000088]]
        with self.assertRaisesRegex(ValueError, 'range'):
            self.check()

    def test_frozen_source_mismatch(self):
        with self.assertRaisesRegex(ValueError, 'translation'):
            oracle.verify_translations(Path(__file__).parent,
                                       {'test_canonical_executable.py': '0' * 64})

    def test_extra_delivered_c_chunk(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'expected.c').write_text('int expected;\n')
            (root / 'extra.c').write_text('int extra;\n')
            pins = {'expected.c': hashlib.sha256((root / 'expected.c').read_bytes()).hexdigest()}
            with self.assertRaisesRegex(ValueError, 'inventory'):
                oracle.verify_translations(root, pins)

    def test_exact_delivered_c_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'expected.c').write_text('int expected;\n')
            pins = {'expected.c': hashlib.sha256((root / 'expected.c').read_bytes()).hexdigest()}
            self.assertEqual(oracle.verify_translations(root, pins), pins)

    def test_header_identity_is_not_coverage_provenance(self):
        self.assertIn('BLOCKED', oracle.coverage_status()['status'])

    def test_emitted_literal_count_defines_are_admitted(self):
        self.assertEqual(self.check()['chunks'][0]['fnv1a64'], '401EFFAAB252C94A')

    def test_zero_exported_chunk_count(self):
        self.tables = self.tables.replace('MODULE_CHUNK_RANGE_COUNT 1u', 'MODULE_CHUNK_RANGE_COUNT 0u')
        with self.assertRaisesRegex(ValueError, 'exported chunk count'):
            self.check()

    def test_truncated_exported_chunk_count(self):
        self.tables = self.tables.replace('{0xC0000080u, 0xC0000084u},',
                                          '{0x80003100u, 0x80003104u},\n{0xC0000080u, 0xC0000084u},')
        self.tables = self.tables.replace('0x401EFFAAB252C94Aull,',
                                          '0x0000000000000000ull,\n0x401EFFAAB252C94Aull,')
        with self.assertRaisesRegex(ValueError, 'exported chunk count'):
            self.check()

    def test_excessive_exported_chunk_count(self):
        self.tables = self.tables.replace('MODULE_CHUNK_RANGE_COUNT 1u', 'MODULE_CHUNK_RANGE_COUNT 2u')
        with self.assertRaisesRegex(ValueError, 'exported chunk count'):
            self.check()

    def test_missing_exported_chunk_count(self):
        self.tables = self.tables.replace('#define MODULE_CHUNK_RANGE_COUNT 1u', '')
        with self.assertRaisesRegex(ValueError, 'MODULE_CHUNK_RANGE_COUNT'):
            self.check()

    def test_duplicate_exported_chunk_count(self):
        self.tables += '\n#define MODULE_CHUNK_RANGE_COUNT 1u\n'
        with self.assertRaisesRegex(ValueError, 'MODULE_CHUNK_RANGE_COUNT'):
            self.check()

    def test_nonliteral_exported_chunk_count(self):
        self.tables = self.tables.replace('MODULE_CHUNK_RANGE_COUNT 1u', 'MODULE_CHUNK_RANGE_COUNT (1u)')
        with self.assertRaisesRegex(ValueError, 'preprocessor'):
            self.check()

    def test_noncanonical_exported_chunk_count(self):
        self.tables = self.tables.replace('MODULE_CHUNK_RANGE_COUNT 1u', 'MODULE_CHUNK_RANGE_COUNT 01u')
        with self.assertRaisesRegex(ValueError, 'literal definition'):
            self.check()

    def test_duplicate_noncanonical_exported_chunk_count(self):
        self.tables += '\n#define MODULE_CHUNK_RANGE_COUNT 01u\n'
        with self.assertRaisesRegex(ValueError, 'literal definition'):
            self.check()

    def test_full_exported_chunk_count(self):
        self.tables = self.tables.replace('{0xC0000080u, 0xC0000084u},',
                                          '{0x80003100u, 0x80003104u},\n{0xC0000080u, 0xC0000084u},')
        self.tables = self.tables.replace('0x401EFFAAB252C94Aull,',
                                          '0x0000000000000000ull,\n0x401EFFAAB252C94Aull,')
        self.tables = self.tables.replace('MODULE_CHUNK_RANGE_COUNT 1u', 'MODULE_CHUNK_RANGE_COUNT 2u')
        self.assertEqual(self.check()['chunks'][0]['fnv1a64'], '401EFFAAB252C94A')

    def test_wrong_exported_contract_array(self):
        self.data = self.data.replace('s_ship_raw_dol,s_rel_init_contracts,1u,',
                                      's_ship_raw_dol,wrong,1u,')
        with self.assertRaisesRegex(ValueError, 'exported contract set'):
            self.check()

    def test_truncated_exported_contract_count(self):
        self.data = self.data.replace('static const BlueWakeRelInitContractV1 s_rel_init_contracts[] = {',
                    'static const BlueWakeRelInitContractV1 s_rel_init_contracts[] = {\n'
                    '{6u,208u,s_other_raw_rel,s_other_canonical,1u,2u,0u,0xC0000098u,'
                    '&s_rel_lifecycle_v2[1],"other-pin"},')
        with self.assertRaisesRegex(ValueError, 'exported contract set'):
            self.check()

    def test_excessive_exported_contract_count(self):
        self.data = self.data.replace('s_ship_raw_dol,s_rel_init_contracts,1u,',
                                      's_ship_raw_dol,s_rel_init_contracts,2u,')
        with self.assertRaisesRegex(ValueError, 'exported contract set'):
            self.check()

    def test_full_exported_contract_count(self):
        self.data = self.data.replace('static const BlueWakeRelInitContractV1 s_rel_init_contracts[] = {',
                    'static const BlueWakeRelInitContractV1 s_rel_init_contracts[] = {\n'
                    '{6u,208u,s_other_raw_rel,s_other_canonical,1u,2u,0u,0xC0000098u,'
                    '&s_rel_lifecycle_v2[1],"other-pin"},')
        self.data = self.data.replace('s_ship_raw_dol,s_rel_init_contracts,1u,',
                                      's_ship_raw_dol,s_rel_init_contracts,2u,')
        self.assertEqual(self.check()['chunks'][0]['fnv1a64'], '401EFFAAB252C94A')

    def test_nonliteral_exported_contract_set(self):
        self.data = self.data.replace('s_ship_raw_dol,s_rel_init_contracts,1u,',
                                      's_ship_raw_dol,s_rel_init_contracts,(1u),')
        with self.assertRaisesRegex(ValueError, 'contract set syntax'):
            self.check()

    def test_zero_exported_contract_count(self):
        self.data = self.data.replace('s_ship_raw_dol,s_rel_init_contracts,1u,',
                                      's_ship_raw_dol,s_rel_init_contracts,0u,')
        with self.assertRaisesRegex(ValueError, 'exported contract set'):
            self.check()

    def test_missing_contract_set(self):
        self.data = self.data[:self.data.index('static const BlueWakeRelInitContractSetV1')]
        with self.assertRaisesRegex(ValueError, 's_rel_init_contract_set'):
            self.check()

    def test_missing_contract_presence_marker(self):
        self.data = self.data.replace('#define MODULE_REL_INIT_CONTRACTS_PRESENT 1', '')
        with self.assertRaisesRegex(ValueError, 'MODULE_REL_INIT_CONTRACTS_PRESENT'):
            self.check()

    def test_duplicate_contract_presence_marker(self):
        self.data += '\n#define MODULE_REL_INIT_CONTRACTS_PRESENT 1\n'
        with self.assertRaisesRegex(ValueError, 'MODULE_REL_INIT_CONTRACTS_PRESENT'):
            self.check()

    def test_invalid_contract_presence_marker(self):
        self.data = self.data.replace('MODULE_REL_INIT_CONTRACTS_PRESENT 1',
                                      'MODULE_REL_INIT_CONTRACTS_PRESENT 0')
        with self.assertRaisesRegex(ValueError, 'presence marker'):
            self.check()

    def test_duplicate_contract_set(self):
        self.data += '\n' + self.data[self.data.index('static const BlueWakeRelInitContractSetV1'):]
        with self.assertRaisesRegex(ValueError, 's_rel_init_contract_set'):
            self.check()

    def test_conditional_c_is_refused(self):
        self.data = '#if 0\n' + self.data + '\n#endif\n'
        with self.assertRaisesRegex(ValueError, 'preprocessor'):
            self.check()

    def test_wrong_descriptor_module(self):
        self.data = self.data.replace('{7u,1u,', '{8u,1u,')
        with self.assertRaisesRegex(ValueError, 'descriptor'):
            self.check()

    def test_duplicate_descriptor(self):
        self.data = self.data.replace('{7u,1u,0xC0000080u,4u,s_sample_canonical_1},',
                                      '{7u,1u,0xC0000080u,4u,s_sample_canonical_1},\n' * 2)
        with self.assertRaisesRegex(ValueError, 'descriptor'):
            self.check()

    def test_receipt_cannot_override_actual_hash(self):
        receipt = {'fnv1a64': '0' * 16}
        self.tables = self.tables.replace('401EFFAAB252C94A', receipt['fnv1a64'])
        with self.assertRaisesRegex(ValueError, 'runtime hash'):
            self.check()

    def test_matching_supplemental_receipts(self):
        binding = self.check()
        contract = {'canonical_sections': [self.row['exec'] |
                    {'sha256': hashlib.sha256(self.expected).hexdigest()}]}
        oracle.verify_receipts(self.row, binding, binding['chunks'], contract)

    def test_disagreeing_supplemental_receipt(self):
        binding = self.check()
        contract = {'canonical_sections': [self.row['exec'] | {'sha256': '0' * 64}]}
        with self.assertRaisesRegex(ValueError, 'receipt'):
            oracle.verify_receipts(self.row, binding, binding['chunks'], contract)


class DolTests(unittest.TestCase):
    def test_text_not_data(self):
        dol = bytearray(0x108)
        struct.pack_into('>II', dol, 0, 0x100, 0)
        struct.pack_into('>I', dol, 7 * 4, 0x104)
        struct.pack_into('>I', dol, 0x48, 0x80003100)
        struct.pack_into('>I', dol, 0x48 + 7 * 4, 0x80004000)
        struct.pack_into('>I', dol, 0x90, 4)
        struct.pack_into('>I', dol, 0x90 + 7 * 4, 4)
        self.assertEqual(oracle.dol_text_sections(dol), [(0x80003100, 0x80003104)])

    def test_dol_file_bounds(self):
        dol = bytearray(0x100)
        struct.pack_into('>I', dol, 0, 0x100)
        struct.pack_into('>I', dol, 0x48, 0x80003100)
        struct.pack_into('>I', dol, 0x90, 4)
        with self.assertRaisesRegex(ValueError, 'DOL executable bounds'):
            oracle.dol_text_sections(dol)


if __name__ == '__main__':
    unittest.main()
