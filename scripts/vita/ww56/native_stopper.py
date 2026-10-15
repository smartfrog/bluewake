#!/usr/bin/env python3
"""One-shot Linux/AArch64 native stopper. Never launches or kills the target."""

import argparse
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import struct
import subprocess
import sys
import time

from stopper_state import Memory, ShortReadError, capture_vita, write_json


VITA_SHA = 'cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30'
READ_SYMBOL = '_ZN19ArmDynarmicCallback10MemoryReadIjEET_j'
GETREGSET, SETREGSET = 0x4204, 0x4205
SEIZE, INTERRUPT, CONT, DETACH = 0x4206, 0x4207, 7, 17
GETEVENTMSG, GETSIGINFO = 0x4201, 0x4202
HW_BREAK = 0x402
WALL = 0x40000000
libc = ctypes.CDLL(None, use_errno=True)
libc.ptrace.restype = ctypes.c_long
libc.ptrace.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p]


class Iovec(ctypes.Structure):
    _fields_ = [('base', ctypes.c_void_p), ('length', ctypes.c_size_t)]


def ptrace(request, tid, address=0, data=0):
    result = libc.ptrace(request, tid, address, data)
    if result == -1:
        code = ctypes.get_errno()
        raise OSError(code, f'ptrace {request:#x} tid={tid}: {os.strerror(code)}')
    return result


def regset(tid, kind, value=None):
    buffer = ctypes.create_string_buffer(value, len(value)) if value is not None else ctypes.create_string_buffer(1024)
    iov = Iovec(ctypes.addressof(buffer), len(buffer))
    ptrace(SETREGSET if value is not None else GETREGSET, tid, kind, ctypes.addressof(iov))
    return buffer.raw[:iov.length]


def registers(tid):
    values = struct.unpack('<34Q', regset(tid, 1))
    return dict(zip([f'x{i}' for i in range(31)] + ['sp', 'pc', 'pstate'], values))


def task_ids(pid):
    return {int(path.name) for path in Path(f'/proc/{pid}/task').iterdir()}


def group_stopped(pid):
    return all(next(line for line in Path(f'/proc/{pid}/task/{tid}/status').read_text().splitlines()
                    if line.startswith('State:')).split()[1] in ('T', 't') for tid in task_ids(pid))


def until(predicate, seconds):
    deadline = time.monotonic() + seconds
    while not predicate():
        if time.monotonic() >= deadline:
            raise TimeoutError('stop/cleanup deadline expired')
        time.sleep(0.005)


