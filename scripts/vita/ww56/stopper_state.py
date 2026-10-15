"""Read-only, bounded capture of the pinned Vita3K/Dynarmic AArch64 layout."""

import bisect
import hashlib
import json
import os
import struct
import time


def write_json(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


class ShortReadError(RuntimeError):
    def __init__(self, address, expected, data):
        super().__init__(f'short process memory read at {address:#x}: expected {expected}, got {len(data)}')
        self.data = data


class Memory:
    def __init__(self, pid, maps, out):
        self.fd = os.open(f'/proc/{pid}/mem', os.O_RDONLY)
        self.out, self.files = out, []
        self.deadline = time.monotonic() + 15
        self.remaining = 64 * 1024 * 1024
        self.maps = []
        for line in maps.splitlines():
            fields = line.split()
            start, end = (int(value, 16) for value in fields[0].split('-'))
            self.maps.append((start, end, fields[1]))

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.record('memory-index', self.files)
        os.close(self.fd)

    def record(self, name, data):
        write_json(self.out / (name + '.json'), data)

    def readable(self, address, size):
        return size >= 0 and any(start <= address and address + size <= end and 'r' in perms
                                 for start, end, perms in self.maps)

    def read(self, address, size):
        if time.monotonic() >= self.deadline or size > self.remaining:
            raise RuntimeError('memory capture deadline/byte limit exceeded')
        if not self.readable(address, size):
            raise ValueError(f'unreadable capture range {address:#x}+{size:#x}')
        result = os.pread(self.fd, size, address)
        self.remaining -= len(result)
        if len(result) != size:
            raise ShortReadError(address, size, result)
        return result

    def u64(self, address):
        return struct.unpack('<Q', self.read(address, 8))[0]

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]

    def dump(self, name, data, address):
        filename = name + '.bin'
        (self.out / filename).write_bytes(data)
        self.files.append(dict(file=filename, address=address, size=len(data),
                               sha256=hashlib.sha256(data).hexdigest()))
        return self.files[-1]

    def window(self, name, address, size, *, allow_partial=False):
        end = next((end for start, end, perms in self.maps if start <= address < end and 'r' in perms), None)
        result = dict(file=None, address=address, requested_size=size, expected_size=0,
                      size=0, status='partial', attempts=0, errors=[])
        if end is None:
            if allow_partial:
                result['errors'].append(f'unreadable capture start {address:#x}')
                return result
            raise ValueError(f'unreadable capture start {address:#x}')
        expected = min(size, end - address)
        result['expected_size'] = expected
        data = b''
        for attempt in range(2 if allow_partial else 1):
            result['attempts'] = attempt + 1
            try:
                data = self.read(address, expected)
                break
            except ShortReadError as error:
                if error.data:
                    result['cause'] = 'boundary-bounded short read'
                    if len(error.data) > len(data):
                        data = error.data
                elif not allow_partial:
                    raise
                result['errors'].append(str(error))
                if not allow_partial:
                    break
            except OSError as error:
                if not allow_partial:
                    raise
                result['errors'].append(str(error))
        result.update(size=len(data), status='complete' if len(data) == expected else 'partial')
        if data or result['status'] == 'complete':
            result['file'] = name + '.bin'
            self.dump(name, data, address).update(result)
        return result


def reverse_entries(memory, space):
    root = memory.u64(space + 0x98)
    count = memory.u64(space + 0xb0)
    if count > 500000:
        raise ValueError(f'reverse map exceeds 500000-node bound: {count}')
    pending, seen, entries = [root] if root else [], set(), []
    while pending:
        address = pending.pop()
        if address in seen or len(seen) >= count:
            raise ValueError('reverse map cycle/count mismatch')
        seen.add(address)
        color, parent, left, right, host, descriptor = struct.unpack('<6Q', memory.read(address, 48))
        entries.append(dict(node=address, color=color, parent=parent, left=left, right=right,
                            entry=host, descriptor=descriptor))
        pending.extend(child for child in (left, right) if child)
    entries.sort(key=lambda entry: entry['entry'])
    if len(entries) != count or len({entry['entry'] for entry in entries}) != count:
        raise ValueError('reverse map size/unique-key mismatch')
    return entries


