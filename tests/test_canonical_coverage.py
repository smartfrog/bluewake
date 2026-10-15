import hashlib
import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import verify_canonical_executable as oracle


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.supplement = self.root / 'supplement'
        self.composite = self.root / 'delivery/composite-batch'
        self.supplement.mkdir()
        (self.composite / 'chunks_dol').mkdir(parents=True)
        self.name = 'chunk_0000_text0_80001000.c'
        self.dol = bytearray(264)
        struct.pack_into('>I', self.dol, 0, 256)
        struct.pack_into('>I', self.dol, 0x48, 0x80001000)
        struct.pack_into('>I', self.dol, 0x90, 8)
        self.row = {'id': 7, 'basename': 'sample.rel',
                    'processed_sha256': hashlib.sha256(b'synthetic processed REL').hexdigest(),
                    'raw_sha256': hashlib.sha256(b'synthetic raw REL').hexdigest()}
        self.files = {
            self.source / 'build-f5/ww05/private/main.dol': bytes(self.dol),
            self.source / 'build-f5/ww05/private/rels/sample.rel': b'synthetic processed REL',
            self.source / 'history.json': b'public synthetic historical map',
            self.source / 'spec-table.json': b'public synthetic frozen spec',
            self.source / 'translated/chunks' / self.name: b'public synthetic translated C',
            self.composite / 'chunks_dol' / self.name: b'public synthetic translated C',
            self.composite / 'generated.h': b'public synthetic reviewed dispatch association',
        }
        self.inputs = {}
        for path, data in self.files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            self.inputs[str(path)] = self.pin(data)
        chunk_sha = hashlib.sha256(b'public synthetic translated C').hexdigest()
        self.inventory = {
            'schema': 'ww149b-dol-coverage-supplement-v1',
            'source_head': '23a8f348ec6c9ef4ab22fb69f2df4e412ffc9d10',
            'claim': 'Audited historical identity + structural derived coverage only',
            'partition_instruction_limit': 4096,
            'text_sections': [{'index': 0, 'file_offset': 256,
                               'start': '80001000', 'end_exclusive': '80001008'}],
            'chunks': [{'basename': self.name, 'start': '80001000', 'end_exclusive': '80001008',
                        'function': 'func_80001000', 'sha256': chunk_sha, 'slots': 2, 'labels': 2,
                        'ordered_slots_sha256': hashlib.sha256(b'80001000\n80001004\n').hexdigest()}],
            'dol_inventory_sha256': hashlib.sha256(f'{chunk_sha} {self.name}\n'.encode()).hexdigest(),
            'target_census': {'canonical_replay_qualified': False, 'uncovered': 0},
            'rels': [{'id': 7, 'path': 'build-f5/ww05/private/rels/sample.rel',
                      'processed_sha256': self.row['processed_sha256'],
                      'raw_reverse_fixups_sha256': self.row['raw_sha256']}],
            'targets': [{'target': '80001000', 'tagged_alias': 'C0001000', 'chunk': self.name,
                         'slot': 0, 'label_line': 10, 'records': [[7, 128]]}],
            'walker': {'start': '80001000', 'end_exclusive': '80001004', 'size': 4,
                       'file_offset': 256, 'span_sha256': hashlib.sha256(b'\0' * 4).hexdigest(),
                       'entries': [{'pc': '80001000', 'tagged': 'C0001000', 'slot': 0}]},
            'aliases': {'tagged_fallback': 'C0000000 <= address < C0400000; clear 40000000; set ctx->pc; call_original after replacement/host hooks'},
        }
        self.external = [0x80001000]
        self.walker = 0xC0001000
        self.source_patch = patch.object(oracle, 'COVERAGE_SOURCE', self.source)
        self.source_patch.start()
        self.addCleanup(self.source_patch.stop)
        self.spec_patch = patch.object(oracle, 'SPEC_SHA256', self.inputs[str(self.source / 'spec-table.json')]['sha256'])
        self.spec_patch.start()
        self.addCleanup(self.spec_patch.stop)
        self.git_patch = patch.object(oracle.subprocess, 'check_output', side_effect=self.git)
        self.git_patch.start()
        self.addCleanup(self.git_patch.stop)
        self.source_head = '23a8f348ec6c9ef4ab22fb69f2df4e412ffc9d10'
        self.source_status = ''
        self.freeze()

    @staticmethod
    def pin(data):
        return {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}

    def git(self, args, **kwargs):
        return self.source_head + '\n' if 'rev-parse' in args else self.source_status

    def freeze(self):
        outputs = {'inventory.json': json.dumps(self.inventory).encode(),
                   'inputs.json': json.dumps(self.inputs).encode(),
                   'inventory.py': b'public synthetic reviewed implementation',
                   'test_inventory.py': b'public synthetic reviewed fixtures',
                   'test-results.txt': b'public synthetic green',
                   'REPORT.md': b'public synthetic structural-only review'}
        for name, data in outputs.items():
            (self.supplement / name).write_bytes(data)
        self.manifest = {'schema': 'ww149b-dol-coverage-manifest-v1', 'inputs': self.inputs,
                         'scripts_and_outputs': {name: self.pin(data) for name, data in outputs.items()}}
        self.manifest_bytes = json.dumps(self.manifest).encode()
        (self.supplement / 'manifest.json').write_bytes(self.manifest_bytes)

    def check(self):
        with patch.object(oracle, 'COVERAGE_MANIFEST_SHA256', hashlib.sha256(self.manifest_bytes).hexdigest()):
            return oracle.coverage_dependency(self.supplement, self.row, self.external,
                                              self.walker, bytes(self.dol), self.composite)

    def test_admitted_binding(self):
        result = self.check()
        self.assertEqual(result['status'], 'VERIFIED-candidate')
        self.assertEqual(result['claim'], 'Audited historical identity + structural derived coverage only')

    def test_absent_supplement_is_blocked(self):
        self.assertEqual(oracle.coverage_dependency(None, self.row, self.external,
                         self.walker, bytes(self.dol), self.composite)['status'], 'BLOCKED-incomplete')

    def test_production_pin_is_not_selected_by_caller(self):
        self.assertEqual(oracle.COVERAGE_MANIFEST_SHA256,
                         'a1e96bc3ffbf0d356b76e2e4fe638943334390e9074638b6945071c5418a5a51')
        with self.assertRaisesRegex(ValueError, 'manifest identity'):
            oracle.coverage_dependency(self.supplement, self.row, self.external,
                                       self.walker, bytes(self.dol), self.composite)

    def test_missing_manifest(self):
        (self.supplement / 'manifest.json').unlink()
        with self.assertRaises(OSError):
            self.check()

    def test_altered_manifest(self):
        (self.supplement / 'manifest.json').write_bytes(self.manifest_bytes + b' ')
        with self.assertRaisesRegex(ValueError, 'manifest identity'):
            self.check()

    def test_altered_input(self):
        (self.source / 'history.json').write_bytes(b'changed historical source')
        with self.assertRaisesRegex(ValueError, 'pinned file'):
            self.check()

    def test_missing_input(self):
        (self.source / 'history.json').unlink()
        with self.assertRaises(OSError):
            self.check()

    def test_altered_dependency(self):
        (self.supplement / 'inventory.py').write_bytes(b'changed implementation')
        with self.assertRaisesRegex(ValueError, 'pinned file'):
            self.check()

    def test_altered_inventory(self):
        (self.supplement / 'inventory.json').write_bytes(b'{}')
        with self.assertRaisesRegex(ValueError, 'pinned file'):
            self.check()

    def test_wrong_source_revision(self):
        self.source_head = '0' * 40
        with self.assertRaisesRegex(ValueError, 'SOURCE revision'):
            self.check()

    def test_dirty_source(self):
        self.source_status = ' M tracked.py\n'
        with self.assertRaisesRegex(ValueError, 'SOURCE tree'):
            self.check()

    def test_delivered_hash_mismatch(self):
        (self.composite / 'chunks_dol' / self.name).write_bytes(b'altered delivered C')
        with self.assertRaisesRegex(ValueError, 'pinned file'):
            self.check()

    def test_different_actual_delivery(self):
        different = self.root / 'different/composite-batch'
        (different / 'chunks_dol').mkdir(parents=True)
        (different / 'generated.h').write_bytes(b'wrong dispatch')
        (different / 'chunks_dol' / self.name).write_bytes(b'public synthetic translated C')
        self.composite = different
        with self.assertRaisesRegex(ValueError, 'pinned file'):
            self.check()

    def test_extra_delivered_chunk(self):
        (self.composite / 'chunks_dol/extra.c').write_bytes(b'public extra chunk')
        with self.assertRaisesRegex(ValueError, 'delivered DOL inventory'):
            self.check()

    def test_extra_current_source_chunk(self):
        (self.source / 'translated/chunks/extra.c').write_bytes(b'public extra source')
        with self.assertRaisesRegex(ValueError, 'source DOL inventory'):
            self.check()

    def test_input_inventory_disagrees_with_manifest(self):
        data = b'{}'
        (self.supplement / 'inputs.json').write_bytes(data)
        self.manifest['scripts_and_outputs']['inputs.json'] = self.pin(data)
        self.manifest_bytes = json.dumps(self.manifest).encode()
        (self.supplement / 'manifest.json').write_bytes(self.manifest_bytes)
        with self.assertRaisesRegex(ValueError, 'input inventory'):
            self.check()

    def test_missing_module_identity(self):
        self.inventory['rels'] = []
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'module identity'):
            self.check()

    def test_independently_derived_target_outside_dol_text(self):
        self.external = [0x80001008]
        self.inventory['targets'][0]['target'] = '80001008'
        self.inventory['targets'][0]['tagged_alias'] = 'C0001008'
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'outside DOL text'):
            self.check()

    def test_walker_outside_admitted_entry(self):
        self.walker = 0xC0001004
        with self.assertRaisesRegex(ValueError, 'walker'):
            self.check()

    def test_unaligned_independently_derived_target(self):
        self.external = [0x80001001]
        self.inventory['targets'][0]['target'] = '80001001'
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'alignment'):
            self.check()

    def test_missing_module_target(self):
        self.inventory['targets'] = []
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'module target'):
            self.check()

    def test_wrong_target_module_association(self):
        self.inventory['targets'][0]['records'] = [[8, 128]]
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'module target'):
            self.check()

    def test_wrong_chunk_association(self):
        self.inventory['targets'][0]['chunk'] = 'wrong.c'
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'target association'):
            self.check()

    def test_wrong_function_association(self):
        self.inventory['chunks'][0]['function'] = 'func_80002000'
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'chunk association'):
            self.check()

    def test_wrong_target_slot(self):
        self.inventory['targets'][0]['slot'] = 1
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'target association'):
            self.check()

    def test_missing_walker_entry(self):
        self.inventory['walker']['entries'] = []
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'walker'):
            self.check()

    def test_wrong_walker_alias(self):
        self.inventory['walker']['entries'][0]['tagged'] = 'C0001004'
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'walker'):
            self.check()

    def test_wrong_walker_span(self):
        self.inventory['walker']['span_sha256'] = '0' * 64
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'walker'):
            self.check()

    def test_alias_outside_qualified_scope(self):
        self.walker = 0xC0401000
        with self.assertRaisesRegex(ValueError, 'alias scope'):
            self.check()

    def test_physical_alias_not_qualified(self):
        self.walker = 0x1000
        with self.assertRaisesRegex(ValueError, 'alias scope'):
            self.check()

    def test_altered_scope(self):
        self.inventory['claim'] = 'instruction semantic equivalence'
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'scope'):
            self.check()

    def test_altered_alias_scope(self):
        self.inventory['aliases']['tagged_fallback'] = 'mask any address'
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'scope'):
            self.check()

    def test_module_identity_mismatch(self):
        self.inventory['rels'][0]['raw_reverse_fixups_sha256'] = '0' * 64
        self.freeze()
        with self.assertRaisesRegex(ValueError, 'module identity'):
            self.check()

    def test_actual_dol_mismatch(self):
        self.dol[-1] = 1
        with self.assertRaisesRegex(ValueError, 'DOL identity'):
            self.check()