class Stopper:
    def __init__(self, pid, breakpoint, out, trigger, fault_address, pidfd):
        self.pid, self.breakpoint, self.out = pid, breakpoint, out
        self.trigger, self.fault_address = trigger, fault_address
        self.pidfd = pidfd
        self.tasks = {}
        self.events = []
        self.native_faults = {}
        self.owns_stop = False

    def send(self, sig):
        signal.pidfd_send_signal(self.pidfd, sig)

    def event(self, name, **fields):
        entry = {'event': name, 'monotonic': time.monotonic(), **fields}
        with (self.out / 'events.jsonl').open('a') as stream:
            stream.write(json.dumps(entry) + '\n')
        if name not in ('stop', 'native-fault-forwarded'):
            self.events.append(entry)
            write_json(self.out / 'events.json', self.events)

    def seize(self):
        if group_stopped(self.pid):
            raise ValueError('target is already stopped; refusing to take ownership of its stop')
        self.send(signal.SIGSTOP)
        self.owns_stop = True
        until(lambda: group_stopped(self.pid), 5)
        for tid in task_ids(self.pid):
            ptrace(SEIZE, tid, 0, 8 | 16)
            self.tasks[tid] = dict(stopped=False, hardware=None, pending=0)
            ptrace(INTERRUPT, tid)
        self.freeze()
        if len(self.tasks) > 128:
            raise RuntimeError('target exceeds 128-thread capture bound')

    def poll(self):
        tid, status = os.waitpid(-1, os.WNOHANG | WALL)
        if not tid:
            return None
        if os.WIFEXITED(status) or os.WIFSIGNALED(status):
            self.tasks.pop(tid, None)
            self.event('thread-exit', tid=tid, status=status)
            return None
        if not os.WIFSTOPPED(status):
            raise RuntimeError(f'unexpected wait status {status:#x}')
        task = self.tasks.setdefault(tid, dict(stopped=False, hardware=None, pending=0))
        if len(self.tasks) > 128:
            raise RuntimeError('target exceeds 128-thread capture bound')
        task['stopped'] = True
        sig, event = os.WSTOPSIG(status), status >> 16
        self.event('stop', tid=tid, signal=sig, ptrace_event=event)
        if event == 3:
            child = ctypes.c_ulong()
            ptrace(GETEVENTMSG, tid, 0, ctypes.addressof(child))
            self.tasks.setdefault(child.value, dict(stopped=False, hardware=None, pending=0))
        elif event == 4:
            raise RuntimeError('target exec changed; observation invalid')
        elif event == 0 and sig not in (signal.SIGSTOP, signal.SIGCONT):
            own_breakpoint = False
            if sig == signal.SIGTRAP and self.trigger == 'callback-breakpoint':
                info = ctypes.create_string_buffer(128)
                ptrace(GETSIGINFO, tid, 0, ctypes.addressof(info))
                code = struct.unpack_from('<i', info.raw, 8)[0]
                own_breakpoint = code == 4 and registers(tid)['pc'] == self.breakpoint
            if not own_breakpoint:
                task['pending'] = sig
        return tid, sig, event

    def freeze(self):
        for tid, task in list(self.tasks.items()):
            if not task['stopped']:
                try:
                    ptrace(INTERRUPT, tid)
                except OSError as error:
                    if error.errno != errno.ESRCH:
                        raise
        until(self.drain_stops, 5)

    def drain_stops(self):
        if all(task['stopped'] for task in self.tasks.values()) and task_ids(self.pid) == self.tasks.keys():
            return True
        self.poll()
        return False

    def arm(self, tid):
        task = self.tasks[tid]
        old = regset(tid, HW_BREAK)
        count = old[0]
        if not 1 <= count <= 16 or len(old) < 8 + 16 * count:
            raise RuntimeError('no usable AArch64 hardware breakpoint register')
        for index in range(count):
            if struct.unpack_from('<I', old, 16 + 16 * index)[0] & 1:
                raise RuntimeError(f'tid {tid} already has a hardware breakpoint')
        task['hardware'] = old
        new = bytearray(old[:24])
        struct.pack_into('<QI', new, 8, self.breakpoint, 0x1e5)
        regset(tid, HW_BREAK, bytes(new))
        actual = regset(tid, HW_BREAK)
        if actual[8:20] != new[8:20]:
            raise RuntimeError('hardware breakpoint readback mismatch')
        self.event('breakpoint-armed', tid=tid, address=self.breakpoint)

    def resume(self, tid, sig=0):
        ptrace(CONT, tid, 0, sig)
        self.tasks[tid]['stopped'] = False
        self.tasks[tid]['pending'] = 0

    def wait_hit(self, seconds):
        if self.trigger == 'callback-breakpoint':
            for tid in self.tasks:
                self.arm(tid)
        self.send(signal.SIGCONT)
        for tid in self.tasks:
            self.resume(tid)
        self.event('armed', tids=sorted(self.tasks), trigger=self.trigger, fault_address=self.fault_address)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            event = self.poll()
            if event is None:
                if not self.tasks:
                    raise RuntimeError('target exited before trigger')
                time.sleep(0.005)
                continue
            tid, sig, kind = event
            if self.tasks[tid]['pending']:
                if sig in (signal.SIGSEGV, signal.SIGBUS):
                    info = ctypes.create_string_buffer(128)
                    ptrace(GETSIGINFO, tid, 0, ctypes.addressof(info))
                    native_fault = dict(tid=tid, signal=sig, monotonic=time.monotonic(),
                                        registers=registers(tid), siginfo=info.raw.hex(),
                                        address=struct.unpack_from('<Q', info.raw, 16)[0])
                    self.native_faults[tid] = native_fault
                    if self.trigger == 'native-fault' and native_fault['address'] == self.fault_address:
                        self.event('native-fault-hit', **native_fault)
                        self.send(signal.SIGSTOP)
                        self.freeze()
                        return tid
                    self.event('native-fault-forwarded', tid=tid, signal=sig,
                               pc=native_fault['registers']['pc'], address=native_fault['address'])
                    self.resume(tid, sig)
                    continue
                raise RuntimeError(f'target signal {sig} on tid {tid}; preserved for detach')
            if self.trigger == 'callback-breakpoint' and sig == signal.SIGTRAP and kind == 0:
                info = ctypes.create_string_buffer(128)
                ptrace(GETSIGINFO, tid, 0, ctypes.addressof(info))
                code = struct.unpack_from('<i', info.raw, 8)[0]
                if code != 4 or registers(tid)['pc'] != self.breakpoint:
                    self.tasks[tid]['pending'] = sig
                    raise RuntimeError(f'unexpected SIGTRAP code={code}; preserved for detach')
                self.event('breakpoint-hit', tid=tid, siginfo=info.raw.hex())
                self.send(signal.SIGSTOP)
                self.freeze()
                return tid
            if self.trigger == 'callback-breakpoint' and self.tasks[tid]['hardware'] is None:
                if len(self.tasks) > 128:
                    raise RuntimeError('target exceeds 128-thread capture bound')
                self.arm(tid)
            self.resume(tid)
        raise TimeoutError('trigger deadline expired')

    def cleanup(self):
        result = dict(detached=False, debug_registers_restored=False, group_stopped=False,
                      debug_restore=[], errors=[])
        if not self.owns_stop:
            os.close(self.pidfd)
            return result
        try:
            self.send(signal.SIGSTOP)
            self.freeze()
        except (OSError, RuntimeError, TimeoutError) as error:
            result['errors'].append(str(error))
        for tid, task in list(self.tasks.items()):
            try:
                if task['hardware'] is not None:
                    regset(tid, HW_BREAK, task['hardware'][:24])
                    actual = regset(tid, HW_BREAK)
                    result['debug_restore'].append(dict(tid=tid, before=task['hardware'].hex(), after=actual.hex()))
                    if (actual[8:16] != task['hardware'][8:16] or
                            actual[24:] != task['hardware'][24:] or
                            struct.unpack_from('<I', actual, 16)[0] & 1):
                        raise RuntimeError(f'debug restore readback mismatch tid={tid}')
            except (OSError, RuntimeError) as error:
                result['errors'].append(str(error))
            try:
                ptrace(DETACH, tid, 0, task['pending'] or signal.SIGSTOP)
                del self.tasks[tid]
            except OSError as error:
                result['errors'].append(str(error))
        result['detached'] = not self.tasks
        result['debug_registers_restored'] = not result['errors']
        try:
            until(lambda: group_stopped(self.pid), 5)
            result['group_stopped'] = True
            result['tracer_pids'] = {
                tid: int(next(line for line in Path(f'/proc/{self.pid}/task/{tid}/status').read_text().splitlines()
                              if line.startswith('TracerPid:')).split()[1]) for tid in task_ids(self.pid)}
            if any(result['tracer_pids'].values()):
                raise RuntimeError('a task is still traced after detach')
        except (OSError, StopIteration, RuntimeError, TimeoutError) as error:
            result['errors'].append(str(error))
        os.close(self.pidfd)
        return result


