import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import watch_guest
from watch_faults import fault_match


class VerdictTests(unittest.TestCase):
    def test_high_cpu_without_a_present_is_hung(self):
        self.assertEqual(
            watch_guest.verdict(cpu=0.9, silent_s=watch_guest.HANG_SECONDS + 1, idle_s=0,
                                since_present_s=watch_guest.IDLE_SILENT_SECONDS + 1),
            'hung-no-present')

    def test_high_cpu_with_a_growing_log_is_not_hung(self):
        self.assertIsNone(
            watch_guest.verdict(cpu=1.29, silent_s=0.0, idle_s=0, since_present_s=40.0))

    def test_recent_present_at_high_cpu_is_healthy(self):
        self.assertIsNone(
            watch_guest.verdict(cpu=0.9, silent_s=1, idle_s=0,
                                since_present_s=watch_guest.HANG_SECONDS - 1))

    def test_low_cpu_and_a_silent_log_is_hung(self):
        self.assertEqual(
            watch_guest.verdict(cpu=0.01, silent_s=watch_guest.IDLE_SILENT_SECONDS + 1,
                                idle_s=watch_guest.IDLE_SILENT_SECONDS + 1, since_present_s=0),
            'hung-low-cpu-silent')

    def test_low_cpu_during_shader_pre_compilation_is_not_judged(self):
        self.assertIsNone(
            watch_guest.verdict(cpu=0.00, silent_s=watch_guest.HANG_SECONDS + 1,
                                idle_s=watch_guest.HANG_SECONDS + 1, since_present_s=0))

    def test_low_cpu_while_the_log_grows_is_healthy(self):
        self.assertIsNone(
            watch_guest.verdict(cpu=0.01, silent_s=2,
                                idle_s=watch_guest.IDLE_SILENT_SECONDS + 1, since_present_s=0))

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


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)
        self.fixture = self.root / watch_guest.FIXTURE_DIR
        self.fixture.mkdir(parents=True)
        (self.fixture / 'ww05.request').write_text('request\n')

    def tearDown(self):
        self.temporary.cleanup()

    def test_request_only_fixture_passes(self):
        self.assertIsNone(watch_guest.preflight(self.root))

    def test_missing_request_is_named(self):
        (self.fixture / 'ww05.request').unlink()
        self.assertEqual(watch_guest.preflight(self.root), 'missing ww05.request')

    def test_continue_with_audio_passes(self):
        for name in ('ww05.continue', 'ww05.audio'):
            (self.fixture / name).write_text('request\n')
        self.assertIsNone(watch_guest.preflight(self.root))

    def test_audio_needs_continue(self):
        (self.fixture / 'ww05.audio').write_text('audio\n')
        self.assertEqual(watch_guest.preflight(self.root), 'ww05.audio needs ww05.continue')

    def test_previous_outputs_are_named(self):
        for name in ('ww05.dispatch', 'ww05.log', 'ww05.partial', 'ww05.report'):
            with self.subTest(present=name):
                path = self.fixture / name
                path.write_text('output\n')
                self.assertEqual(watch_guest.preflight(self.root), f'present {name}')
                path.unlink()

    def test_required_file_is_reported_before_a_previous_output(self):
        (self.fixture / 'ww05.request').unlink()
        (self.fixture / 'ww05.dispatch').write_text('output\n')
        self.assertEqual(watch_guest.preflight(self.root), 'missing ww05.request')


class WatchPreflightTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_incomplete_fixture_ends_the_watch_without_a_guest(self):
        fixture = self.root / watch_guest.FIXTURE_DIR
        fixture.mkdir(parents=True)
        (fixture / 'ww05.audio').write_text('audio\n')
        self.assertEqual(watch_guest.watch(self.root), 1)
        self.assertEqual((self.root / 'live-end.txt').read_text(),
                         'reason=preflight-failed\ndetail=missing ww05.request\n')
        self.assertFalse((self.root / 'cpu-samples.jsonl').exists())


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


class FaultMatchTests(unittest.TestCase):
    def test_a_fault_after_a_case_changing_character_is_still_found(self):
        # 'İ'.lower() is two characters: that used to shift the skipped prefix.
        self.assertIsNotNone(fault_match('ok\n' + 'İ' * 8 + 'SIGSEGV\n'))

    def test_a_healthy_chunk_with_no_fault_word_is_not_a_fault(self):
        self.assertIsNone(fault_match('[frame-present] frame=1\n' * 10))


class TruncationTests(unittest.TestCase):
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


class StaleConsoleTests(unittest.TestCase):
    def console(self, tmp, text):
        console = Path(tmp) / watch_guest.CONSOLE_LOG
        console.parent.mkdir(parents=True, exist_ok=True)
        console.write_text(text)
        return Path(tmp)

    def test_a_console_log_that_already_records_a_fault_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.console(tmp, '[MemoryRead]: Invalid read of uint32_t at 0x8\n')
            self.assertIsNotNone(watch_guest.stale_console(root))

    def test_a_console_log_without_a_fault_is_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.console(tmp, '[frame-present] frame=1\n')
            self.assertIsNone(watch_guest.stale_console(root))

    def test_a_fault_in_the_middle_of_a_large_log_is_found(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.console(tmp, '[frame-present] frame=1\n' * 400000
                                + '[MemoryRead]: Invalid read of uint32_t at 0x8\n'
                                + '[frame-present] frame=2\n' * 400000)
            self.assertIsNotNone(watch_guest.stale_console(root))

    def test_a_large_log_without_a_fault_is_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.console(tmp, '[frame-present] frame=1\n' * 800000)
            self.assertIsNone(watch_guest.stale_console(root))


if __name__ == '__main__':
    unittest.main()