class CoverageCliTests(unittest.TestCase):
    def arguments(self):
        return ['verify_canonical_executable.py', '--module', '7', '--spec', 'spec.json',
                '--iso', 'owned.iso', '--delivery', 'delivery', '--evidence', 'evidence']

    def test_qualified_candidate_exit_zero(self):
        output = io.StringIO()
        with patch.object(sys, 'argv', self.arguments() + ['--dol-coverage', 'supplement']), \
                patch.object(oracle, 'qualify', return_value={'status': 'VERIFIED-candidate'}) as qualify, \
                contextlib.redirect_stdout(output):
            self.assertEqual(oracle.main(), 0)
        self.assertEqual(qualify.call_args.args[0].dol_coverage, Path('supplement'))
        self.assertEqual(json.loads(output.getvalue())['status'], 'VERIFIED-candidate')

    def test_absent_qualification_exit_two(self):
        with patch.object(sys, 'argv', self.arguments()), \
                patch.object(oracle, 'qualify', return_value={'status': 'BLOCKED-incomplete'}), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(oracle.main(), 2)

    def test_unqualified_manifest_remains_blocked(self):
        output = io.StringIO()
        with patch.object(sys, 'argv', self.arguments() + ['--dol-coverage', 'unqualified']), \
                patch.object(oracle, 'qualify', side_effect=oracle.UnqualifiedCoverage('unadmitted manifest identity')), \
                contextlib.redirect_stdout(output):
            self.assertEqual(oracle.main(), 2)
        self.assertEqual(json.loads(output.getvalue())['status'], 'BLOCKED-incomplete')

    def test_caller_cannot_select_expected_manifest_identity(self):
        with patch.object(sys, 'argv', self.arguments() + ['--dol-coverage-manifest-sha256', '0' * 64]), \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
            oracle.main()
        self.assertEqual(caught.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