def capture_thread(memory, proc, tid, receipt, *, decisive):
    regs = registers(tid)
    entry = dict(tid=tid, registers=regs, comm=(proc / f'task/{tid}/comm').read_text().strip())
    receipt['threads'].append(entry)
    memory.dump(f'thread-{tid}-fpregs', regset(tid, 2), 0)
    window = memory.window(f'thread-{tid}-stack', regs['sp'], 65536, allow_partial=not decisive)
    entry['stack_window'] = window
    frames = [regs['pc'], regs['x30']]
    entry['frame_pointer_trace_not_complete_unwind'] = frames
    if window['status'] == 'partial':
        receipt['partial_windows'].append(dict(tid=tid, **window))
        receipt['capture_partial'] = True
        entry['frame_pointer_trace_error'] = 'not attempted after partial stack window'
        print(f'STOPPER WARNING: thread {tid} stack window partial: {window}', file=sys.stderr)
        return regs
    frame = regs['x29']
    for _ in range(64):
        if not memory.readable(frame, 16):
            break
        try:
            previous, lr = struct.unpack('<QQ', memory.read(frame, 16))
        except (ShortReadError, OSError) as error:
            if decisive:
                raise
            entry['frame_pointer_trace_error'] = str(error)
            receipt['partial_backtraces'].append(dict(tid=tid, address=frame, error=str(error)))
            receipt['capture_partial'] = True
            print(f'STOPPER WARNING: thread {tid} frame-pointer trace partial: {error}', file=sys.stderr)
            break
        frames.append(lr)
        if previous <= frame or previous - frame > 1024 * 1024:
            break
        frame = previous
    return regs


