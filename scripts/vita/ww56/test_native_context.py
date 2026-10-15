import unittest
from pathlib import Path
import json
import struct
import tempfile
from unittest.mock import patch

from native_context import NativeContextWindow
from native_trace import checksum, decode
from native_artifacts import identity, verify_receipt
from test_watch_frontier import WatchFixture


class NativeContextWindowTests(unittest.TestCase):
    def test_default_has_no_native_deadline(self):
        self.assertIsNone(NativeContextWindow().stop(1000))

    def test_arm_deadline_uses_event_time_not_delayed_ingestion(self):
        window = NativeContextWindow()
        window.feed(['[native-ctx] event=armed us=100000000 fd=7 flags=0'], now=50, wall=102)
        self.assertIsNone(window.stop(57.99))
        self.assertEqual(window.stop(58)['stop_reason'], 'native-context-arm-deadline')

    def test_body_read_deadline_is_five_seconds(self):
        window = NativeContextWindow()
        window.feed(['[native-ctx] event=armed us=100000000 fd=7 flags=0'], now=50, wall=100)
        window.feed(['[native-ctx] event=tlogoe-body us=102000000 fd=7 flags=0'], now=52, wall=102)
        self.assertEqual(window.stop(57)['stop_reason'], 'native-context-body-deadline')

    def test_ready_failure_is_inconclusive(self):
        window = NativeContextWindow()
        window.feed(['[native-ctx] event=ready us=100000000 fd=-1 flags=16'], now=50, wall=100)
        self.assertEqual(window.stop(50)['classification'], 'inconclusive')

    def test_body_without_arm_is_inconclusive(self):
        window = NativeContextWindow()
        window.feed(['[native-ctx] event=tlogoe-body us=100000000 fd=7 flags=0'], now=50, wall=100)
        self.assertEqual(window.stop(50)['classification'], 'inconclusive')

    def test_native_classification_gap_is_explicit(self):
        window = NativeContextWindow()
        self.assertIn('unresolved', window.receipt()['native_fault_classification'])

    def test_malformed_marker_is_inconclusive(self):
        window = NativeContextWindow()
        window.feed(['[native-ctx] event=armed us=bad fd=7 flags=0'], now=50, wall=100)
        self.assertEqual(window.stop(50)['classification'], 'inconclusive')


def record(stage, sequence=1):
    words = [0x37315757, 1, sequence, ~sequence & 0xffffffff, stage] + [0] * 27
    raw = struct.pack('<32I', *words)
    return raw[:124] + struct.pack('<I', checksum(raw))


class NativeTraceDecoderTests(unittest.TestCase):
    def test_no_footer_is_needed_for_valid_armed_records(self):
        data = bytearray(4224)
        data[:128] = record(0)
        data[4096:] = record(5, 2)
        self.assertEqual(decode(data)['coverage'], 'available')

    def test_interrupted_record_makes_coverage_inconclusive(self):
        data = bytearray(4224)
        data[:128] = record(0)
        data[4096:] = record(5, 2)
        data[-1] ^= 1
        self.assertEqual(decode(data)['invalid_offsets'], [4096])

    def test_oversized_file_is_rejected(self):
        with self.assertRaises(ValueError):
            decode(bytes(593921))

    def test_sticky_failure_makes_coverage_inconclusive(self):
        data = bytearray(4224)
        data[:128] = record(0)
        data[640:768] = record(9, 2)
        data[4096:] = record(5, 3)
        self.assertEqual(decode(data)['coverage'], 'inconclusive')


class NativeArtifactTests(unittest.TestCase):
    def test_missing_copy_blocks_launch(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            root = Path(directory)
            receipt = {'source': 'a' * 40, 'private': True, 'artifacts': {}}
            for name in ('ww05_host', 'ww05_host.velf', 'ww05.map'):
                path = root / name
                path.write_bytes(b'synthetic ELF identity fixture')
                receipt['artifacts'][name] = {'copy': str(path), 'identity': identity(path)}
            (root / 'receipt.json').write_text(json.dumps(receipt))
            verify_receipt(root / 'receipt.json', 'a' * 40)
            (root / 'ww05_host.velf').unlink()
            with self.assertRaisesRegex(ValueError, 'missing or changed'):
                verify_receipt(root / 'receipt.json', 'a' * 40)


class NativeWatchIntegrationTests(WatchFixture):
    def test_existing_watch_applies_the_arm_deadline_without_a_snapshot(self):
        self.watch.stop_state.started = 0
        line = '[native-ctx] event=armed us=100000000 fd=7 flags=0'
        with patch('run.time.time', return_value=100), patch('run.capture_snapshot') as snapshot:
            self.assertIsNone(self.watch._observe_frontier(50, [line], [line]))
            self.assertEqual(self.watch._observe_frontier(60, [], [])['stop_reason'],
                             'native-context-arm-deadline')
            snapshot.assert_not_called()

    def test_missing_expected_trace_is_preserved_as_inconclusive(self):
        self.watch.native_expected = True
        self.watch.final_drain('synthetic-exit')
        self.assertEqual(self.state['native_context']['trace']['coverage'], 'inconclusive')
        self.assertFalse(self.state['native_context']['window_armed'])
