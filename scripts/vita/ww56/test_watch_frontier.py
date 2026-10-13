"""Tests for event-driven frontier ingestion and the exit-race drain.

Real marker placement, taken verbatim from the WW50 receipts (read-only):
the GPU line arrives on the runtime/process log, while `[frame-present]` and
`[rel-frontier]` arrive in the guest journal. A presentation that lands just
before the process exits must still be counted, and its snapshot recorded as
missed rather than lost.
"""

from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

import run

GPU_LINE = '[00:08:23.745] |I| [create]: GPU = Apple M1 Pro (G13S C0)'
PRESENT_ONE = '[frame-present] frame=1 us=1791929310581478'
PRESENT_TWO = '[frame-present] frame=2 us=1791929310690334'
REL_LINE = '[rel-frontier] name=f_pc_profile_lst object=803A71C0 module=00000000 pc=80240744'
SUMMARY = {'totals': {'Rss': 1}, 'duration_seconds': 0.0}


class WatchFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)
        self.runroot = self.root / 'run'
        self.receipts = self.root / 'receipts'
        self.journal = self.runroot / run.REPORT_DIRECTORY / 'ww05.log'
        self.journal.parent.mkdir(parents=True)
        self.receipts.mkdir()
        self.phase_root = self.runroot / 'phases' / 'guest'
        (self.phase_root / 'evidence').mkdir(parents=True)
        self.entry = {'evidence_root': str(self.phase_root)}
        self.args = SimpleNamespace(hang_seconds=30.0, present_grace_seconds=10.0, rel_settle_seconds=5.0)
        self.state = {'pid': 0, 'cgroup': 'test', 'snapshots': [], 'presentations': [],
                      'released': True, 'phase_root': str(self.phase_root)}
        self.watch = run.PhaseWatch('guest', self.args, self.runroot, self.receipts,
                                    {'phases': {'guest': self.entry}}, self.entry,
                                    'ww56-test-guest.service', 4 * 1024 ** 3, 4 * 1024 ** 3,
                                    180.0, self.state)

    def tearDown(self):
        self.temporary.cleanup()


class LogSourceTests(WatchFixture):
    def test_gpu_trigger_fires_from_the_runtime_log_not_the_journal(self):
        with patch.object(run, 'capture_snapshot', return_value=SUMMARY) as capture:
            self.watch._observe_frontier(100.0, [GPU_LINE], [])
            capture.assert_called_once()
            self.assertEqual(capture.call_args.kwargs['event'], 'gpu-context-created')
            self.watch._observe_frontier(101.0, [GPU_LINE], [])
            capture.assert_called_once()

    def test_present_trigger_fires_once_from_the_journal_and_counts_the_series(self):
        with patch.object(run, 'capture_snapshot', return_value=SUMMARY) as capture:
            self.watch._observe_frontier(100.0, [PRESENT_ONE], [PRESENT_ONE])
            capture.assert_called_once()
            self.assertEqual(capture.call_args.kwargs['event'], 'first-actual-frame-present')
            self.watch._observe_frontier(101.0, [PRESENT_TWO], [PRESENT_TWO])
            capture.assert_called_once()
        self.assertEqual([event['index'] for event in self.state['presentations']], [1, 2])

    def test_a_trigger_without_a_released_process_is_recorded_missed(self):
        self.state['pid'] = None
        self.watch._observe_frontier(100.0, [GPU_LINE], [])
        self.assertEqual(self.state['snapshots'],
                         [{'event': 'gpu-context-created', 'missed': 'process never released'}])


class ExitRaceDrainTests(WatchFixture):
    def test_final_drain_counts_a_presentation_after_the_process_exits(self):
        self.journal.write_text(PRESENT_ONE + '\n' + REL_LINE + '\n')
        self.watch.final_drain('managed-native-exit')
        self.assertEqual(len(self.state['presentations']), 1)
        self.assertEqual(self.state['first_rel'], REL_LINE)
        self.assertEqual(self.state['snapshots'][0]['event'], 'first-actual-frame-present')
        self.assertIn('missed', self.state['snapshots'][0])
        drain = self.state['terminal_drain']
        self.assertEqual(drain['reason'], 'managed-native-exit')
        self.assertEqual(drain['presentations'], 1)
        self.assertEqual(drain['last_present']['index'], 1)
        watch_log = (self.receipts / 'cpu-log-watch.jsonl').read_text()
        self.assertIn('managed-native-exit', watch_log)

    def test_final_drain_is_idempotent(self):
        self.journal.write_text(PRESENT_ONE + '\n')
        self.watch.final_drain('first')
        self.watch.final_drain('second')
        self.assertEqual(len(self.state['presentations']), 1)
        self.assertEqual(len(self.state['snapshots']), 1)
        self.assertEqual(self.state['terminal_drain']['reason'], 'first')

    def test_native_exit_drains_the_logs_before_returning(self):
        self.journal.write_text(PRESENT_ONE + '\n')
        with patch.object(run, 'properties', return_value={'Result': 'success'}), \
                patch.object(run.subprocess, 'run') as stop:
            observation = self.watch(12345)
        self.assertEqual(observation['stop_reason'], 'managed-native-exit')
        self.assertEqual(len(self.state['presentations']), 1)
        self.assertIn('missed', self.state['snapshots'][0])
        stop.assert_not_called()


if __name__ == '__main__':
    unittest.main()