def native_capture(stopper, tid, receipt):
    proc = Path(f'/proc/{stopper.pid}')
    maps = (proc / 'maps').read_text()
    (stopper.out / 'maps.txt').write_text(maps)
    (stopper.out / 'status.txt').write_text((proc / 'status').read_text())
    receipt['threads'] = []
    receipt['decisive_capture_complete'] = False
    receipt['capture_partial'] = False
    receipt['partial_windows'] = []
    receipt['partial_backtraces'] = []
    with Memory(stopper.pid, maps, stopper.out) as memory:
        fault = capture_thread(memory, proc, tid, receipt, decisive=True)
        native_fault_trigger = stopper.trigger == 'native-fault'
        receipt['invalid_address'] = 0xd71 if native_fault_trigger else memory.u32(fault['sp'] + 0x6c)
        receipt['native_return_pc'] = None if native_fault_trigger else memory.u64(fault['sp'] + 8)
        if native_fault_trigger:
            receipt['native_load_pc'] = fault['pc']
            memory.window('native-load-code', fault['pc'] - 256, 1024)
        receipt['preceding_native_fault_same_thread_not_necessarily_same_access'] = stopper.native_faults.get(tid)
        if not native_fault_trigger:
            memory.window('native-caller-code', receipt['native_return_pc'] - 256, 1024)
        if receipt['invalid_address'] != 0xd71:
            raise RuntimeError(f'first invalid read is {receipt["invalid_address"]:#x}, not 0xd71; stopped, not skipped')
        if receipt['profile'] == 'vita':
            receipt['vita'] = capture_vita(memory, fault, receipt['native_return_pc'],
                                           stopper.native_faults.get(tid), at_native_fault=native_fault_trigger)
        receipt['decisive_capture_complete'] = True
        for current in sorted(stopper.tasks):
            if current != tid:
                capture_thread(memory, proc, current, receipt, decisive=False)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--sha256', default=VITA_SHA)
    parser.add_argument('--profile', choices=('vita', 'native'), default='vita')
    parser.add_argument('--trigger', choices=('native-fault', 'callback-breakpoint'), default='native-fault')
    parser.add_argument('--fault-address', type=lambda value: int(value, 0), default=0x400000d71)
    parser.add_argument('--allow-hardware-breakpoint', action='store_true',
                        help='acknowledge that SIGKILL can leave an active ptrace hardware breakpoint')
    parser.add_argument('--break-symbol', default=READ_SYMBOL)
    parser.add_argument('--break-offset', type=lambda value: int(value, 0), default=0x34)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--wait-seconds', type=float, default=120)
    args = parser.parse_args()
    if args.pid <= 1 or not 0 < args.wait_seconds <= 180 or args.break_offset < 0:
        parser.error('require pid > 1, 0 < wait-seconds <= 180, nonnegative offset')
    if args.trigger == 'callback-breakpoint' and not args.allow_hardware_breakpoint:
        parser.error('callback-breakpoint requires --allow-hardware-breakpoint; SIGKILL is unsafe')
    return args


