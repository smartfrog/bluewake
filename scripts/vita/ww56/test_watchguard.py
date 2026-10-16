"""Synthetic watchguard contracts: no services, signals, guests or private data."""

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from journal import IncrementalLog
import run
from phase_identity import OwnedIdentity
from watchguard import (FirstFaultWatchguard, WW56Ownership, guardian_ok,
                        summarize_sources, write_summary)
from test_watch_frontier import WatchFixture


class BoundedPhaseWatchTests(WatchFixture):
    def test_bounded_fault_stop_precedes_receipt_serialization(self):
        self.watch.bounded_logs = True
        first = self.phase_root / 'evidence/process.log'
        first.write_text('[fatal] synthetic\n')
        events = []
        original_open = Path.open
        original_exists = Path.exists

        def opening(path, *args, **kwargs):
            if path.name == 'cpu-log-watch.jsonl':
                self.assertEqual(events, ['signal'])
                events.append('receipt')
            return original_open(path, *args, **kwargs)

        with patch.object(Path, 'exists', lambda path: True if str(path) == '/proc/0' else original_exists(path)), \
                patch.object(Path, 'open', opening), \
                patch.object(run.subprocess, 'run', side_effect=lambda *args, **kwargs: events.append('signal')):
            self.watch(999)
        self.assertEqual(events, ['signal', 'receipt'])

    def test_explicit_bounded_adoption_stops_reading_sources_at_first_failure(self):
        self.watch.bounded_logs = True
        first, second = list(self.watch.readers.values())[:2]
        with patch.object(first, 'read_bounded', return_value=(['[fatal] synthetic'], 18)) as read, \
                patch.object(first, 'read', side_effect=AssertionError('unbounded read')), \
                patch.object(second, 'read_bounded') as later:
            lines, _ = self.watch._read_logs()
        self.assertEqual(lines, ['[fatal] synthetic'])
        read.assert_called_once()
        later.assert_not_called()

    def test_bounded_final_drain_reports_partial_and_retains_incremental_extent(self):
        self.watch.bounded_logs = True
        self.journal.write_bytes(b'ordinary\n' * 1000)
        self.watch.final_drain('post-cleanup')
        self.assertTrue(self.state['terminal_drain']['partial_scan'])
        reader = self.watch.readers[str(self.journal)]
        self.assertEqual(reader.lines, 256)
        self.watch.final_drain('post-cleanup')
        self.assertEqual(reader.lines, 512)


class BoundedJournalTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'synthetic.log'

    def test_one_chunk_and_line_budget(self):
        self.path.write_bytes(b'x\n' * 100000)
        reader = IncrementalLog(self.path)
        lines, count = reader.read_bounded(max_bytes=65536, max_lines=16)
        self.assertEqual(len(lines), 16)
        self.assertLessEqual(count, 65536)
        self.assertEqual(reader.offset, count)

    def test_long_line_clipped_bytes_counted_once(self):
        with self.path.open('wb') as stream:
            for _ in range(4096):
                stream.write(b'x' * 4096)
            stream.write(b'\n')
        reader = IncrementalLog(self.path)
        while not reader.complete:
            reader.read_bounded()
            self.assertLessEqual(len(reader.pending), 2048)
        self.assertEqual(reader.clipped_bytes, 16 * 1024 * 1024 - 2048)
        self.assertEqual(reader.clipped_lines, 1)
        self.assertEqual(reader.lines, 1)

    def test_bounded_replacement_or_truncation_refused(self):
        for replacement in (False, True):
            with self.subTest(replacement=replacement):
                self.path.write_bytes(b'original\n')
                reader = IncrementalLog(self.path)
                reader.read_bounded()
                if replacement:
                    other = self.path.with_suffix('.next')
                    other.write_bytes(b'new content\n')
                    other.replace(self.path)
                else:
                    self.path.write_bytes(b'x')
                with self.assertRaisesRegex(ValueError, 'replaced or truncated'):
                    reader.read_bounded()

    def test_permission_error_is_not_absence(self):
        with patch.object(Path, 'open', side_effect=PermissionError('synthetic denial')):
            with self.assertRaises(PermissionError):
                IncrementalLog(self.path).read_bounded()

    def test_read_request_is_at_most_one_chunk_per_call(self):
        self.path.write_bytes(b'x\n' * 100000)
        reads = []
        original_open = Path.open

        class ObservedStream:
            def __init__(self, stream):
                self.stream = stream

            def __enter__(self):
                return self

            def __exit__(self, *unused):
                self.stream.close()

            def fileno(self):
                return self.stream.fileno()

            def seek(self, offset):
                self.stream.seek(offset)

            def read(self, size):
                reads.append(size)
                return self.stream.read(size)

        with patch.object(Path, 'open', lambda path, *args: ObservedStream(original_open(path, *args))):
            reader = IncrementalLog(self.path)
            reader.read_bounded()
            self.assertEqual(reads, [65536])
            reader.read_bounded()
            self.assertEqual(reads, [65536])

    def test_pending_unterminated_final_line_is_counted_once_postflight(self):
        self.path.write_bytes(b'FAULT without newline')
        reader = IncrementalLog(self.path)
        self.assertEqual(reader.read_bounded()[0], [])
        self.assertEqual(reader.finish_bounded(), ['FAULT without newline'])
        self.assertEqual(reader.finish_bounded(), [])
        self.assertEqual(reader.lines, 1)