def capture_vita(memory, regs, return_pc, native_fault=None, *, at_native_fault=False):
    callback = memory.u64(regs['x28'] - 0x180 + 8) if at_native_fault else regs['x24']
    parent, cpu = memory.u64(callback + 8), memory.u64(callback + 16)
    jit = memory.u64(cpu + 16)
    impl = memory.u64(jit + 8)
    state, space = impl + 0x180, impl + 0x2f0
    if regs['x28'] != state:
        raise ValueError(f'x28 does not match callback JIT state: {regs["x28"]:#x} != {state:#x}')
    memory.window('callback', callback, 24)
    memory.window('vita-cpu-state', parent, 256)
    memory.window('dynarmic-cpu', cpu, 128)
    memory.window('jit-impl', impl, 0x800)
    raw = memory.read(state, 0x170)
    memory.dump('a32-jit-state', raw, state)
    guest = list(struct.unpack_from('<16I', raw, 24))
    result = dict(callback=callback, cpu_state=parent, guest_thread_id=memory.u32(parent),
                  dynarmic_cpu=cpu, jit=jit, impl=impl, a32_state=state,
                  address_space=space, guest_regs=guest,
                  stored_descriptor=guest[15] | (memory.u32(state + 88) << 32))
    memory.record('vita-state', result)
    begin, end = memory.u64(space + 0x40), memory.u64(space + 0x48)
    span = end - begin
    if end < begin or span % 24 or span > memory.remaining:
        raise ValueError('invalid/oversize forward block-map bucket range')
    buckets = memory.read(begin, span) if span else b''
    memory.dump('forward-block-map-buckets', buckets, begin)
    current_entries = [dict(descriptor=descriptor, entry=host)
                       for _, distance, _, descriptor, host in struct.iter_unpack('<IhHQQ', buckets)
                       if distance != -1]
    if len(current_entries) != memory.u64(space + 0x68):
        raise ValueError('forward map occupied-count mismatch')
    memory.record('current-block-map', current_entries)
    result['current_block_map'] = dict(file='current-block-map.json', count=len(current_entries))
    entries = reverse_entries(memory, space)
    memory.record('reverse-block-map', entries)
    result['reverse_block_map'] = dict(file='reverse-block-map.json', count=len(entries))
    keys = [entry['entry'] for entry in entries]
    targets = [return_pc - 4] if return_pc is not None else []
    if native_fault is not None:
        targets.append(native_fault['registers']['pc'])
    run_sp = regs['sp'] if at_native_fault else regs['sp'] + 0x440
    wrapped, wrapped_end = memory.u64(space + 0x218), memory.u64(space + 0x220)
    result['callback_route'] = 'native-fault-before-callback' if at_native_fault else 'direct-or-unrecognized'
    if not at_native_fault and wrapped <= return_pc < wrapped_end:
        return_pc = memory.u64(run_sp + 0x80)
        run_sp += 0x290
        result['callback_route'] = 'wrapped-read32'
        targets.insert(0, return_pc - 4)
    result['native_load_return_pc'] = return_pc
    layout = memory.read(run_sp, 0x580)
    memory.dump('run-stack-candidate', layout, run_sp)
    rsb = [dict(target=target, code_ptr=code) for target, code in struct.iter_unpack('<QQ', layout[:128])]
    result['rsb_candidate'] = dict(address=run_sp, entries=rsb,
                                   pointer=struct.unpack_from('<I', layout, 0x480)[0],
                                   qualification='native-fault/direct/wrapped-read32 layout; verify captured code, not an incoming-edge trace')
    result['host_run_return_pc'] = struct.unpack_from('<Q', layout, 0x4f8)[0]
    result['host_run_frame_pointer'] = struct.unpack_from('<Q', layout, 0x4f0)[0]
    targets.extend(entry['code_ptr'] for entry in rsb)
    result['native_ownership'] = []
    for index, target in enumerate(targets):
        position = bisect.bisect_right(keys, target) - 1
        owner = entries[position] if position >= 0 else None
        result['native_ownership'].append(dict(pc=target, predecessor_map_entry=owner,
                                              qualification='nearest lower entry, not an extent or epoch proof'))
        if memory.readable(target, 4):
            memory.window(f'jit-target-{index}', target, 1024)
        if owner and memory.readable(owner['entry'], 4):
            memory.window(f'jit-owner-{index}', owner['entry'], 4096)
    mem = memory.u64(parent + 8)
    guest_base = memory.u64(mem + 0x88)
    result['guest_memory_base'] = guest_base
    if at_native_fault and native_fault['address'] != guest_base + 0xd71:
        raise ValueError('native fault does not equal captured guest memory base+0xd71')
    use_page_table = memory.read(mem + 0x118, 1)[0] & 1
    table = memory.u64(mem + 0x120) if use_page_table else 0
    if use_page_table and not table:
        raise ValueError('MemState enables a null page table')
    result['guest_page_table'] = table
    if native_fault is not None:
        result['preceding_native_fault_address_matches_d71'] = native_fault['address'] == guest_base + 0xd71
    memory.record('vita-state', result)

    def guest_address(address):
        return address + (memory.u64(table + (address >> 12) * 8) if table else guest_base)

    for name, address, size in [('guest-arm-stack', guest[13], 4096),
                                ('ww-host-cpu-known', 0x98005090, 0xe00)]:
        offset = 0
        while offset < size:
            at = address + offset
            length = min(size - offset, 4096 - (at & 4095))
            native = guest_address(at)
            memory.window(f'{name}-{offset:x}', native, length)
            offset += length
    result['ww_host_cpu_pc_known'] = memory.u32(guest_address(0x98005090 + 0x280))
    memory.record('vita-state', result)
    return result