def main():
    args = parse_args()
    os.umask(0o077)
    args.out.mkdir(mode=0o700)
    receipt = dict(outcome='error', profile=args.profile, trigger=args.trigger, pid=args.pid, exe=str(args.exe),
                   started_monotonic=time.monotonic(), no_target_memory_or_general_register_writes=True,
                   debug_register_writes=args.trigger == 'callback-breakpoint')
    stopper, pidfd = None, None
    try:
        if platform.system() != 'Linux' or platform.machine() != 'aarch64':
            raise RuntimeError('only Linux/AArch64 is supported')
        pidfd = os.pidfd_open(args.pid)
        proc = Path(f'/proc/{args.pid}')
        receipt['starttime'] = (proc / 'stat').read_text().rsplit(')', 1)[1].split()[19]
        if not os.path.samefile(proc / 'exe', args.exe):
            raise ValueError('PID executable does not match --exe')
        with (proc / 'exe').open('rb') as executable:
            sha = hashlib.file_digest(executable, 'sha256').hexdigest()
            executable.seek(0)
            header = executable.read(20)
        if header[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', header, 16) != (3, 183):
            raise ValueError('require a little-endian AArch64 PIE executable')
        if sha != args.sha256:
            raise ValueError(f'executable SHA-256 mismatch: {sha}')
        receipt['sha256'] = sha
        symbols = subprocess.run(['nm', '--defined-only', str(args.exe)], check=True,
                                 capture_output=True, text=True, timeout=10).stdout.splitlines()
        found = [int(line.split()[0], 16) for line in symbols
                 if len(line.split()) == 3 and line.split()[2] == args.break_symbol]
        if len(found) != 1:
            raise ValueError(f'expected one symbol {args.break_symbol!r}; found {len(found)}')
        mappings = (proc / 'maps').read_text().splitlines()
        bases = [int(line.split()[0].split('-')[0], 16) for line in mappings
                 if int(line.split()[2], 16) == 0 and len(line.split()) >= 6
                 and os.path.realpath(line.split(maxsplit=5)[5]) == str(args.exe.resolve())]
        if len(bases) != 1:
            raise ValueError('expected one offset-zero executable mapping (PIE required)')
        address = bases[0] + found[0] + args.break_offset
        if address % 4:
            raise ValueError('breakpoint is not instruction-aligned')
        receipt['callback_breakpoint_address'] = address
        receipt['expected_native_fault_address'] = args.fault_address
        stopper = Stopper(args.pid, address, args.out, args.trigger, args.fault_address, pidfd)
        stopper.seize()
        with Memory(args.pid, (proc / 'maps').read_text(), args.out) as memory:
            if memory.read(address, 4) != bytes.fromhex('f55b02a9'):
                raise ValueError('breakpoint instruction mismatch; expected stp x21,x22,[sp,#32]')
        tid = stopper.wait_hit(args.wait_seconds)
        receipt['hit_tid'] = tid
        native_capture(stopper, tid, receipt)
        receipt['outcome'] = 'captured'
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        receipt['error'] = str(error)
        print(f'STOPPER ERROR: {error}', file=sys.stderr)
    finally:
        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            signal.signal(sig, signal.SIG_IGN)
        if stopper is not None:
            receipt['cleanup'] = stopper.cleanup()
            if receipt['cleanup']['errors']:
                receipt['outcome'] = 'error'
        elif pidfd is not None:
            os.close(pidfd)
        receipt['finished_monotonic'] = time.monotonic()
        write_json(args.out / 'receipt.json', receipt)
    if receipt['outcome'] == 'captured':
        try:
            result = subprocess.run(['eu-stack', '--pid', str(args.pid), '-n', '64'],
                                    capture_output=True, text=True, timeout=8,
                                    env={**os.environ, 'DEBUGINFOD_URLS': ''})
            (args.out / 'eu-stack.txt').write_text(result.stdout + result.stderr)
            receipt['eu_stack'] = dict(returncode=result.returncode,
                                      complete=result.returncode == 0)
            receipt['stopped_after_unwind'] = group_stopped(args.pid)
            if not receipt['stopped_after_unwind']:
                raise RuntimeError('target did not remain group-stopped after eu-stack')
            if result.returncode != 0:
                raise RuntimeError(f'eu-stack incomplete (exit {result.returncode}); see eu-stack.txt')
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            receipt['outcome'] = 'error'
            receipt['error'] = str(error)
            print(f'STOPPER ERROR: {error}', file=sys.stderr)
        write_json(args.out / 'receipt.json', receipt)
    print(json.dumps(receipt))
    return 0 if receipt['outcome'] == 'captured' else 2


def interrupted(sig, frame):
    raise RuntimeError(f'interrupted by signal {sig}; stopping and detaching target')


if __name__ == '__main__':
    for requested_signal in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(requested_signal, interrupted)
    sys.exit(main())
