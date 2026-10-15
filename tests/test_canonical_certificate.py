import copy
import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import verify_canonical_executable as oracle
from test_canonical_executable import fixture
import test_canonical_inherited


def public_certificate():
    raw, sections = fixture()
    raw[0x30:0x33] = bytes([1, 1, 1])
    struct.pack_into('>III', raw, 0x34, 0, 4, 8)
    struct.pack_into('>I', raw, 0x90, 0xC0000080)
    processed = bytes(raw)
    raw = bytearray(raw)
    struct.pack_into('>I', raw, 0x40, 8)
    row = dict(id=7, sections=sections, exec=dict(index=1, offset=128,
               linked_start=0xC0000080, size=16),
               translation={'chunk_ranges': [[0xC0000080, 0xC0000090]]},
               ctor={'section_index': 2, 'table_offset': 0, 'successor': 0xC0000088},
               dtor={'section_index': 2, 'table_offset': 0})
    digest = lambda data: hashlib.sha256(data).hexdigest()
    expected = dict(version=1, module_id=7, dol_size=10, dol_sha256=digest(b'public DOL'),
                    rel_size=208, rel_sha256=digest(raw), processed_rel_sha256=digest(processed),
                    section_alignment={'identity': 8, 'processed': 4}, exec=row['exec'].copy(),
                    lifecycle={'prolog': 0xC0000080, 'epilog': 0xC0000084, 'unresolved': 0xC0000088},
                    ctors=0xC0000090, dtors=0xC0000090, chunks=[[0xC0000080, 0xC0000090]],
                    section_count=2, canonical_sections=[
                        dict(index=1, offset=128, linked_start=0xC0000080, size=16,
                             sha256=digest(bytes.fromhex('80018000') + bytes(12)),
                             raw_sha256=digest(bytes.fromhex('48000001') + bytes(12))),
                        dict(index=2, offset=144, linked_start=0xC0000090, size=8,
                             sha256=digest(bytes.fromhex('c000008000000000')),
                             raw_sha256=digest(bytes.fromhex('c000008000000000')))])
    return row, bytes(raw), processed, expected


class CertificateTests(unittest.TestCase):
    def test_complete_independent_preimage(self):
        row, raw, processed, expected = public_certificate()
        canonical, _ = oracle.relocate(raw, row['sections'], 7, [])
        self.assertEqual(oracle.certificate_preimage(row, b'public DOL', raw, processed, canonical), expected)

    def test_public_canonical_json_hash(self):
        _, _, _, expected = public_certificate()
        self.assertEqual(oracle.certificate_identity(expected),
                         'f837d6dc453f1bf9b196b7597079062916f0c168d1fcb8ad9d3e84993ac3c130')

    def test_matching_delivery_mutations_do_not_rescue_preimage(self):
        _, _, _, expected = public_certificate()
        for field in expected:
            with self.subTest(field=field):
                changed = copy.deepcopy(expected)
                changed[field] = None
                receipt = changed | {'build_identity': oracle.certificate_identity(changed), 'sha256': 'a' * 64}
                with self.assertRaisesRegex(ValueError, 'preimage'):
                    oracle.compare_certificate_receipt(expected, receipt)

    def test_missing_preimage_field(self):
        _, _, _, expected = public_certificate()
        for field in expected:
            with self.subTest(field=field):
                changed = expected.copy()
                del changed[field]
                with self.assertRaisesRegex(ValueError, 'preimage'):
                    oracle.compare_certificate_receipt(expected, changed)

    def test_wrong_identity(self):
        _, _, _, expected = public_certificate()
        with self.assertRaisesRegex(ValueError, 'identity'):
            oracle.compare_certificate_receipt(expected, expected | {'build_identity': '0' * 64, 'sha256': 'a' * 64})

    def test_boolean_is_not_integer_certificate_field(self):
        _, _, _, expected = public_certificate()
        changed = expected | {'version': True, 'build_identity': oracle.certificate_identity(expected),
                              'sha256': 'a' * 64}
        with self.assertRaisesRegex(ValueError, 'preimage'):
            oracle.compare_certificate_receipt(expected, changed)

    def test_unsupported_preimage_is_blocked(self):
        row, raw, processed, _ = public_certificate()
        row['ctor']['table_offset'] = 4
        canonical, _ = oracle.relocate(raw, row['sections'], 7, [])
        with self.assertRaises(oracle.UnqualifiedCoverage):
            oracle.certificate_preimage(row, b'public DOL', raw, processed, canonical)


