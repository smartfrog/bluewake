"""Strict startup-frontier evidence. A command observation never means a rendered scene."""

import re

MAX_JOURNAL_COMMANDS = 196608
MAX_JOURNAL_BYTES = 8 * 1024 * 1024


def matched(pattern, line):
    result = re.fullmatch(pattern, line)
    if result is None:
        raise ValueError('invalid WW05 report record: ' + line)
    return result.groups()


def command_bytes(opcode, size, raw):
    data = bytes.fromhex(raw)
    if len(data) != int(size) or len(data) > 1025 or (data and data[0] != int(opcode, 16)):
        raise ValueError('WW05 FIFO byte evidence mismatch')
    return data


def validate_frame(data, layout=None):
    opcode = data[0] if data else -1
    if (opcode & 0xF8) in (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8):
        if layout is None or len(data) < 3:
            raise ValueError('WW05 draw requires a witnessed pinned CP layout')
        format_index, vertices, stride, size = layout
        if (format_index != (opcode & 7) or vertices != int.from_bytes(data[1:3], 'big') or
                vertices == 0 or stride == 0 or size != len(data) or size != 3 + vertices * stride):
            raise ValueError('WW05 draw bytes disagree with the pinned layout witness')
        return
    if layout is not None:
        raise ValueError('WW05 layout witness must belong to a logged primitive')
    required = {0x00: 1, 0x48: 1, 0x08: 6, 0x61: 5, 0x20: 5, 0x28: 5,
                0x30: 5, 0x38: 5, 0x40: 9}.get(opcode)
    if opcode == 0x10 and len(data) >= 5:
        count = int.from_bytes(data[1:3], 'big') + 1
        if count <= 255:
            required = 5 + count * 4
    if required is None or len(data) != required:
        raise ValueError('WW05 PASS is not one fully framed supported GX command')


def stream_counts(line, limit):
    decoded, logged, requested, pending, read = (int(value) for value in matched(
        r'GX_STREAM mode=continue decoded=(\d+) logged=(\d+) limit=(\d+) pending=(\d+) read_bytes=(\d+)', line))
    if (not 1 <= limit <= MAX_JOURNAL_COMMANDS or requested != limit or logged != min(decoded, limit) or
        pending > 1025 or read % 32 or read < decoded or decoded > (1 << 64) - 1 or read > (1 << 64) - 1):
        raise ValueError('invalid WW05 continuation counts or request')
    return decoded, logged, pending, read


