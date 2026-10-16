"""Owned stopping transitions, with fake time and no services or signals."""

import unittest
from unittest.mock import patch

import run
import test_watchguard
from watchguard import FirstFaultWatchguard, WW56Ownership, guardian_ok


class StopTransitionTests(unittest.TestCase):
    setUp = test_watchguard.OwnershipTests.setUp

    def observe(self, sequence, *, process=None, error=None, budget=1, state=None):
        self.now = 0
        self.yields = []
        self.queries = 0
        self.index = 0

        def properties(unit):
            self.queries += 1
            current = sequence[min(self.index, len(sequence) - 1)]
            self.index += 1
            return current

        def yield_for(seconds):
            self.yields.append(seconds)
            self.now += seconds

        owner = WW56Ownership(run, 'synthetic.service', self.owned, properties,
                               lambda group: [], self.delivered.append,
                               clock=lambda: self.now, yield_for=yield_for,
                               terminal_wait_seconds=budget, poll_seconds=0.1)
        owner.state.update(state or {})
        guard = FirstFaultWatchguard(owner, lambda line: 'FAULT' in line)
        with patch.object(run, 'process_identity', return_value=process or (987, '/owned'), side_effect=error), \
                patch.object(run, 'original_process_gone', return_value=True):
            guard.confirm_cleanup()
        return guard

    def stopping(self, **changes):
        return dict(self.live, ActiveState='deactivating', SubState='stop-sigterm', **changes)

    def test_active_stopping_terminal_is_confirmed_without_signals(self):
        guard = self.observe([self.live, self.stopping(), self.terminal])
        self.assertEqual(guard.cleanup, 'confirmed')
        self.assertEqual(guard.safety, 'OK')
        self.assertEqual(self.delivered, [])
        self.assertEqual(len(self.yields), 2)

    def test_stopping_request_waits_for_terminal_without_signalling(self):
        sequence = iter([self.stopping(), self.terminal])
        owner = WW56Ownership(run, 'synthetic.service', self.owned, lambda unit: next(sequence),
                               lambda group: [], self.delivered.append)
        guard = FirstFaultWatchguard(owner, lambda line: True)
        with patch.object(run, 'process_identity', return_value=(987, '/owned')), \
                patch.object(run, 'original_process_gone', return_value=True):
            guard.observe('synthetic', 'FAULT')
        self.assertEqual(guard.disposition, 'terminal-confirmed')
        self.assertEqual(self.delivered, [])

    def test_foreign_transition_proofs_fail_immediately(self):
        for changes in ({'InvocationID': 'f' * 32}, {'MainPID': '54321'}, {'ControlGroup': '/foreign'}):
            with self.subTest(changes=changes):
                guard = self.observe([self.stopping(**changes)])
                self.assertEqual(guard.cleanup, 'blocked')
                self.assertEqual(guard.safety, 'FAILED')
                self.assertEqual(self.yields, [])
                self.assertEqual(self.delivered, [])

    def test_same_pid_foreign_kernel_start_is_rejected_immediately(self):
        guard = self.observe([self.stopping()], process=(988, '/owned'))
        self.assertEqual(guard.cleanup, 'blocked')
        self.assertEqual(self.yields, [])
        self.assertEqual(self.delivered, [])

    def test_permission_denial_is_not_absence_or_retry(self):
        guard = self.observe([self.stopping()], error=PermissionError('synthetic EACCES'))
        self.assertEqual(guard.cleanup, 'blocked')
        self.assertEqual(self.yields, [])
        self.assertEqual(guard.errors[0]['exception'][0]['type'], 'PermissionError')

    def test_missing_terminal_proof_expires_with_bounded_yields(self):
        guard = self.observe([self.stopping(MainPID='0')], error=FileNotFoundError(), budget=0.3)
        self.assertEqual(guard.cleanup, 'blocked')
        self.assertLessEqual(len(self.yields), 4)
        self.assertEqual(guard.safety, 'FAILED')
        self.assertEqual(self.delivered, [])

    def test_zero_pid_absence_requires_later_terminal_and_empty_cgroup_proof(self):
        guard = self.observe([self.stopping(MainPID='0'), self.terminal], error=FileNotFoundError())
        self.assertEqual(guard.cleanup, 'confirmed')
        self.assertEqual(self.delivered, [])

    def test_changed_original_nonce_is_rejected_before_polling(self):
        guard = self.observe([self.stopping()], state={
            'owned_identity': self.owned._replace(launch_nonce='c' * 64)})
        self.assertEqual(guard.cleanup, 'blocked')
        self.assertEqual(self.queries, 0)
        self.assertEqual(self.yields, [])

    def test_original_fatal_native_result_remains_distinct_from_cleanup(self):
        terminal = dict(self.terminal, Result='timeout', ExecMainCode='3', ExecMainStatus='6')
        intent = {'stop_reason': 'planned-ceiling'}
        guard = self.observe([self.stopping(), terminal], state={'requested_stop': intent})
        self.assertEqual(guard.cleanup, 'confirmed')
        self.assertEqual(guard.ownership.state['properties_at_exit'], terminal)
        self.assertEqual(guard.ownership.state['requested_stop'], intent)
        result = guard.result('timeout/SIG6', publication=True)
        self.assertEqual(result['native'], 'timeout/SIG6')
        self.assertFalse(guardian_ok(result))


if __name__ == '__main__':
    unittest.main()
