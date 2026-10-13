import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import extract
from test_capture import command


class ExtractionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)
        self.journal = self.root / 'ww05.log'
        self.report = self.root / 'ww05.report'

    def tearDown(self):
        self.temporary.cleanup()

    def test_preserves_exact_fifo_and_provenance(self):
        data = bytes.fromhex('610F0000FF')
        self.journal.write_text(command(1, 0x61, data))
        result = extract.extract_fixture(self.journal, self.root / 'fixture')
        self.assertEqual((self.root / 'fixture/stream.fifo').read_bytes(), data)
        self.assertEqual(result['fifo_sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(result['captured_draws'], 0)

    def test_reports_unlogged_draws_without_inventing_them(self):
        self.journal.write_text(command(1, 0, b'\0'))
        self.report.write_text('GX_STREAM decoded=20 logged=1 limit=1 pending=0 read_bytes=90\n'
                               'RENDER fifo_bytes=90 draws=1\n')
        result = extract.extract_fixture(self.journal, self.root / 'fixture', report=self.report)
        self.assertEqual(result['coverage'], 'logged-command-prefix-only')
        self.assertEqual(result['reported_draws'], 1)
        self.assertEqual(result['captured_draws'], 0)

    def test_refuses_metadata_sequence_gaps(self):
        self.journal.write_text(command(2, 0, b'\0'))
        with self.assertRaisesRegex(ValueError, 'sequence'):
            extract.extract_fixture(self.journal, self.root / 'fixture')

    def test_marks_indexed_draw_as_memory_dependent(self):
        cp = bytes.fromhex('085000000400')
        draw = bytes.fromhex('900003000102')
        self.journal.write_text(command(1, 8, cp) + '\n' + command(2, 0x90, draw))
        result = extract.extract_fixture(self.journal, self.root / 'fixture')
        self.assertTrue(result['requires_mem1'])
        self.assertEqual(result['captured_draws'], 1)

    def test_copies_supplied_memory_instead_of_zero_filling(self):
        self.journal.write_text(command(1, 0, b'\0'))
        memory = self.root / 'guest.mem1'
        memory.write_bytes(b'guest-owned-memory')
        result = extract.extract_fixture(self.journal, self.root / 'fixture', mem1=memory)
        self.assertEqual((self.root / 'fixture/mem1.bin').read_bytes(), memory.read_bytes())
        self.assertEqual(result['mem1_sha256'], hashlib.sha256(memory.read_bytes()).hexdigest())

    def test_refuses_to_overwrite_a_fixture(self):
        self.journal.write_text(command(1, 0, b'\0'))
        output = self.root / 'fixture'
        extract.extract_fixture(self.journal, output)
        with self.assertRaises(FileExistsError):
            extract.extract_fixture(self.journal, output)

    def test_manifest_matches_returned_metadata(self):
        self.journal.write_text(command(1, 0, b'\0'))
        result = extract.extract_fixture(self.journal, self.root / 'fixture')
        manifest = json.loads((self.root / 'fixture/capture.json').read_text())
        self.assertEqual(manifest, result)

    def test_latest_selection_uses_journal_draws_not_renderer_totals(self):
        directory = self.root / 'vita3k-run-one'
        directory.mkdir()
        (directory / 'ww05.report').write_text('RENDER draws=9\n')
        (directory / 'ww05.log').write_text(command(1, 0, b'\0'))
        self.assertIsNone(extract.latest_draw_capture(self.root, minimum_draws=1))

    def test_validates_commands_after_memory_dependent_command(self):
        self.journal.write_text(command(1, 0x20, bytes.fromhex('2000000000')) + '\n'
                                + command(2, 0x61, bytes.fromhex('6100')))
        with self.assertRaisesRegex(ValueError, 'frame length'):
            extract.extract_fixture(self.journal, self.root / 'fixture')

    def test_tlut_load_requires_memory_without_a_draw(self):
        self.journal.write_text(command(1, 0x61, bytes.fromhex('6165000000')))
        result = extract.extract_fixture(self.journal, self.root / 'fixture')
        self.assertTrue(result['requires_mem1'])

    def test_complete_command_prefix_preserves_pending_bytes_status(self):
        self.journal.write_text(command(1, 0, b'\0'))
        self.report.write_text('GX_STREAM decoded=1 logged=1 pending=3\n')
        result = extract.extract_fixture(self.journal, self.root / 'fixture', report=self.report)
        self.assertEqual(result['pending_bytes'], 3)
        self.assertEqual(result['coverage'], 'all-reported-commands-partial-tail')

    def test_replay_requires_memory_before_calling_bridge(self):
        with patch.object(extract.bridge, 'run_stream') as runner:
            text, status = extract.replay_fixture(self.root, {'requires_mem1': True, 'mem1_sha256': None})
        runner.assert_not_called()
        self.assertEqual((text, status), ('verdict=memory-required\n', 2))

    def test_replay_rejects_silent_draw_loss(self):
        (self.root / 'stream.fifo').write_bytes(b'\0')
        output = 'bridge ok=1 plans_skipped=0 programs_refused=0 copies_refused=0 programs=0\n'
        metadata = dict(requires_mem1=False, mem1_sha256=None, captured_draws=1)
        with patch.object(extract.bridge, 'find_binary'), patch.object(extract.bridge, 'run_stream', return_value=(output, None)):
            text, status = extract.replay_fixture(self.root, metadata)
        self.assertEqual(status, 1)
        self.assertIn('verdict=ran-with-coverage-gaps', text)

    def test_replay_retains_nonzero_feature_gaps(self):
        (self.root / 'stream.fifo').write_bytes(b'\0')
        output = 'bridge ok=1 plans_skipped=0 programs_refused=0 copies_refused=0 programs=1\ngap fog_ignored=1\n'
        metadata = dict(requires_mem1=False, mem1_sha256=None, captured_draws=1)
        with patch.object(extract.bridge, 'find_binary'), patch.object(extract.bridge, 'run_stream', return_value=(output, None)):
            text, status = extract.replay_fixture(self.root, metadata)
        self.assertEqual(status, 1)
        self.assertIn('gap fog_ignored=1', text)

    def test_latest_selection_finds_multiple_logged_draws(self):
        directory = self.root / 'vita3k-run-two'
        directory.mkdir()
        report = directory / 'ww05.report'
        journal = directory / 'ww05.log'
        report.write_text('RENDER draws=2\n')
        draw = bytes.fromhex('900000')
        journal.write_text(command(1, 0x90, draw) + '\n' + command(2, 0x90, draw))
        self.assertEqual(extract.latest_draw_capture(self.root, minimum_draws=2), (journal, report))

    def test_extract_stages_snapshot_and_guard_without_exposing_holes(self):
        data = bytes.fromhex('610F0000FF')
        self.journal.write_text(command(1, 0x61, data))
        memory = self.root / 'memory.bin'
        memory.write_bytes(b'bytes')
        manifest = self.root / 'memory.json'
        manifest.write_text(json.dumps(dict(format='gx-mem1-snapshot-v1', binary=memory.name,
                                            binary_sha256=hashlib.sha256(b'bytes').hexdigest(),
                                            fifo_sha256=hashlib.sha256(data).hexdigest(),
                                            regions=[dict(address=0x1000, offset=0, size=5)])))
        output = self.root / 'fixture'
        result = extract.extract_fixture(self.journal, output, snapshot=manifest)
        self.assertEqual((output / 'mem1.bin').read_bytes()[0x1000:0x1005], b'bytes')
        self.assertEqual((output / 'mem1.ranges').read_text(), 'GX_MEM1_RANGES_V1\n00001000 00000005\n')
        self.assertEqual(result['snapshot']['captured_bytes'], 5)

    def test_extract_rejects_raw_and_manifest_memory_together(self):
        self.journal.write_text(command(1, 0, b'\0'))
        with self.assertRaisesRegex(ValueError, 'one memory'):
            extract.extract_fixture(self.journal, self.root / 'fixture', mem1=self.root, snapshot=self.root)

    def test_fixture_fifo_tampering_is_detected_before_replay(self):
        (self.root / 'stream.fifo').write_bytes(b'changed')
        metadata = dict(requires_mem1=True, mem1_sha256='0' * 64,
                        snapshot=dict(fifo_sha256=hashlib.sha256(b'original').hexdigest()))
        with patch.object(extract.bridge, 'run_stream') as runner:
            with self.assertRaisesRegex(ValueError, 'FIFO SHA-256 mismatch'):
                extract.replay_fixture(self.root, metadata)
        runner.assert_not_called()

    def test_selected_prefix_matches_numbered_snapshot_without_changing_source(self):
        first = bytes.fromhex('610F0000FF')
        self.journal.write_text(command(1, 0x61, first) + '\n' + command(2, 0, b'\0'))
        original = self.journal.read_bytes()
        payload = b'actual-captured-bytes'
        (self.root / 'memory.bin').write_bytes(payload)
        manifest = self.root / 'snapshot.json'
        manifest.write_text(json.dumps(dict(format='gx-mem1-snapshot-v1', binary='memory.bin',
                                            binary_sha256=hashlib.sha256(payload).hexdigest(),
                                            fifo_sha256=hashlib.sha256(first).hexdigest(),
                                            regions=[dict(address=0, offset=0, size=len(payload))])))
        result = extract.extract_fixture(self.journal, self.root / 'fixture', snapshot=manifest, through_command=1)
        self.assertEqual((self.root / 'fixture/stream.fifo').read_bytes(), first)
        self.assertEqual((result['commands'], result['journal_commands']), (1, 2))
        self.assertEqual(result['coverage'], 'selected-command-prefix')
        self.assertEqual(self.journal.read_bytes(), original)

    def test_selected_prefix_keeps_full_report_provenance(self):
        self.journal.write_text(command(1, 0, b'\0') + '\n' + command(2, 0, b'\0'))
        self.report.write_text('GX_STREAM decoded=4 logged=2 pending=5\nRENDER draws=2\n')
        result = extract.extract_fixture(self.journal, self.root / 'fixture', report=self.report, through_command=1)
        self.assertEqual(result['journal_coverage'], 'logged-command-prefix-only')
        self.assertEqual(result['reported_pending_bytes'], 5)
        self.assertEqual(result['pending_bytes'], 0)

    def test_prefix_cannot_request_an_unlogged_command(self):
        self.journal.write_text(command(1, 0, b'\0'))
        with self.assertRaisesRegex(ValueError, 'through-command'):
            extract.extract_fixture(self.journal, self.root / 'fixture', through_command=2)

    def test_full_journal_snapshot_hash_cannot_be_rebound_to_a_shorter_prefix(self):
        self.journal.write_text(command(1, 0, b'\0') + '\n' + command(2, 0, b'\0'))
        payload = b'captured'
        (self.root / 'memory.bin').write_bytes(payload)
        manifest = self.root / 'snapshot.json'
        manifest.write_text(json.dumps(dict(format='gx-mem1-snapshot-v1', binary='memory.bin',
                                            binary_sha256=hashlib.sha256(payload).hexdigest(),
                                            fifo_sha256=hashlib.sha256(b'\0\0').hexdigest(),
                                            regions=[dict(address=0, offset=0, size=len(payload))])))
        with self.assertRaisesRegex(ValueError, 'FIFO SHA-256'):
            extract.extract_fixture(self.journal, self.root / 'fixture', snapshot=manifest, through_command=1)
        self.assertFalse((self.root / 'fixture').exists())


if __name__ == '__main__':
    unittest.main()