class ExportBindingTests(unittest.TestCase):
    def setUp(self):
        self.row, self.raw, self.processed, self.expected = public_certificate()
        self.pin = 'f837d6dc453f1bf9b196b7597079062916f0c168d1fcb8ad9d3e84993ac3c130'
        self.contract = ('{7u,208u,s_public_raw_rel,s_public_canonical,2u,2u,0u,0xC0000088u,'
                         '&s_rel_lifecycle_v2[0],"' + self.pin + '"},\n')
        self.policy = '{7u,"' + self.pin + '",true,WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO},\n'
        self.receipt = self.expected | {'build_identity': self.pin, 'sha256': 'a' * 64}
        self.contracts = {'7': self.receipt}
        self.data = 'static const BlueWakeRelInitContractV1 s_rel_init_contracts[] = {\n' + self.contract + '};\n'
        self.canonical = {1: bytes.fromhex('80018000') + bytes(12),
                          2: bytes.fromhex('c000008000000000')}
        arrays = [('s_public_raw_rel', self.raw), ('s_public_canonical_1', self.canonical[1])]
        fragment = ''
        for name, data in arrays:
            fragment += f'static const u8 {name}[] = {{\n'
            fragment += ''.join(','.join(str(b) for b in data[at:at+32]) + ',\n'
                                for at in range(0, len(data), 32))
            fragment += '};\n'
        fragment += ('static const BlueWakeRelData s_public_canonical[] = {\n'
                     '{7u,1u,0xC0000080u,16u,s_public_canonical_1},\n'
                     '{7u,2u,0xC0000090u,8u,s_rel_data_7_2},\n};\n')
        self.receipt['sha256'] = hashlib.sha256(fragment.encode()).hexdigest()
        self.data += fragment
        for name, data in [('s_ship_raw_dol', b'public DOL'), ('s_rel_data_7_2', self.canonical[2])]:
            self.data += 'static const u8 ' + name + '[] = {\n' + ','.join(map(str, data)) + ',\n};\n'
        self.data += ('static const BlueWakeRelLifecycleV2 s_rel_lifecycle_v2[] = {\n'
                      '{7u,0xC0000080u,0xC0000084u,0xC0000088u},\n};\n')
        self.data += ('static const BlueWakeRelInitContractSetV1 s_rel_init_contract_set = {\n'
                      '1u,10u,s_ship_raw_dol,s_rel_init_contracts,1u,"' + 'b' * 64 + '"\n};\n')

    def fullset(self):
        return oracle.bind_full_contract_set(self.data, self.policy, self.contracts, (7,))

    def check(self):
        import ship_contract
        actual, policy = self.fullset()
        with patch.object(ship_contract, 'selected_module', return_value={'name': 'public'}):
            return oracle.bind_certificate(self.row, self.expected, self.raw, b'public DOL',
                self.canonical, self.data, self.receipt, actual, policy)

    def test_independently_specified_export(self):
        self.assertEqual(self.check()['build_identity'], self.pin)

    def test_consistently_changed_identity_and_preimage(self):
        changed = self.expected | {'dol_size': 11}
        wrong = hashlib.sha256(json.dumps(changed, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        self.receipt.update(changed | {'build_identity': wrong})
        self.data = self.data.replace(self.pin, wrong)
        self.policy = self.policy.replace(self.pin, wrong)
        with self.assertRaisesRegex(ValueError, 'preimage'):
            self.check()

    def test_wrong_exported_certificate_field(self):
        for before, after in [('7u,208u', '7u,209u'), ('2u,2u,0u', '3u,2u,0u'),
                              ('0xC0000088u,&', '0xC000008Cu,&'), ('v2[0]', 'v2[1]')]:
            with self.subTest(field=before):
                original = self.data
                self.data = original.replace(before, after)
                with self.assertRaises(ValueError):
                    self.check()
                self.data = original

    def test_wrong_canonical_data(self):
        self.data = self.data.replace('s_rel_data_7_2[] = {\n192,0,0,128,',
                                      's_rel_data_7_2[] = {\n192,0,0,132,')
        with self.assertRaisesRegex(ValueError, 'canonical section'):
            self.check()

    def test_literal_hex_canonical_data(self):
        self.data = self.data.replace('s_rel_data_7_2[] = {\n192,0,0,128,0,0,0,0,',
            's_rel_data_7_2[] = {\n0xc0,0x00,0x00,0x80,0x00,0x00,0x00,0x00,')
        self.assertEqual(self.check()['build_identity'], self.pin)

    def test_literal_unsigned_hex_canonical_data(self):
        self.data = self.data.replace('s_rel_data_7_2[] = {\n192,0,0,128,0,0,0,0,',
            's_rel_data_7_2[] = {\n0xC0u,0x00u,0x00u,0x80u,0x00u,0x00u,0x00u,0x00u,')
        self.assertEqual(self.check()['build_identity'], self.pin)

    def test_wrong_emitted_text_hash(self):
        self.receipt['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'emitted text'):
            self.check()

    def test_wrong_raw_rel_array(self):
        self.data = self.data.replace('s_public_raw_rel[] = {\n0,0,0,7,',
                                      's_public_raw_rel[] = {\n0,0,0,8,')
        with self.assertRaisesRegex(ValueError, 'raw identity'):
            self.check()

    def test_wrong_raw_dol_array(self):
        self.data = self.data.replace('112,117,98,108,105,99,32,68,79,76,',
                                      '112,117,98,108,105,99,32,68,79,77,')
        with self.assertRaisesRegex(ValueError, 'raw identity'):
            self.check()

    def test_octal_raw_dol_byte_rejected(self):
        original = 's_ship_raw_dol[] = {\n112,117,98,108,105,99,32,68,79,76,'
        mutated = 's_ship_raw_dol[] = {\n0112,117,98,108,105,99,32,68,79,76,'
        self.assertEqual(self.data.count(original), 1)
        self.data = self.data.replace(original, mutated)
        self.assertNotEqual(int('0112', 8), ord('p'))
        with self.assertRaisesRegex(ValueError, 'array syntax'):
            self.check()

    def test_wrong_exported_set_dol_size(self):
        self.data = self.data.replace('1u,10u,s_ship_raw_dol', '1u,11u,s_ship_raw_dol')
        with self.assertRaisesRegex(ValueError, 'set DOL'):
            self.check()

    def test_missing_canonical_data_descriptor(self):
        self.data = self.data.replace('{7u,2u,0xC0000090u,8u,s_rel_data_7_2},\n', '')
        with self.assertRaisesRegex(ValueError, 'descriptor'):
            self.check()

    def test_wrong_lifecycle_field(self):
        self.data = self.data.replace('0xC0000084u,0xC0000088u', '0xC0000080u,0xC0000088u')
        with self.assertRaisesRegex(ValueError, 'lifecycle'):
            self.check()

    def test_unexpected_nonactor(self):
        self.policy = self.policy.replace('true,', 'false,')
        with self.assertRaisesRegex(ValueError, 'nonactor'):
            self.fullset()

    def test_duplicate_actual_id(self):
        self.data = self.data.replace(self.contract, self.contract * 2)
        with self.assertRaisesRegex(ValueError, 'fullset'):
            self.fullset()

    def test_extra_actual_id(self):
        self.data = self.data.replace(self.contract, self.contract + self.contract.replace('{7u,', '{8u,'))
        with self.assertRaisesRegex(ValueError, 'fullset'):
            self.fullset()

    def test_missing_actual_id(self):
        self.data = self.data.replace(self.contract, '')
        with self.assertRaisesRegex(ValueError, 'fullset'):
            self.fullset()

    def test_malformed_actual_id(self):
        self.data = self.data.replace('{7u,208u', '{(7u),208u')
        with self.assertRaisesRegex(ValueError, 'syntax'):
            self.fullset()

    def test_duplicate_policy_id(self):
        self.policy *= 2
        with self.assertRaisesRegex(ValueError, 'fullset'):
            self.fullset()

    def test_missing_policy_id(self):
        self.policy = ''
        with self.assertRaisesRegex(ValueError, 'fullset'):
            self.fullset()

    def test_extra_policy_id(self):
        self.policy += self.policy.replace('{7u,', '{8u,')
        with self.assertRaisesRegex(ValueError, 'fullset'):
            self.fullset()

    def test_malformed_policy_id(self):
        self.policy = self.policy.replace('{7u,', '{07u,')
        with self.assertRaises(ValueError):
            self.fullset()

    def test_unselected_row_cannot_escape_fullset(self):
        other = self.contract.replace('{7u,', '{8u,')
        self.data = self.data.replace(self.contract, self.contract + other)
        self.policy += self.policy.replace('{7u,', '{9u,')
        self.contracts['8'] = self.receipt.copy()
        with self.assertRaisesRegex(ValueError, 'fullset'):
            oracle.bind_full_contract_set(self.data, self.policy, self.contracts, (7, 8))

    def test_receipt_extra_id(self):
        self.contracts['8'] = self.receipt.copy()
        with self.assertRaisesRegex(ValueError, 'fullset'):
            self.fullset()

    def test_wrong_policy_form(self):
        self.policy = self.policy.replace('CERTIFIED_INLINE_ZERO', 'OWNER_PUBLISHED')
        with self.assertRaisesRegex(ValueError, 'input form'):
            self.check()


class LiteralByteTests(unittest.TestCase):
    def test_canonical_decimal_bytes(self):
        for token, expected in [('0', 0), ('1', 1), ('74', 74), ('112', 112), ('255', 255)]:
            with self.subTest(token=token):
                text = 'static const u8 public_bytes[] = {\n' + token + ',\n};'
                self.assertEqual(oracle.literal_bytes(text, 'public_bytes'), bytes([expected]))

    def test_bounded_hex_bytes(self):
        for token, expected in [('0x00', 0), ('0x00u', 0), ('0x4A', 74), ('0x70u', 112),
                                ('0xff', 255), ('0xFFu', 255)]:
            with self.subTest(token=token):
                text = 'static const u8 public_bytes[] = {\n' + token + ',\n};'
                self.assertEqual(oracle.literal_bytes(text, 'public_bytes'), bytes([expected]))

    def test_leading_zero_decimal_spellings_rejected(self):
        for token in ('00', '01', '0112', '000112', '08', '09', '000'):
            with self.subTest(token=token):
                text = 'static const u8 public_bytes[] = {\n' + token + ',\n};'
                with self.assertRaisesRegex(ValueError, 'array syntax'):
                    oracle.literal_bytes(text, 'public_bytes')

    def test_decimal_byte_range_guard(self):
        for token in ('256', '999'):
            with self.subTest(token=token):
                text = 'static const u8 public_bytes[] = {\n' + token + ',\n};'
                with self.assertRaisesRegex(ValueError, 'array byte bounds'):
                    oracle.literal_bytes(text, 'public_bytes')


class SharedSetupTests(unittest.TestCase):
    def setUp(self):
        fixture = test_canonical_inherited.InheritedPinTests()
        fixture.setUp()
        self.current, self.prior = fixture.current, fixture.prior
        self.policy, self.base = fixture.policy, fixture.base_policy

    def test_each_of_seven_prior_pins_required(self):
        for mid in ('9', '19', '60', '81', '84', '168', '400'):
            with self.subTest(mid=mid):
                prior = copy.deepcopy(self.prior)
                prior['selected_rel_contracts']['contracts'][mid]['build_identity'] = '0' * 64
                with self.assertRaisesRegex(ValueError, 'inherited pin'):
                    oracle.inherited_pin_comparison(self.current, prior, self.policy, self.base)

    def test_missing_prior_pin_refused(self):
        for mid in ('9', '19', '60', '81', '84', '168', '400'):
            with self.subTest(mid=mid):
                prior = copy.deepcopy(self.prior)
                del prior['selected_rel_contracts']['contracts'][mid]
                with self.assertRaisesRegex(ValueError, 'membership'):
                    oracle.inherited_pin_comparison(self.current, prior, self.policy, self.base)

    def test_wrong_base_authority(self):
        with self.assertRaisesRegex(ValueError, 'inherited pin'):
            oracle.inherited_pin_comparison(self.current, self.prior, self.policy,
                                           self.base.replace(f'{168:064x}', '0' * 64))

    def test_shared_setup_exact_authorities(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'composite-batch/generation_receipt.json'
            path.parent.mkdir()
            data = json.dumps(self.prior).encode()
            path.write_bytes(data)
            with patch.object(oracle, 'INHERITED_PRIOR', root), \
                    patch.object(oracle, 'INHERITED_PRIOR_RECEIPT_SHA256', hashlib.sha256(data).hexdigest()), \
                    patch.object(oracle.subprocess, 'check_output', return_value=self.base) as git:
                result = oracle.shared_prior_setup(self.current, self.policy, root)
            self.assertEqual(set(result['prior_wave_pins']), {'9', '19', '60', '81', '84', '168', '400'})
            self.assertFalse(result['automatic_module_admission'])
            git.assert_called_once_with(['git', '-C', str(root), 'show',
                oracle.COVERAGE_SOURCE_HEAD + ':vita/host/actor_batch_policy.h'], text=True)

    def test_missing_authority_never_falls_back_to_delivery(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(oracle, 'INHERITED_PRIOR', Path(directory)):
            with self.assertRaises(OSError):
                oracle.shared_prior_setup(self.current, self.policy, Path(directory))

    def test_wrong_authority_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'composite-batch/generation_receipt.json'
            path.parent.mkdir()
            path.write_text(json.dumps(self.prior))
            with patch.object(oracle, 'INHERITED_PRIOR', root):
                with self.assertRaisesRegex(ValueError, 'JSON identity'):
                    oracle.shared_prior_setup(self.current, self.policy, root)


if __name__ == '__main__':
    unittest.main()
