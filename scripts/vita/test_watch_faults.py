import unittest

import watch_faults

NORMAL_LINES = (
    '[I|CPU] PC: 0x80000000 LR: 0x80000001',
    '[I|GX] [frame-present] frame=12 us=1791929310581478',
    '[I|MEM] read 0x4 bytes at 0x80000000',
    '[I|SYS] module gGZLE01 loaded at 0x80004000',
)


class FaultLineTests(unittest.TestCase):
    def test_recognises_each_fault_signature(self):
        lines = (
            'Unhandled SIGSEGV at address 0x0',
            'SIGSEGV',
            'PC is 0x0',
            'PC is 0x8',
            'PC is 0x80000000',
            '[E|MEM] [MemoryRead]: Invalid read at 0x00000008',
            '[E|MEM] [MemoryWrite]: Invalid write at 0x00000001',
            'guest: assertion failed: renderer queue empty',
            'panic: guest stopped on unknown import',
            "std::terminate called after throwing an instance of 'std::runtime_error'",
            'std::terminate',
        )
        for line in lines:
            with self.subTest(line=line):
                self.assertEqual(watch_faults.fault_match([line]), line)

    def test_normal_guest_lines_are_not_faults(self):
        for line in NORMAL_LINES:
            with self.subTest(line=line):
                self.assertIsNone(watch_faults.fault_match([line]))

    def test_repeated_fault_wording_is_a_flood(self):
        line = '[E|MEM] invalid access outside MEM1 at 0x00000008'
        self.assertEqual(watch_faults.fault_match([line] * watch_faults.FAULT_REPEATS),
                         line)

    def test_repeated_normal_wording_is_not_a_flood(self):
        line = '[I|GX] submitted 4 display lists'
        self.assertIsNone(watch_faults.fault_match([line] * 200))

    def test_text_input_is_split_into_lines(self):
        text = '\n'.join(NORMAL_LINES[:2] + ('PC is 0x8',))
        self.assertEqual(watch_faults.fault_match(text), 'PC is 0x8')

    def test_clean_log_has_no_match(self):
        self.assertIsNone(watch_faults.fault_match(NORMAL_LINES))

    def test_boot_audio_assert_to_no_op_is_not_a_fault(self):
        line = ('[audio-deferred] event=assert-to-no-op control=0952 pc=803190E0 '
                'action=ignored reply=none cpu_irq=none pcm_output=none')
        self.assertIsNone(watch_faults.fault_match([line]))
        self.assertIsNone(watch_faults.fault_match([line] * 200))

    def test_identical_progress_lines_are_not_a_flood(self):
        line = '[progress] turns=148871 edges=13162849 exception=00000000 resume=9403 us'
        self.assertIsNone(watch_faults.fault_match([line] * 200))


if __name__ == '__main__':
    unittest.main()