class SyntheticOwnership:
    def __init__(self):
        self.snapshot = {'synthetic': True}
        self.terminal = False
        self.signals = 0
        self.failure = None

    def verify(self):
        if self.failure:
            raise self.failure
        return self.terminal

    def signal(self):
        self.signals += 1


class SyntheticReader:
    def __init__(self, lines, ownership):
        self.lines = lines
        self.ownership = ownership
        self.reads = 0

    def read_bounded(self):
        self.reads += 1
        if self.reads > 1 and not self.ownership.signals:
            raise AssertionError('infinite backlog read again before STOP')
        return self.lines, 65536


class WatchguardTests(unittest.TestCase):
    def setUp(self):
        self.owner = SyntheticOwnership()
        self.guard = FirstFaultWatchguard(self.owner, lambda line: 'FAULT' in line)

    def test_infinite_backlog_stops_before_second_read_or_source_or_line(self):
        reader = SyntheticReader(['FAULT', 'must not process'], self.owner)
        second = SyntheticReader(['FAULT'], self.owner)
        observed = []
        self.guard.sample({'first': reader, 'second': second}, lambda *line: observed.append(line))
        self.assertEqual(self.owner.signals, 1)
        self.assertEqual(reader.reads, 1)
        self.assertEqual(second.reads, 0)
        self.assertEqual(observed, [])

    def test_duplicate_sources_and_samples_preserve_first_fault_and_one_signal(self):
        reader = SyntheticReader(['FAULT'], self.owner)
        self.guard.sample({'first': reader})
        original = dict(self.guard.first_fault)
        self.guard.sample({'first': reader, 'duplicate': reader})
        self.assertEqual(self.guard.first_fault, original)
        self.assertEqual(self.owner.signals, 1)

    def test_source_vanish_after_recognition_cannot_cancel_stop(self):
        self.guard.sample({'vanished': SyntheticReader(['FAULT'], self.owner)})
        self.assertEqual(self.guard.disposition, 'signal-sent')
        self.assertEqual(self.owner.signals, 1)

    def test_later_read_error_visible_without_duplicate_signal(self):
        self.guard.observe('first', 'FAULT')
        with patch.object(SyntheticReader, 'read_bounded', side_effect=PermissionError('later')):
            self.guard.sample({'second': SyntheticReader([], self.owner)})
        self.assertEqual(self.owner.signals, 1)
        self.assertEqual(self.guard.safety, 'FAILED')
        self.assertEqual(self.guard.errors[0]['exception'][0]['type'], 'PermissionError')

    def test_assertion_real_frames_and_cause_survive_successful_cleanup(self):
        def verifier():
            try:
                raise ValueError('original cause')
            except ValueError as cause:
                raise AssertionError('actual synthetic assertion') from cause
        with patch.object(self.owner, 'verify', side_effect=verifier):
            self.guard.observe('first', 'FAULT')
        self.owner.terminal = True
        self.guard.confirm_cleanup()
        result = self.guard.result('failure', True)
        self.assertEqual(result['safety'], 'FAILED')
        self.assertEqual(result['cleanup'], 'confirmed')
        error = result['errors'][0]
        self.assertEqual(error['attempt']['outcome'], 'blocked')
        self.assertEqual(error['proof'], {'synthetic': True})
        self.assertEqual(error['exception'][0]['type'], 'AssertionError')
        self.assertEqual(error['exception'][0]['frames'][-1]['function'], 'verifier')
        self.assertIn('raise AssertionError', error['exception'][0]['frames'][-1]['source'])
        self.assertEqual(error['exception'][1]['type'], 'ValueError')
        self.assertFalse(guardian_ok(result))

    def test_guardian_rejects_driver_zero_without_each_independent_success(self):
        good = dict(safety='OK', native='success', cleanup='confirmed', publication=True)
        for field, value in [('safety', 'FAILED'), ('native', 'failure'),
                             ('cleanup', 'blocked'), ('publication', False),
                             ('first_fault', {'line': 'FAULT'})]:
            with self.subTest(field=field):
                self.assertFalse(guardian_ok(dict(good, **{field: value})))

    def test_failed_delivery_never_sets_signal_sent(self):
        with patch.object(self.owner, 'signal', side_effect=OSError('delivery failed')):
            self.guard.observe('first', 'FAULT')
        self.assertEqual(self.guard.disposition, 'blocked')
        self.assertEqual(self.guard.safety, 'FAILED')


