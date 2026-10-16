"""Public synthetic gate receipts; exec and kernel observations are faked."""

from contextlib import ExitStack
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import exec_gate


class GateReceiptTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.cgroup = '/user.slice/user-1000.slice/user@1000.service/app.slice/synthetic.service'
        self.nonce = 'b' * 64
        self.invocation = 'a' * 32
        self.spec = {'unit': 'synthetic.service', 'budget_bytes': 1024,
                     'permit_timeout_seconds': 1, 'cwd': str(self.root),
                     'command': ['synthetic-only'], 'environment': {}}
        (self.root / 'guest-exec.json').write_text(json.dumps(self.spec))
        (self.root / 'guest-permit').touch()

    def gate(self, invocation=None, nonce=None):
        original = Path.read_text

        def kernel(path, *args, **kwargs):
            if str(path) == '/proc/self/cgroup':
                return '0::' + self.cgroup + '\n'
            if path.name == 'memory.max':
                return '1024\n'
            if path.name == 'memory.swap.max':
                return '0\n'
            return original(path, *args, **kwargs)

        with ExitStack() as stack:
            stack.enter_context(patch.object(Path, 'read_text', kernel))
            stack.enter_context(patch.object(exec_gate.sys, 'argv',
                                             ['exec_gate.py', str(self.root), 'guest', nonce or self.nonce]))
            stack.enter_context(patch.dict(os.environ, {'INVOCATION_ID': self.invocation
                                                        if invocation is None else invocation}))
            stack.enter_context(patch.object(exec_gate.os, 'getpid', return_value=12345))
            stack.enter_context(patch.object(exec_gate, 'process_identity', return_value=(987, self.cgroup)))
            stack.enter_context(patch.object(exec_gate.os, 'chdir'))
            execute = stack.enter_context(patch.object(exec_gate.os, 'execve'))
            exec_gate.main()
        return execute

    def test_receipt_binds_launch_nonce_and_manager_invocation_with_private_mode(self):
        old_umask = os.umask(0o022)
        try:
            execute = self.gate()
        finally:
            os.umask(old_umask)
        ready = self.root / 'guest-ready.json'
        self.assertEqual(json.loads(ready.read_text()),
                         {'pid': 12345, 'cgroup': self.cgroup, 'starttime': 987,
                          'invocation_id': self.invocation, 'launch_nonce': self.nonce})
        self.assertEqual(ready.stat().st_mode & 0o777, 0o600)
        execute.assert_called_once_with('synthetic-only', ['synthetic-only'], {})

    def test_missing_or_invalid_manager_invocation_creates_no_receipt(self):
        for invocation in ('', 'invalid', '0' * 32):
            with self.subTest(invocation=invocation):
                with self.assertRaisesRegex(ValueError, 'invocation'):
                    self.gate(invocation=invocation)
                self.assertFalse((self.root / 'guest-ready.json').exists())

    def test_invalid_launch_nonce_creates_no_receipt(self):
        with self.assertRaisesRegex(ValueError, 'nonce'):
            self.gate(nonce='invalid')
        self.assertFalse((self.root / 'guest-ready.json').exists())

    def test_existing_ready_receipt_is_not_overwritten(self):
        ready = self.root / 'guest-ready.json'
        ready.write_text('preserve earlier receipt\n')
        with self.assertRaises(FileExistsError):
            self.gate()
        self.assertEqual(ready.read_text(), 'preserve earlier receipt\n')

    def test_ready_name_is_published_only_after_complete_private_receipt(self):
        link = os.link
        observed = []

        def publish(source, destination):
            self.assertFalse(destination.exists())
            self.assertEqual(json.loads(source.read_text())['invocation_id'], self.invocation)
            self.assertEqual(source.stat().st_mode & 0o777, 0o600)
            observed.append(destination)
            link(source, destination)

        with patch.object(exec_gate.os, 'link', side_effect=publish):
            self.gate()
        self.assertEqual(observed, [self.root / 'guest-ready.json'])
        self.assertFalse((self.root / 'guest-ready.tmp').exists())


if __name__ == '__main__':
    unittest.main()
