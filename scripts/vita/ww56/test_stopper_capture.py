import errno
import json
import os
from pathlib import Path
import platform
import shutil
import threading
from types import SimpleNamespace
from unittest import mock

from test_stopper_state import VitaStateFixture

if platform.system() == 'Linux':
    from native_stopper import native_capture


class NativeCaptureTests(VitaStateFixture):
    def setUp(self):
        super().setUp()
        self.finished = threading.Event()
        self.thread = threading.Thread(target=self.finished.wait)
        self.thread.start()
        self.fault_tid, self.other_tid = self.thread.native_id, os.getpid()
        self.pack(self.impl + 8, '<Q', self.callback)
        self.fault_regs = dict(x24=0, x28=self.state, sp=self.run_sp,
                               pc=self.code, x29=0, x30=self.code + 4)
        self.other_sp = self.base + 0xa000
        self.other_regs = dict(self.fault_regs, sp=self.other_sp)
        self.stopper = SimpleNamespace(
            pid=os.getpid(), out=self.out, tasks={self.other_tid: {}, self.fault_tid: {}},
            trigger='native-fault', native_faults={self.fault_tid: dict(address=0xd71, registers=self.fault_regs)})
        self.receipt = dict(profile='vita')
        self.original_pread = os.pread
        self.reads = []
        self.short_address = None
        self.short_size = None
        self.short_attempts = None
        self.short_reads = []
        self.read_error = None

    def tearDown(self):
        self.finished.set()
        self.thread.join(timeout=5)
        if os.environ.get('STOPPER_CAPTURE_RECEIPTS'):
            destination = Path(os.environ['STOPPER_CAPTURE_RECEIPTS']) / self._testMethodName
            shutil.copytree(self.out, destination)
            (destination / 'capture-receipt.json').write_text(json.dumps(self.receipt, indent=2) + '\n')
            (destination / 'read-trace.json').write_text(json.dumps(self.reads) + '\n')
        super().tearDown()

    def pread(self, fd, size, address):
        self.reads.append((address, size))
        if (address == self.short_address and (self.short_size is None or size == self.short_size)
                and self.short_attempts != 0):
            self.short_reads.append((address, size))
            if self.short_attempts is not None:
                self.short_attempts -= 1
            if self.read_error is not None:
                raise self.read_error
            return self.original_pread(fd, min(size // 2, 32), address)
        return self.original_pread(fd, size, address)

    def capture(self):
        with mock.patch('native_stopper.registers', side_effect=lambda tid: self.fault_regs
                        if tid == self.fault_tid else self.other_regs), \
                mock.patch('native_stopper.regset', return_value=b'fpregs'), \
                mock.patch('stopper_state.os.pread', side_effect=self.pread):
            native_capture(self.stopper, self.fault_tid, self.receipt)

    def test_unrelated_short_stack_read_preserves_decisive_capture_and_prefix(self):
        self.short_address = self.other_sp
        self.capture()
        self.assertTrue(self.receipt['decisive_capture_complete'])
        self.assertTrue(self.receipt['capture_partial'])
        partial = self.receipt['partial_windows'][0]
        self.assertEqual(partial['tid'], self.other_tid)
        self.assertEqual(partial['status'], 'partial')
        self.assertEqual(partial['attempts'], 2)
        self.assertEqual(partial['size'], 32)
        self.assertGreater(partial['expected_size'], 32)
        self.assertEqual((self.out / partial['file']).stat().st_size, 32)
        self.assertIn('rsb_candidate', self.receipt['vita'])
        self.assertTrue((self.out / 'reverse-block-map.json').is_file())
        index = json.loads((self.out / 'memory-index.json').read_text())
        self.assertEqual(next(item for item in index if item['file'] == partial['file'])['status'], 'partial')

    def test_unrelated_stack_io_error_is_recorded_and_capture_continues(self):
        self.short_address = self.other_sp
        self.read_error = OSError(errno.EIO, 'synthetic stack read failure')
        self.capture()
        self.assertTrue(self.receipt['decisive_capture_complete'])
        partial = self.receipt['partial_windows'][0]
        self.assertEqual(partial['attempts'], 2)
        self.assertIsNone(partial['file'])
        self.assertIn('synthetic stack read failure', partial['errors'][0])

    def test_decisive_state_precedes_unrelated_stack_windows(self):
        self.capture()
        self.assertEqual(self.receipt['threads'][0]['tid'], self.fault_tid)
        files = [item['file'] for item in json.loads((self.out / 'memory-index.json').read_text())]
        other = files.index(f'thread-{self.other_tid}-stack.bin')
        for decisive in ('native-load-code.bin', 'a32-jit-state.bin', 'forward-block-map-buckets.bin',
                         'run-stack-candidate.bin', 'jit-target-0.bin', 'ww-host-cpu-known-0.bin'):
            with self.subTest(file=decisive):
                self.assertLess(files.index(decisive), other)
        self.assertFalse(self.receipt['capture_partial'])

    def expect_decisive_short_read_error(self, address, size=None):
        self.short_address, self.short_size = address, size
        with self.assertRaisesRegex(RuntimeError, 'short process memory read') as caught:
            self.capture()
        self.receipt['fixture_expected_error'] = str(caught.exception)
        self.assertFalse(self.receipt['decisive_capture_complete'])
        self.assertFalse(any(address == self.other_sp for address, _ in self.reads))
        self.assertEqual(len(self.short_reads), 1)

    def test_faulting_thread_short_stack_read_remains_an_error(self):
        self.expect_decisive_short_read_error(self.run_sp)

    def test_jit_state_short_read_remains_an_error(self):
        self.expect_decisive_short_read_error(self.state)

    def test_forward_block_map_short_read_remains_an_error(self):
        self.expect_decisive_short_read_error(self.buckets)

    def test_reverse_block_map_short_read_remains_an_error(self):
        self.expect_decisive_short_read_error(self.node)

    def test_rsb_short_read_remains_an_error(self):
        self.expect_decisive_short_read_error(self.run_sp, 0x580)

    def test_unrelated_short_stack_read_recovers_on_single_retry(self):
        self.short_address, self.short_attempts = self.other_sp, 1
        self.capture()
        window = self.receipt['threads'][1]['stack_window']
        self.assertEqual(window['status'], 'complete')
        self.assertEqual(window['attempts'], 2)
        self.assertEqual(len(window['errors']), 1)
        self.assertFalse(self.receipt['capture_partial'])

    def test_unrelated_unmapped_stack_is_marked_partial(self):
        self.other_regs['sp'] = 0
        self.capture()
        partial = self.receipt['partial_windows'][0]
        self.assertEqual(partial['attempts'], 0)
        self.assertIsNone(partial['file'])
        self.assertTrue(self.receipt['decisive_capture_complete'])

    def test_unrelated_frame_pointer_short_read_is_marked_partial(self):
        self.short_address = self.other_sp + 0x100
        self.other_regs['x29'] = self.short_address
        self.capture()
        self.assertTrue(self.receipt['capture_partial'])
        self.assertTrue(self.receipt['decisive_capture_complete'])
        self.assertEqual(self.receipt['partial_windows'], [])
        self.assertEqual(self.receipt['partial_backtraces'][0]['address'], self.short_address)

    def test_capture_budget_failure_is_not_treated_as_optional_short_read(self):
        self.short_address = self.other_sp
        self.read_error = RuntimeError('memory capture deadline/byte limit exceeded')
        with self.assertRaisesRegex(RuntimeError, 'deadline/byte limit exceeded'):
            self.capture()
        self.assertTrue(self.receipt['decisive_capture_complete'])
        self.assertFalse(self.receipt['capture_partial'])

    def test_dump_write_error_is_not_treated_as_optional_read_error(self):
        original_dump = type(self.memory).dump

        def dump(memory, name, data, address):
            if name == f'thread-{self.other_tid}-stack':
                raise OSError(errno.ENOSPC, 'synthetic dump write failure')
            return original_dump(memory, name, data, address)

        with mock.patch('stopper_state.Memory.dump', dump), \
                self.assertRaisesRegex(OSError, 'synthetic dump write failure'):
            self.capture()
        self.assertTrue(self.receipt['decisive_capture_complete'])
        self.assertFalse(self.receipt['capture_partial'])