class OwnershipTests(unittest.TestCase):
    def setUp(self):
        self.owned = OwnedIdentity(12345, '/owned', 987, 'a' * 32, 'b' * 64)
        self.live = dict(LoadState='loaded', ActiveState='active', SubState='running',
                         MainPID='12345', ControlGroup='/owned', InvocationID='a' * 32)
        self.terminal = dict(self.live, MainPID='0', ControlGroup='', ActiveState='failed',
                             SubState='failed', Result='oom-kill', ExecMainCode='2', ExecMainStatus='9')
        self.delivered = []

    def adapter(self, props, members=(), guests=()):
        return WW56Ownership(run, 'synthetic.service', self.owned, lambda unit: props,
                             lambda group: list(members), self.delivered.append, guests)

    def test_same_original_invocation_terminal_absent_empty_sends_no_signal_retains_failure(self):
        adapter = self.adapter(self.terminal)
        guard = FirstFaultWatchguard(adapter, lambda line: True)
        with patch.object(run, 'original_process_gone', return_value=True):
            guard.observe('synthetic', 'FAULT')
        self.assertEqual(self.delivered, [])
        self.assertEqual(guard.disposition, 'terminal-confirmed')
        self.assertFalse(guardian_ok(guard.result('oom-kill', True)))

    def test_process_vanish_during_authentication_rechecks_same_original_terminal(self):
        observations = iter([self.live, self.terminal])
        adapter = WW56Ownership(run, 'synthetic.service', self.owned,
                                 lambda unit: next(observations), lambda group: [], self.delivered.append)
        guard = FirstFaultWatchguard(adapter, lambda line: True)
        with patch.object(run, 'process_identity', side_effect=FileNotFoundError('synthetic exit')), \
                patch.object(run, 'original_process_gone', return_value=True):
            guard.observe('synthetic', 'FAULT')
        self.assertEqual(guard.disposition, 'terminal-confirmed')
        self.assertEqual(adapter.snapshot['properties_recheck'], self.terminal)
        self.assertEqual(self.delivered, [])

    def test_missing_foreign_reused_permission_proofs_block_without_signal(self):
        cases = [dict(props=dict(self.live, InvocationID='f' * 32)),
                 dict(props=dict(self.live, MainPID='54321')),
                 dict(props=dict(self.live, ControlGroup='/foreign')),
                 dict(props=dict(self.terminal, InvocationID='')),
                 dict(props=self.live, process=(988, '/owned')),
                 dict(props=self.live, process=(987, '/foreign')),
                 dict(props=self.live, error=PermissionError('synthetic EACCES')),
                 dict(props=self.terminal, members=[12345])]
        for case in cases:
            with self.subTest(case=case):
                adapter = self.adapter(case['props'], case.get('members', ()))
                guard = FirstFaultWatchguard(adapter, lambda line: True)
                with patch.object(run, 'process_identity', return_value=case.get('process', (987, '/owned')),
                                  side_effect=case.get('error')), \
                        patch.object(run, 'original_process_gone', return_value=True):
                    guard.observe('synthetic', 'FAULT')
                self.assertEqual(guard.disposition, 'blocked')
                self.assertEqual(guard.safety, 'FAILED')
                self.assertEqual(self.delivered, [])

    def test_terminal_supervisor_active_guest_is_blocked_not_clean(self):
        guest = self.adapter(self.live)
        adapter = self.adapter(self.terminal, guests=(guest,))
        guard = FirstFaultWatchguard(adapter, lambda line: True)
        with patch.object(run, 'original_process_gone', return_value=True), \
                patch.object(run, 'process_identity', return_value=(987, '/owned')):
            guard.observe('synthetic', 'FAULT')
        self.assertEqual(guard.cleanup, 'blocked')
        self.assertEqual(self.delivered, [])


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'synthetic.log'

    def test_millions_generated_fault_lines_duplicate_sources_bounded_samples_exact_counts(self):
        block = b'FAULT synthetic\n' * 4096
        with self.path.open('wb') as stream:
            for _ in range(256):
                stream.write(block)
        result = summarize_sources([self.path, self.path], lambda line: 'FAULT' in line,
                                   stopped_and_clean=True)
        for source in result['sources']:
            self.assertEqual(source['matches'], 1048576)
            self.assertEqual(source['lines'], 1048576)
            self.assertEqual(source['bytes_read'], len(block) * 256)
            self.assertEqual(source['scanned_bytes'], source['bytes_read'])
            self.assertEqual(len(source['first']), 16)
            self.assertEqual(len(source['tail']), 16)
            self.assertTrue(source['complete'])
        self.assertLessEqual(len(json.dumps(result).encode()), 256 * 1024)

    def test_interrupted_scan_reports_partial_extent_and_hash(self):
        self.path.write_bytes(b'FAULT\n' * 100000)
        calls = iter([True, False])
        result = summarize_sources([self.path], lambda line: True, stopped_and_clean=True,
                                   continue_scan=lambda: next(calls))['sources'][0]
        self.assertFalse(result['complete'])
        self.assertLess(result['scanned_bytes'], self.path.stat().st_size)
        self.assertEqual(len(result['sha256_read_extent']), 64)

    def test_postflight_before_cleanup_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'cleanup'):
            summarize_sources([], lambda line: True, stopped_and_clean=False)

    def test_maximum_escaped_samples_obey_json_budget(self):
        self.path.write_bytes((b'FAULT' + b'\x00' * 2043 + b'\n') * 40)
        result = summarize_sources([self.path] * 8, lambda line: True, stopped_and_clean=True)
        self.assertLessEqual(len(json.dumps(result).encode()), 256 * 1024)

    def test_first_tail_overlap_not_duplicated(self):
        self.path.write_bytes(b'FAULT\n' * 17)
        result = summarize_sources([self.path], lambda line: True, stopped_and_clean=True)['sources'][0]
        self.assertEqual(len(result['first']) + len(result['tail']), 17)

    def test_summary_write_error_propagates(self):
        with patch.object(Path, 'open', side_effect=PermissionError('synthetic write denial')):
            with self.assertRaises(PermissionError):
                write_summary(self.path, {})


if __name__ == '__main__':
    unittest.main()
