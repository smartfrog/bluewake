import unittest

import capture


def command(index, opcode, data):
    return (f'[gx-command] index={index} opcode={opcode:02X} bytes={len(data)} '
            f'pc=80320000 completion_pc=80320004 sp=80400000 hex={data.hex().upper()}')


class JournalTests(unittest.TestCase):
    def test_journal_replays_all_commands(self):
        bp = bytes.fromhex('610F0000FF')
        cp = bytes.fromhex('085000000200')
        kind, data = capture.classify_capture(
            (command(1, 0x61, bp) + '\n' + command(2, 0x08, cp)).encode())
        self.assertEqual((kind, data), ('journal', bp + cp))

    def test_journal_takes_precedence_over_frontier(self):
        bp = bytes.fromhex('610F0000FF')
        text = ('GX_FRONTIER complete=1 opcode=61 bytes=5 pc=80320000 '
                'completion_pc=80320004 sp=80400000 trailing=0 edge_stop=0 hex=610F0000FF\n'
                + command(1, 0x61, bp))
        self.assertEqual(capture.classify_capture(text.encode()), ('journal', bp))

    def test_missing_command_is_refused(self):
        text = command(1, 0, b'\0') + '\n' + command(3, 0, b'\0')
        with self.assertRaisesRegex(ValueError, 'sequence'):
            capture.classify_capture(text.encode())

    def test_wrong_opcode_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'opcode'):
            capture.classify_capture(command(1, 0x08, bytes.fromhex('610F0000FF')).encode())

    def test_wrong_length_is_refused(self):
        text = command(1, 0x61, bytes.fromhex('610F0000FF')).replace('bytes=5', 'bytes=6')
        with self.assertRaisesRegex(ValueError, 'byte count'):
            capture.classify_capture(text.encode())

    def test_malformed_record_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'malformed'):
            capture.classify_capture(b'[gx-command] index=1 hex=AA')


if __name__ == '__main__':
    unittest.main()
