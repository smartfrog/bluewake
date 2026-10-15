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


class InheritedCaptureTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.evidence = self.root / 'delivery'
        self.prior = self.root / 'prior'
        (self.evidence / 'manifests').mkdir(parents=True)
        (self.prior / 'manifests').mkdir(parents=True)
        magic = {9: b'BRIDGE9\0', 19: b'GRASS19\0', 60: b'IKADA60\0', 81: b'TPOST81\0',
                 84: b'WOOD84\0\0', 168: b'SHIP168\0', 400: b'TITLE400'}
        paths = {9: 'inherited/bridge/bridge9.ship-code-input',
                 19: 'inherited/grass/grass19.ship-code-input',
                 60: 'inherited/inherited/ikada.ship-code-input',
                 81: 'inherited/toripost/toripost81.ship-code-input',
                 84: 'inherited/inherited/wood84.ship-code-input',
                 168: 'inherited/inherited/ship-inherited.ship-code-input',
                 400: 'inherited/inherited/title-recovered.ship-code-input'}
        self.rows, self.payloads = {}, {}
        self.manifest = {'version': 1, 'dol_sha256': 'd' * 64, 'modules': {}}
        self.old = {'version': 1, 'dol_sha256': 'd' * 64, 'modules': {}}
        self.delivery = {'source_delivery_sha': oracle.DELIVERY, 'inherited_ids': list(paths),
                         'captures': {}, 'relocated_manifests': {'opening-d.json': {}}}
        for mid, relative in paths.items():
            row = {'id': mid, 'basename': f'public{mid}.rel', 'expected_capture_size': 32,
                   'raw_sha256': '1' * 64, 'processed_sha256': '2' * 64,
                   'translation': {'files_sha256': {'chunks/public.c': '3' * 64}},
                   'capture_magic_hex': (b'REL00081' if mid == 81 else magic[mid]).hex()}
            self.rows[mid] = row
            payload = magic[mid] + struct.pack('>IIIII', 1, 1, 128, 0xC0000080, 4) + b'ABCD'
            self.payloads[mid] = payload
            path = self.evidence / 'captures' / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
            pin = hashlib.sha256(payload).hexdigest()
            entry = {'capture': str(path), 'capture_sha256': pin, 'raw_sha256': row['raw_sha256'],
                     'processed_rel_sha256': row['processed_sha256'],
                     'translation_sha256': row['translation']['files_sha256']}
            self.manifest['modules'][str(mid)] = entry
            self.old['modules'][str(mid)] = entry | {'capture': str(self.prior / 'captures' / relative.removeprefix('inherited/'))}
            self.delivery['captures'][relative] = pin
        self.prior_patch = patch.object(oracle, 'INHERITED_PRIOR', self.prior)
        self.prior_patch.start()
        self.addCleanup(self.prior_patch.stop)
        self.freeze()

    def freeze(self):
        self.manifest_bytes = json.dumps(self.manifest).encode()
        self.old_bytes = json.dumps(self.old).encode()
        (self.evidence / 'manifests/opening-d.json').write_bytes(self.manifest_bytes)
        (self.prior / 'manifests/opening-d.json').write_bytes(self.old_bytes)
        self.delivery['relocated_manifests']['opening-d.json'] = {
            'original_sha256': hashlib.sha256(self.old_bytes).hexdigest(),
            'preserved_sha256': hashlib.sha256(self.manifest_bytes).hexdigest()}
        self.delivery_bytes = json.dumps(self.delivery).encode()
        (self.evidence / 'MANIFEST.json').write_bytes(self.delivery_bytes)

    def check(self, mid=84):
        with patch.object(oracle, 'INHERITED_ROOT_SHA256', hashlib.sha256(self.delivery_bytes).hexdigest()), \
                patch.object(oracle, 'INHERITED_PRIOR_MANIFEST_SHA256', hashlib.sha256(self.old_bytes).hexdigest()):
            return oracle.capture_input(self.rows[mid], self.evidence)

    def test_all_seven_exact_layouts(self):
        for mid in (9, 19, 60, 81, 84, 168, 400):
            with self.subTest(mid=mid):
                data, provenance = self.check(mid)
                self.assertEqual(data, self.payloads[mid])
                self.assertEqual(provenance['capture_sha256'], hashlib.sha256(data).hexdigest())
                self.assertEqual(provenance['capture_magic_hex'], data[:8].hex())

    def test_bulk_route_remains_unchanged(self):
        path = self.evidence / 'captures/bulk/public-7.ship-code-input'
        path.parent.mkdir()
        path.write_bytes(b'public bulk capture')
        data, provenance = oracle.capture_input({'id': 7, 'capture_basename': path.name}, self.evidence)
        self.assertEqual((data, provenance), (b'public bulk capture', None))

    def test_missing_capture_never_uses_bulk_alias(self):
        path = Path(self.manifest['modules']['84']['capture'])
        path.unlink()
        bulk = self.evidence / 'captures/bulk'
        bulk.mkdir()
        (bulk / 'public84.ship-code-input').write_bytes(self.payloads[84])
        self.rows[84]['capture_basename'] = 'public84.ship-code-input'
        with self.assertRaises(OSError):
            self.check()

    def test_changed_capture_pin(self):
        self.manifest['modules']['84']['capture_sha256'] = '0' * 64
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'capture pin'):
            self.check()

    def test_missing_capture_pin(self):
        del self.delivery['captures']['inherited/inherited/wood84.ship-code-input']
        self.freeze()
        with self.assertRaises(KeyError):
            self.check()

    def test_missing_prior_manifest(self):
        (self.prior / 'manifests/opening-d.json').unlink()
        with self.assertRaises(OSError):
            self.check()

    def test_altered_prior_manifest(self):
        (self.prior / 'manifests/opening-d.json').write_bytes(self.old_bytes + b' ')
        with self.assertRaisesRegex(ValueError, 'JSON identity'):
            self.check()

    def test_toripost_magic_does_not_mutate_frozen_spec(self):
        data, provenance = self.check(81)
        self.assertEqual(data[:8], b'TPOST81\0')
        self.assertEqual(provenance['capture_magic_hex'], b'TPOST81\0'.hex())
        self.assertEqual(self.rows[81]['capture_magic_hex'], b'REL00081'.hex())

    def test_unreviewed_namespace_refused(self):
        self.rows[84]['capture_magic_hex'] = b'UNKNOWN!'.hex()
        with self.assertRaisesRegex(ValueError, 'namespace'):
            self.check()

    def test_changed_capture_bytes(self):
        Path(self.manifest['modules']['84']['capture']).write_bytes(b'changed public capture')
        with self.assertRaisesRegex(ValueError, 'capture identity'):
            self.check()

    def test_wrong_path(self):
        self.manifest['modules']['84']['capture'] = str(self.evidence / 'captures/bulk/wood84.ship-code-input')
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'capture path'):
            self.check()

    def test_lexical_path_alias(self):
        self.manifest['modules']['84']['capture'] = str(self.evidence / 'captures/inherited/inherited') + '/../inherited/wood84.ship-code-input'
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'capture path'):
            self.check()

    def test_symlink_capture_alias(self):
        path = Path(self.manifest['modules']['84']['capture'])
        path.unlink()
        other = self.root / 'public-alias'
        other.write_bytes(self.payloads[84])
        path.symlink_to(other)
        with self.assertRaisesRegex(ValueError, 'capture path'):
            self.check()

    def test_prior_capture_pin_disagreement(self):
        self.old['modules']['84']['capture_sha256'] = '0' * 64
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'capture pin'):
            self.check()

    def test_missing_module(self):
        del self.manifest['modules']['84']
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'membership'):
            self.check()

    def test_altered_root_manifest(self):
        (self.evidence / 'MANIFEST.json').write_bytes(self.delivery_bytes + b' ')
        with self.assertRaisesRegex(ValueError, 'JSON identity'):
            self.check()

    def test_ambiguous_module_key(self):
        self.manifest_bytes = self.manifest_bytes.replace(b'"modules": {', b'"modules": {"84": {},')
        (self.evidence / 'manifests/opening-d.json').write_bytes(self.manifest_bytes)
        self.delivery['relocated_manifests']['opening-d.json']['preserved_sha256'] = hashlib.sha256(self.manifest_bytes).hexdigest()
        self.delivery_bytes = json.dumps(self.delivery).encode()
        (self.evidence / 'MANIFEST.json').write_bytes(self.delivery_bytes)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.check()

    def test_frozen_source_identity_disagreement(self):
        self.rows[84]['raw_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'source identity'):
            self.check()


class InheritedPinTests(unittest.TestCase):
    def setUp(self):
        pins = {mid: f'{mid:064x}' for mid in (9, 19, 60, 81, 84, 168, 400)}
        contracts = {str(mid): {'build_identity': pin} for mid, pin in pins.items()}
        self.current = {'selected_rel_contracts': {'contracts': contracts}}
        self.prior = json.loads(json.dumps(self.current))
        lines = []
        for mid, pin in pins.items():
            actor = 'false' if mid in (60, 168, 400) else 'true'
            form = 'EXISTING' if mid in (60, 168, 400) else ('CERTIFIED_INLINE_ZERO' if mid == 84 else 'OWNER_PUBLISHED')
            lines.append(f'    {{{mid}u, "{pin}", {actor}, WW_ACTOR_SDK_{form}}},')
        self.policy = '\n'.join(lines)
        self.base_policy = '#if WW05_TESTS_ONLY\npublic synthetic fixture branch\n#else\n' + self.policy + '\n#endif\n};'

    def check(self):
        return oracle.inherited_pin_comparison(self.current, self.prior, self.policy, self.base_policy)

    def test_all_prior_pins_and_nonactor_assignments(self):
        proof = self.check()
        self.assertEqual(set(proof), {'9', '19', '60', '81', '84', '168', '400'})
        for mid in ('60', '168', '400'):
            with self.subTest(mid=mid):
                self.assertFalse(proof[mid]['actor'])
                self.assertEqual(proof[mid]['policy_sdk_contract'], 'EXISTING')

    def test_prior_certificate_disagreement(self):
        self.prior['selected_rel_contracts']['contracts']['168']['build_identity'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'inherited pin'):
            self.check()

    def test_wrong_current_policy_pin(self):
        self.policy = self.policy.replace(f'{168:064x}', '0' * 64)
        with self.assertRaisesRegex(ValueError, 'inherited pin'):
            self.check()

    def test_nonactor_override_refused(self):
        self.policy = self.policy.replace('false, WW_ACTOR_SDK_EXISTING', 'true, WW_ACTOR_SDK_OWNER_PUBLISHED')
        with self.assertRaisesRegex(ValueError, 'policy assignment'):
            self.check()

    def test_duplicate_policy_member(self):
        self.policy += '\n' + self.policy.splitlines()[0]
        with self.assertRaisesRegex(ValueError, 'membership'):
            self.check()


if __name__ == '__main__':
    unittest.main()
