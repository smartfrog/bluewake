import json
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest
import sys

import test_host

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))
sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))
from memory_capture import publish_replay_manifests, verify_snapshots


class Mem1CaptureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, root, scenario):
        result = subprocess.run([str(test_host.OUT / 'gx_drain_probe'), scenario], cwd=root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stderr

    def test_two_logged_draw_snapshots_contain_actual_time_matched_ram_without_padding_or_later_captures(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
            root = Path(directory)
            log = self.probe(root, 'capture-two')
            records = [json.loads((root / f'ww05.mem1-{index}.json').read_text()) for index in (1, 2)]
            self.assertEqual([record['command'] for record in records], [14, 15])
            self.assertEqual([record['bytes'] for record in records], [25165824, 25165824])
            self.assertEqual([record['physical_base'] for record in records], ['00000000', '00000000'])
            self.assertEqual([record['guest_base'] for record in records], ['80000000', '80000000'])
            self.assertGreater(records[1]['us'], records[0]['us'])
            snapshots = [(root / record['file']).read_bytes() for record in records]
            self.assertEqual([len(snapshot) for snapshot in snapshots], [25165824, 25165824])
            self.assertNotEqual(snapshots[0][0x1000], 0x55)
            self.assertEqual(snapshots[1][0x1000], 0x55)
            self.assertEqual(snapshots[0][:0x1000], snapshots[1][:0x1000])
            self.assertEqual(snapshots[0][0x1001:], snapshots[1][0x1001:])
            self.assertEqual(log.count('[mem1-snapshot]'), 2)
            self.assertEqual(len(list(root.iterdir())), 4)
            manifest = verify_snapshots(root, token='c' * 32, source='a' * 40, package='b' * 64, count=2, journal=log)
            self.assertEqual(manifest['captured'], 2)
            self.assertEqual([item['bytes'] for item in manifest['snapshots']], [25165824, 25165824])
            self.assertNotEqual(manifest['snapshots'][0]['sha256'], manifest['snapshots'][1]['sha256'])

    def test_replay_manifest_uses_lane18_physical_regions_and_exact_journal_and_binary_hashes(self):
        from gx.capture import journal_commands
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
            root = Path(directory)
            log = self.probe(root, 'capture-one')
            manifest = verify_snapshots(root, token='c' * 32, source='a' * 40, package='b' * 64, count=1, journal=log)
            replay = manifest['snapshots'][0]['replay_manifest']
            self.assertEqual(replay['format'], 'gx-mem1-snapshot-v1')
            self.assertEqual(replay['binary'], 'ww05.mem1-1.bin')
            self.assertEqual(replay['binary_sha256'], hashlib.sha256((root / replay['binary']).read_bytes()).hexdigest())
            self.assertEqual(replay['fifo_sha256'], hashlib.sha256(b''.join(journal_commands(log))).hexdigest())
            self.assertEqual(replay['regions'], [{'address': 0, 'offset': 0, 'size': 25165824}])
            self.assertEqual(replay['producer']['command'], 14)
            self.assertEqual(replay['producer']['phase'], 'before-renderer-write')

    def test_replay_manifest_is_published_next_to_unchanged_binary_without_overwriting_evidence(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
            root = Path(directory)
            log = self.probe(root, 'capture-one')
            manifest = verify_snapshots(root, token='c' * 32, source='a' * 40, package='b' * 64, count=1, journal=log)
            publish_replay_manifests(root, manifest)
            path = Path(manifest['snapshots'][0]['replay_manifest_path'])
            content = path.read_bytes()
            self.assertEqual(json.loads(content), manifest['snapshots'][0]['replay_manifest'])
            with self.assertRaises(FileExistsError):
                publish_replay_manifests(root, manifest)
            self.assertEqual(path.read_bytes(), content)

    def test_one_snapshot_limit_does_not_block_subsequent_draws(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
            root = Path(directory)
            log = self.probe(root, 'capture-one')
            self.assertEqual(log.count('[mem1-snapshot]'), 1)
            self.assertEqual(len(list(root.iterdir())), 2)

    def test_capture_is_off_by_default(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
            root = Path(directory)
            self.probe(root, 'state')
            self.assertEqual(list(root.iterdir()), [])

    def test_selected_draw_window_ignores_initial_ram_and_binds_each_snapshot_to_its_draw_prefix(self):
        from gx.capture import journal_commands
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-scene-') as directory:
            root = Path(directory)
            log = self.probe(root, 'capture-window')
            manifest = verify_snapshots(root, token='c' * 32, source='a' * 40, package='b' * 64,
                                        count=2, journal=log, command_window=[15, 16])
            self.assertEqual([s['command'] for s in manifest['snapshots']], [15, 16])
            commands = journal_commands(log)
            for snapshot, value in zip(manifest['snapshots'], (0x55, 0xAA)):
                with self.subTest(command=snapshot['command']):
                    self.assertEqual((root / snapshot['file']).read_bytes()[0x1000], value)
                    self.assertEqual(snapshot['replay_manifest']['fifo_sha256'],
                                     hashlib.sha256(b''.join(commands[:snapshot['command']])).hexdigest())

    def test_scene_window_refuses_more_draws_than_its_snapshot_budget(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-scene-') as directory:
            root = Path(directory)
            log = self.probe(root, 'capture-window-limit')
            self.assertIn('scene MEM1 draw window exhausted its snapshot budget', log)
            self.assertEqual(len(list(root.glob('ww05.mem1-*.bin'))), 1)

    def test_scene_window_cannot_relabel_initial_snapshots_without_its_native_witness(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-scene-') as directory:
            root = Path(directory)
            log = self.probe(root, 'capture-two')
            with self.assertRaisesRegex(ValueError, 'window witness'):
                verify_snapshots(root, token='c' * 32, source='a' * 40, package='b' * 64,
                                 count=2, journal=log, command_window=[14, 15])

    def test_invalid_identity_or_non_mem1_ram_fails_before_opening_any_file(self):
        for scenario in ('capture-bad-identity', 'capture-bad-ram'):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
                root = Path(directory)
                self.probe(root, scenario)
                self.assertEqual(list(root.iterdir()), [])

    def test_existing_final_or_partial_output_is_preserved_and_blocks_capture(self):
        for name in ('ww05.mem1-1.bin', 'ww05.mem1-1.json', 'ww05.mem1-1.bin.partial', 'ww05.mem1-1.json.partial'):
            with self.subTest(name=name), tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
                root = Path(directory)
                (root / name).write_bytes(b'preserve this evidence')
                self.probe(root, 'capture-preexisting')
                self.assertEqual((root / name).read_bytes(), b'preserve this evidence')
                self.assertEqual(len(list(root.iterdir())), 1)

    def test_binary_write_flush_close_or_publish_failure_keeps_partial_evidence_without_a_manifest(self):
        for stage in ('write', 'flush', 'close', 'publish'):
            with self.subTest(stage=stage), tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
                root = Path(directory)
                log = self.probe(root, f'capture-{stage}-failure')
                self.assertTrue((root / 'ww05.mem1-1.bin.partial').exists())
                self.assertFalse((root / 'ww05.mem1-1.bin').exists())
                self.assertFalse((root / 'ww05.mem1-1.json').exists())
                self.assertNotIn('[mem1-snapshot]', log)

    def test_metadata_write_or_publish_failure_cannot_claim_a_complete_snapshot(self):
        for stage in ('write', 'publish'):
            with self.subTest(stage=stage), tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
                root = Path(directory)
                log = self.probe(root, f'capture-json-{stage}-failure')
                self.assertEqual((root / 'ww05.mem1-1.bin').stat().st_size, 25165824)
                self.assertTrue((root / 'ww05.mem1-1.json.partial').exists())
                self.assertFalse((root / 'ww05.mem1-1.json').exists())
                self.assertNotIn('[mem1-snapshot]', log)

    def test_manifest_refuses_wrong_identity_draw_extent_or_time_proof(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
            root = Path(directory)
            log = self.probe(root, 'capture-two')
            path = root / 'ww05.mem1-2.json'
            original = json.loads(path.read_text())
            for field, value in (('token', 'd' * 32), ('source', 'e' * 40), ('vpk_sha256', 'f' * 64),
                                 ('command', 16), ('bytes', 1), ('physical_base', '00001000'),
                                 ('us', 0), ('pc', 'not-a-PC'), ('pc', '12345678'), ('completion_pc', '87654321'),
                                 ('snapshot', True), ('file', '../outside.bin')):
                with self.subTest(field=field):
                    changed = dict(original)
                    changed[field] = value
                    path.write_text(json.dumps(changed) + '\n')
                    with self.assertRaises(ValueError):
                        verify_snapshots(root, token='c' * 32, source='a' * 40, package='b' * 64, count=2, journal=log)
            path.write_text(json.dumps(original) + '\n')

    def test_manifest_refuses_truncated_ram_instead_of_zero_filling_it(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
            root = Path(directory)
            log = self.probe(root, 'capture-one')
            with (root / 'ww05.mem1-1.bin').open('r+b') as stream:
                stream.truncate(4096)
            with self.assertRaises(ValueError):
                verify_snapshots(root, token='c' * 32, source='a' * 40, package='b' * 64, count=1, journal=log)

    def test_partial_or_unrequested_snapshots_cannot_be_promoted_into_proof(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-mem1-') as directory:
            root = Path(directory)
            log = self.probe(root, 'capture-one')
            with self.assertRaises(ValueError):
                verify_snapshots(root, token='c' * 32, source='a' * 40, package='b' * 64, count=0, journal=log)
            (root / 'ww05.mem1-2.bin.partial').write_bytes(b'partial evidence')
            with self.assertRaises(ValueError):
                verify_snapshots(root, token='c' * 32, source='a' * 40, package='b' * 64, count=1, journal=log)
