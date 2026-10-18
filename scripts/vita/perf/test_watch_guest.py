import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import watch_guest


class VerdictTests(unittest.TestCase):
    def test_high_cpu_without_a_present_is_hung(self):
        self.assertEqual(
            watch_guest.verdict(cpu=0.9, silent_s=1, idle_s=0,
                                since_present_s=watch_guest.HANG_SECONDS + 1),
            'hung-no-present')

    def test_recent_present_at_high_cpu_is_healthy(self):
        self.assertIsNone(
            watch_guest.verdict(cpu=0.9, silent_s=1, idle_s=0,
                                since_present_s=watch_guest.HANG_SECONDS - 1))

    def test_low_cpu_and_a_silent_log_is_hung(self):
        self.assertEqual(
            watch_guest.verdict(cpu=0.01, silent_s=watch_guest.HANG_SECONDS + 1,
                                idle_s=watch_guest.HANG_SECONDS + 1, since_present_s=0),
            'hung-low-cpu-silent')

    def test_low_cpu_while_the_log_grows_is_healthy(self):
        self.assertIsNone(
            watch_guest.verdict(cpu=0.01, silent_s=2,
                                idle_s=watch_guest.HANG_SECONDS + 1, since_present_s=0))

    def test_mid_cpu_without_a_present_is_not_judged(self):
        self.assertIsNone(
            watch_guest.verdict(cpu=0.3, silent_s=1, idle_s=0,
                                since_present_s=watch_guest.HANG_SECONDS + 1))


class ReadNewTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.log = Path(self.temporary.name) / 'ww05.log'

    def tearDown(self):
        self.temporary.cleanup()

    def test_keeps_an_unfinished_line_until_its_newline_arrives(self):
        self.log.write_text('one\ntwo-par')
        lines, offset, pending = watch_guest.read_new(self.log, 0, '')
        self.assertEqual((lines, pending), (['one'], 'two-par'))
        with self.log.open('a') as stream:
            stream.write('tial\n')
        lines, _, pending = watch_guest.read_new(self.log, offset, pending)
        self.assertEqual((lines, pending), (['two-partial'], ''))


class EndNoteTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_records_a_guest_fault_and_the_matched_line(self):
        line = '[E|MEM] [MemoryRead]: Invalid read at 0x00000008'
        path = watch_guest.write_end(self.root, 'guest-fault', line)
        self.assertEqual(path, self.root / 'live-end.txt')
        self.assertEqual(path.read_text(),
                         f'reason=guest-fault\ndetail={line}\n')


class StopGuestTests(unittest.TestCase):
    def test_terminates_a_guest_with_sigterm(self):
        guest = subprocess.Popen(['sleep', '30'])
        started = time.monotonic()
        watch_guest.stop_guest(guest.pid)
        elapsed = time.monotonic() - started
        guest.wait(timeout=1)
        self.assertLess(elapsed, 1)

    def test_kills_a_guest_that_ignores_sigterm(self):
        guest = subprocess.Popen([
            sys.executable, '-c',
            'import signal, time; signal.signal(signal.SIGTERM, signal.SIG_IGN);'
            ' print("ready", flush=True); time.sleep(30)'],
            stdout=subprocess.PIPE, text=True)
        self.assertEqual(guest.stdout.readline().strip(), 'ready')
        guest.stdout.close()
        started = time.monotonic()
        watch_guest.stop_guest(guest.pid)
        elapsed = time.monotonic() - started
        self.assertLess(elapsed, 1.5)
        guest.wait(timeout=1)
        self.assertEqual(guest.returncode, -signal.SIGKILL)


if __name__ == '__main__':
    unittest.main()


class ReadNewTests(unittest.TestCase):
    def test_offset_resets_when_the_log_truncates(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'vita3k.log'
            log.write_text('x' * 100 + '\n')
            _, offset, _ = watch_guest.read_new(log, 0)
            self.assertEqual(offset, 101)

            log.write_text('[MemoryRead]: Invalid read at 0x8\n')
            lines, offset, pending = watch_guest.read_new(log, offset)

            self.assertEqual(offset, len('[MemoryRead]: Invalid read at 0x8\n'))
            self.assertIn('[MemoryRead]: Invalid read at 0x8', lines)
            self.assertEqual(pending, '')
