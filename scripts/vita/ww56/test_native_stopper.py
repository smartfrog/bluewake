import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import tempfile
import time
import unittest


HERE = Path(__file__).resolve().parent


@unittest.skipUnless(platform.system() == 'Linux' and platform.machine() == 'aarch64',
                     'Linux AArch64 ptrace integration')
class StopperIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = None
        if os.environ.get('WW123_DRY_ROOT'):
            cls.root = Path(os.environ['WW123_DRY_ROOT'])
            cls.root.mkdir(mode=0o700)
        else:
            cls.temporary = tempfile.TemporaryDirectory(
                prefix='ww123-', dir='/tmp/opencode' if Path('/tmp/opencode').is_dir() else None)
            cls.root = Path(cls.temporary.name)
        cls.binary = cls.root / 'ww123-synthetic'
        subprocess.run(['cc', '-g', '-O0', '-fPIE', '-pie', '-fno-omit-frame-pointer', '-pthread',
                        str(HERE / 'stopper_fixture.c'), '-o', str(cls.binary)], check=True)
        cls.sha = hashlib.sha256(cls.binary.read_bytes()).hexdigest()

    @classmethod
    def tearDownClass(cls):
        if cls.temporary is not None:
            cls.temporary.cleanup()

    def setUp(self):
        self.target = None
        self.stopper_process = None

    def tearDown(self):
        if self.stopper_process is not None and self.stopper_process.poll() is None:
            self.stopper_process.send_signal(signal.SIGTERM)
            self.stopper_process.communicate(timeout=15)
        if self.target is not None:
            if self.target.poll() is None:
                os.kill(self.target.pid, signal.SIGCONT)
            self.target.communicate(timeout=10)

    def start_stopper(self, scenario, *extra):
        self.target = subprocess.Popen([str(self.binary), scenario], stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
        self.assertEqual(self.target.stdout.readline().strip(), 'READY')
        output = self.root / (self._testMethodName + '-capture')
        command = ['python3', '-B', str(HERE / 'native_stopper.py'),
                   '--pid', str(self.target.pid), '--exe', str(self.binary),
                   '--sha256', self.sha, '--profile', 'native',
                   '--trigger', 'callback-breakpoint', '--allow-hardware-breakpoint',
                   '--break-symbol', 'ww123_read', '--break-offset', '0x34',
                   '--out', str(output), '--wait-seconds', '3', *extra]
        self.stopper_process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return self.stopper_process, output

    def finish_stopper(self, process):
        stdout, stderr = process.communicate(timeout=15)
        (self.root / (self._testMethodName + '-tool.log')).write_text(stdout + stderr)
        return subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)

    def run_stopper(self, scenario, *extra):
        process, output = self.start_stopper(scenario, *extra)
        result = self.finish_stopper(process)
        self.assertTrue((output / 'receipt.json').exists(), result.stderr)
        return result, json.loads((output / 'receipt.json').read_text()), output

    def expect_target_exit(self, marker, *, resume=True):
        if resume:
            os.kill(self.target.pid, signal.SIGCONT)
        stdout, stderr = self.target.communicate(timeout=8)
        result = dict(pid=self.target.pid, returncode=self.target.returncode, stdout=stdout, stderr=stderr)
        (self.root / (self._testMethodName + '-target.json')).write_text(json.dumps(result, indent=2) + '\n')
        self.assertEqual(self.target.returncode, 0, stderr)
        self.assertIn(marker, stdout)
        return stdout

    def wait_armed(self, process, output):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if (output / 'events.json').exists():
                events = json.loads((output / 'events.json').read_text())
                if any(event['event'] == 'armed' for event in events):
                    return
            self.assertIsNone(process.poll())
            time.sleep(0.01)
        self.fail('stopper did not arm')

    def test_breakpoint_on_new_thread_captures_then_detaches_stopped(self):
        result, receipt, output = self.run_stopper('hit')
        self.assertEqual(result.returncode, 0, result.stderr + json.dumps(receipt))
        self.assertEqual(receipt['outcome'], 'captured')
        self.assertEqual(receipt['invalid_address'], 0xd71)
        self.assertGreaterEqual(len(receipt['threads']), 2)
        milestones = json.loads((output / 'events.json').read_text())
        initial = next(event['tids'] for event in milestones if event['event'] == 'armed')
        self.assertNotIn(receipt['hit_tid'], initial)
        events = [json.loads(line) for line in (output / 'events.jsonl').read_text().splitlines()]
        self.assertTrue(any(event.get('ptrace_event') == 3 for event in events))
        self.assertTrue(receipt['cleanup']['debug_registers_restored'])
        self.assertTrue(receipt['cleanup']['detached'], receipt)
        self.assertTrue(receipt['cleanup']['group_stopped'], receipt)
        self.assertIn('ww123_read', (output / 'eu-stack.txt').read_text())
        self.assertIn('TracerPid:\t0', Path(f'/proc/{self.target.pid}/status').read_text())
        self.assertIsNone(self.target.poll())
        self.expect_target_exit('AFTER_TRIGGER')

    def test_timeout_restores_debug_registers_without_killing_target(self):
        result, receipt, _ = self.run_stopper('timeout', '--wait-seconds', '0.15')
        self.assertEqual(result.returncode, 2)
        self.assertEqual(receipt['outcome'], 'error')
        self.assertIn('deadline', receipt['error'])
        self.assertTrue(receipt['cleanup']['detached'], receipt)
        self.assertTrue(receipt['cleanup']['group_stopped'], receipt)
        self.expect_target_exit('DONE')

    def test_native_fault_timeout_detaches_without_debug_register_writes(self):
        result, receipt, _ = self.run_stopper('timeout', '--trigger', 'native-fault', '--wait-seconds', '0.15')
        self.assertEqual(result.returncode, 2)
        self.assertEqual(receipt['cleanup']['debug_restore'], [])
        self.assertTrue(receipt['cleanup']['detached'], receipt)
        self.assertTrue(receipt['cleanup']['group_stopped'], receipt)
        self.expect_target_exit('DONE')

    def test_sigterm_to_stopper_detaches_target_stopped(self):
        process, output = self.start_stopper('timeout')
        self.wait_armed(process, output)
        process.send_signal(signal.SIGTERM)
        result = self.finish_stopper(process)
        receipt = json.loads((output / 'receipt.json').read_text())
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertTrue(receipt['cleanup']['detached'], receipt)
        self.assertTrue(receipt['cleanup']['group_stopped'], receipt)
        self.assertTrue(receipt['cleanup']['debug_registers_restored'], receipt)
        self.expect_target_exit('DONE')

    def test_wrong_invalid_address_fails_closed_and_target_can_resume(self):
        result, receipt, _ = self.run_stopper('mismatch')
        self.assertEqual(result.returncode, 2)
        self.assertEqual(receipt['invalid_address'], 0xd75)
        self.assertTrue(receipt['cleanup']['detached'], receipt)
        self.assertTrue(receipt['cleanup']['group_stopped'], receipt)
        self.expect_target_exit('AFTER_TRIGGER')

    def test_native_fault_is_forwarded_to_target_handler_before_callback_stop(self):
        result, receipt, output = self.run_stopper('fault')
        self.assertEqual(result.returncode, 0, result.stderr + json.dumps(receipt))
        events = [json.loads(line) for line in (output / 'events.jsonl').read_text().splitlines()]
        self.assertTrue(any(event['event'] == 'native-fault-forwarded' for event in events))
        self.assertEqual(receipt['preceding_native_fault_same_thread_not_necessarily_same_access']['address'], 0xd71)
        stdout = self.expect_target_exit('AFTER_TRIGGER')
        self.assertIn('NATIVE_FAULT_HANDLED', stdout)

    def test_wrong_binary_hash_refuses_without_stopping_target(self):
        result, receipt, _ = self.run_stopper('hit', '--sha256', '0' * 64)
        self.assertEqual(result.returncode, 2)
        self.assertIn('SHA-256 mismatch', receipt['error'])
        self.assertNotIn('cleanup', receipt)
        self.expect_target_exit('AFTER_TRIGGER', resume=False)

    def test_native_fault_trigger_survives_tracer_sigkill_on_armed_thread(self):
        process, output = self.start_stopper('safe', '--trigger', 'native-fault', '--fault-address', '0xd71')
        self.wait_armed(process, output)
        process.kill()
        self.finish_stopper(process)
        self.expect_target_exit('AFTER_TRIGGER')

    def test_native_fault_trigger_captures_load_then_detaches_stopped(self):
        result, receipt, output = self.run_stopper('fault', '--trigger', 'native-fault', '--fault-address', '0xd71')
        self.assertEqual(result.returncode, 0, result.stderr + json.dumps(receipt))
        self.assertEqual(receipt['trigger'], 'native-fault')
        self.assertIn('native_load_pc', receipt)
        self.assertIsNone(receipt['native_return_pc'])
        self.assertFalse(receipt['debug_register_writes'])
        self.assertEqual(receipt['cleanup']['debug_restore'], [])
        self.assertTrue(receipt['cleanup']['group_stopped'])
        self.assertTrue((output / 'native-load-code.bin').exists())
        stdout = self.expect_target_exit('NATIVE_FAULT_HANDLED')
        self.assertIn('AFTER_TRIGGER', stdout)


if __name__ == '__main__':
    unittest.main()