def validate(content, token, source, package, continue_gx=0, defer_audio=False):
    if len(content.encode()) > 65536 or not content.endswith('\n'):
        raise ValueError('WW05 report must be bounded and closed')
    lines = content.splitlines()
    header = f'WW05 {token} {source} {package} method=startup-gx-v1 phase=guest platform=VITA'
    if defer_audio:
        header += ' audio=deferred'
    if not lines or lines[0] != header:
        raise ValueError('WW05 guest identity mismatch')
    if len(lines) == 3 and lines[1:] == ['STOP reason=host allocation failed', 'END frontier=FAIL live=0 rendered=0']:
        return 'FAIL'
    if len(lines) != (10 if continue_gx else 9) or lines[1] != 'ABI pointer=4 cpu=3496 version=6':
        raise ValueError('WW05 record count or target CPU ABI mismatch')
    decoded = 0
    if continue_gx:
        decoded, logged, pending, read = stream_counts(lines.pop(6), continue_gx)
    host, mem1, vm, cache, code_snapshot, aram, aliases, count, live = (int(value or 0) for value in matched(
        r'ALLOC host=(\d+) mem1=(\d+) vm=(\d+) cache=(\d+)(?: code_snapshot=(\d+))?(?: aram=(\d+))? aliases=(\d+) alias_count=(\d+) owned_live=(\d+)', lines[2]))
    if (live != host + mem1 + vm + cache + code_snapshot + aram + aliases or host <= 0 or code_snapshot > mem1 or
        aram not in (0, 16777216) or
        mem1 not in (0, 25165824) or vm not in (0, 33554432) or cache not in (0, 16384) or
        count > 4096 or bool(aliases) != bool(count)):
        raise ValueError('WW05 allocation ledger is inconsistent')
    if lines[3] != 'UNKNOWN residency native_stack_highwater pool_peak GPU':
        raise ValueError('unmeasured quantities must remain UNKNOWN')
    entry, pc, sp, edges, exception, cycles = matched(
        r'STATE entry=([0-9A-F]{8}) pc=([0-9A-F]{8}) sp=([0-9A-F]{8}) edges=(\d+) exception=([0-9A-F]{8}) cycles=(\d+)', lines[4])
    complete, opcode, size, start, finish, fifo_sp, trailing, edge_stop, raw = matched(
        r'GX_FRONTIER complete=([01]) opcode=([0-9A-F]{2}) bytes=(\d+) pc=([0-9A-F]{8}) completion_pc=([0-9A-F]{8}) sp=([0-9A-F]{8}) trailing=(\d+) edge_stop=([01]) hex=([0-9A-F]*)', lines[5])
    data = command_bytes(opcode, size, raw)
    reason, address, error_pc, width, value = matched(
        r'STOP reason=(.{1,256}) address=([0-9A-F]{8}) pc=([0-9A-F]{8})(?: width=([1248]) value=([0-9A-F]{16}))?', lines[6])
    if width is not None and int(value, 16) >= 1 << (8 * int(width)):
        raise ValueError('WW05 service write value exceeds its width')
    renderer = re.fullmatch(
        r'RENDER fifo_bytes=(\d+) events=(\d+) plans=(\d+) plans_skipped=(\d+) '
        r'draws=(\d+) programs=(\d+) programs_distinct=(\d+) programs_refused=(\d+) '
        r'copies=(\d+) copies_refused=(\d+) display_copies=(\d+) presented=(\d+) gaps=(\d+)', lines[7])
    if renderer is None:
        raise ValueError('WW05 renderer summary missing or malformed')
    counts = tuple(int(value) for value in renderer.groups())
    verdict, rendered = matched(r'END frontier=(PASS|FAIL) live=0 rendered=(\d+)', lines[8])
    if (any(value > (1 << 64) - 1 for value in counts) or counts[-1] > 16 or
        counts[11] > counts[10] or int(rendered) != counts[11]):
        raise ValueError('WW05 renderer presentation counts disagree')
    if continue_gx and bool(decoded) != (complete == '1'):
        raise ValueError('WW05 continuation lost its first frontier')
    if verdict == 'PASS':
        if (complete != '1' or int(edges) < 1 or entry != '80003140' or
            (not continue_gx and (edge_stop != '1' or exception != '00000000' or
             reason != 'first complete GX command; no renderer' or address != '00000000' or
             error_pc != '00000000' or width is not None)) or
            (mem1, vm, cache) != (25165824, 33554432, 16384)):
            raise ValueError('WW05 PASS without genuine stopped frontier')
        validate_frame(data)
    return verdict


def validate_commands(content, report, limit):
    """Certify journal bytes/counts and producer layout witnesses, not renderer replay."""
    lines = report.splitlines()
    if len(lines) == 3 and lines[1:] == ['STOP reason=host allocation failed', 'END frontier=FAIL live=0 rendered=0']:
        if '[gx-command' in content:
            raise ValueError('WW05 unallocated host cannot have a command journal')
        return 0
    decoded, logged, pending, read = stream_counts(lines[6], limit)
    commands = [line for line in content.splitlines() if line.startswith('[gx-command]')]
    layouts = {}
    for line in content.splitlines():
        if not line.startswith('[gx-command-layout]'):
            continue
        number, format_index, vertices, stride, size = (int(value) for value in matched(
            r'\[gx-command-layout\] index=(\d+) format=(\d+) vertices=(\d+) stride=(\d+) bytes=(\d+)', line))
        if not 1 <= number <= logged or number in layouts:
            raise ValueError('out-of-range or duplicate WW05 command layout witness')
        layouts[number] = (format_index, vertices, stride, size)
    if len(commands) != logged:
        raise ValueError('WW05 command journal count mismatch')
    captured_bytes = 0
    for index, line in enumerate(commands, 1):
        number, opcode, size, pc, finish, sp, raw = matched(
            r'\[gx-command\] index=(\d+) opcode=([0-9A-F]{2}) bytes=(\d+) pc=([0-9A-F]{8}) '
            r'completion_pc=([0-9A-F]{8}) sp=([0-9A-F]{8}) hex=([0-9A-F]+)', line)
        if int(number) != index:
            raise ValueError('WW05 command journal sequence mismatch')
        data = command_bytes(opcode, size, raw)
        validate_frame(data, layouts.pop(index, None))
        captured_bytes += len(data)
        if index == 1 and (f'opcode={opcode} bytes={size} pc={pc} completion_pc={finish} sp={sp} ' not in lines[5] or
                           not lines[5].endswith(' hex=' + raw)):
            raise ValueError('WW05 first journal command differs from its frontier')
    if layouts or captured_bytes > read or captured_bytes > MAX_JOURNAL_BYTES:
        raise ValueError('WW05 journal has orphaned layout witnesses or more bytes than the actual FIFO reads')
    return logged
