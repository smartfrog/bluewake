import ctypes
import json
import os
from pathlib import Path
import platform
import struct
import tempfile
import unittest

from stopper_state import Memory, capture_vita, reverse_entries


@unittest.skipUnless(platform.system() == 'Linux', 'Linux process memory capture')
class VitaStateTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(
            prefix='ww123-state-', dir='/tmp/opencode' if Path('/tmp/opencode').is_dir() else None)
        self.out = Path(self.directory.name)
        self.arena = ctypes.create_string_buffer(65536)
        self.base = ctypes.addressof(self.arena)
        self.table = ctypes.create_string_buffer(8 * 1024 * 1024)
        self.table_address = ctypes.addressof(self.table)
        self.callback, self.parent, self.mem = self.base, self.base + 0x40, self.base + 0x200
        self.cpu, self.jit, self.impl = self.base + 0x400, self.base + 0x500, self.base + 0x800
        self.state, self.space = self.impl + 0x180, self.impl + 0x2f0
        self.node, self.buckets = self.base + 0x2000, self.base + 0x2100
        self.code, self.run_sp = self.base + 0x3000, self.base + 0x4000
        self.descriptor = 0x193f0d238
        for at, value in [(self.callback + 8, self.parent), (self.callback + 16, self.cpu),
                          (self.parent + 8, self.mem), (self.cpu + 16, self.jit),
                          (self.jit + 8, self.impl), (self.mem + 0x120, self.table_address),
                          (self.space + 0x98, self.node), (self.space + 0xb0, 1),
                          (self.space + 0x40, self.buckets), (self.space + 0x48, self.buckets + 24),
                          (self.space + 0x68, 1)]:
            self.pack(at, '<Q', value)
        self.pack(self.parent, '<I', 42)
        self.pack(self.mem + 0x118, '<B', 1)
        self.pack(self.state + 24 + 13 * 4, '<I', 0x80170010)
        self.pack(self.state + 24 + 15 * 4, '<I', 0x93f0d238)
        self.pack(self.state + 88, '<I', 1)
        self.pack(self.node, '<6Q', 1, self.space + 0x90, 0, 0, self.code, self.descriptor)
        self.pack(self.buckets, '<IhHQQ', 0, 0, 0, self.descriptor, self.code)
        self.pack(self.run_sp, '<QQ', self.descriptor, self.code)
        for page, native in [(0x80170, self.base + 0x6000), (0x80171, self.base + 0x7000),
                             (0x98005, self.base + 0x8000)]:
            self.pack(self.table_address + page * 8, '<Q', (native - (page << 12)) % (1 << 64))
        self.pack(self.base + 0x8090 + 0x280, '<I', 0x802b0508)
        self.regs = dict(x24=self.callback, x28=self.state, sp=self.run_sp - 0x440)
        self.memory = Memory(os.getpid(), Path('/proc/self/maps').read_text(), self.out)

    def tearDown(self):
        self.memory.__exit__()
        self.directory.cleanup()

    def pack(self, address, fmt, *values):
        data = struct.pack(fmt, *values)
        ctypes.memmove(address, data, len(data))

    def test_captures_guest_state_forward_reverse_maps_and_direct_rsb(self):
        result = capture_vita(self.memory, self.regs, self.code + 4)
        self.assertEqual(result['guest_regs'][15], 0x93f0d238)
        self.assertEqual(result['guest_thread_id'], 42)
        self.assertEqual(result['ww_host_cpu_pc_known'], 0x802b0508)
        reverse = json.loads((self.out / result['reverse_block_map']['file']).read_text())
        current = json.loads((self.out / result['current_block_map']['file']).read_text())
        self.assertEqual(reverse[0]['descriptor'], self.descriptor)
        self.assertEqual(current[0]['entry'], self.code)
        self.assertEqual(result['rsb_candidate']['entries'][0]['target'], self.descriptor)

    def test_recovers_underlying_load_return_and_rsb_through_wrapped_read(self):
        wrapped = self.code + 0x400
        self.pack(self.space + 0x218, '<QQ', wrapped, wrapped + 0x200)
        self.regs['sp'] -= 0x290
        self.pack(self.regs['sp'] + 0x440 + 0x80, '<Q', self.code + 4)
        result = capture_vita(self.memory, self.regs, wrapped + 0x7c)
        self.assertEqual(result['callback_route'], 'wrapped-read32')
        self.assertEqual(result['native_load_return_pc'], self.code + 4)
        self.assertEqual(result['rsb_candidate']['address'], self.run_sp)

    def test_native_fault_uses_live_state_and_run_sp_without_callback_frame(self):
        self.pack(self.impl + 8, '<Q', self.callback)
        self.regs['sp'], self.regs['x24'] = self.run_sp, 0
        native_fault = dict(address=0xd71, registers=dict(pc=self.code))
        result = capture_vita(self.memory, self.regs, None, native_fault, at_native_fault=True)
        self.assertEqual(result['callback'], self.callback)
        self.assertEqual(result['rsb_candidate']['address'], self.run_sp)
        self.assertEqual(result['callback_route'], 'native-fault-before-callback')

    def test_rejects_reverse_map_with_wrong_node_count(self):
        self.pack(self.space + 0xb0, '<Q', 2)
        with self.assertRaisesRegex(ValueError, 'size/unique-key mismatch'):
            reverse_entries(self.memory, self.space)

    def test_rejects_mismatched_callback_cpu_instance(self):
        self.regs['x28'] = 0
        with self.assertRaisesRegex(ValueError, 'x28 does not match'):
            capture_vita(self.memory, self.regs, self.code + 4)


if __name__ == '__main__':
    unittest.main()
