#!/usr/bin/env python3
"""Extract a private, byte-exact GX journal fixture and optionally replay it."""

import argparse
import hashlib
import json
from pathlib import Path
import re

import bridge
import capture
import memory_snapshot


DRAW_FAMILIES = set(range(0x80, 0xC0, 8))


def draw_count(commands):
    return sum(data[0] & 0xF8 in DRAW_FAMILIES for data in commands)


def requires_memory(commands):
    low = high = 0
    texture_bound = False
    needed = False
    for data in commands:
        opcode = data[0]
        framed = capture.required_length(data)
        if framed == 'overflow' or (isinstance(framed, int) and len(data) != framed):
            raise ValueError('journal command frame length mismatch')
        if opcode == 0x08:
            value = int.from_bytes(data[2:6], 'big')
            if data[1] == 0x50:
                low = value
            elif data[1] == 0x60:
                high = value
        elif opcode == 0x61:
            reg = data[1]
            texture_bound |= reg in range(0x94, 0x98) or reg in range(0xB4, 0xB8)
            needed |= reg == 0x65
        elif opcode in (0x20, 0x28, 0x30, 0x38):
            needed = True
        elif opcode == 0x40 and int.from_bytes(data[5:9], 'big') != 0:
            needed = True
        elif opcode & 0xF8 in DRAW_FAMILIES:
            if len(data) < 3:
                raise ValueError('draw journal record lacks a vertex count')
            indexed = any(((low >> shift) & 3) >= 2 for shift in (9, 11, 13, 15))
            indexed |= any(((high >> shift) & 3) >= 2 for shift in range(0, 16, 2))
            needed |= texture_bound or indexed
    return needed


def report_fields(text, prefix):
    lines = [line for line in text.splitlines() if line.startswith(prefix + ' ')]
    if len(lines) > 1:
        raise ValueError(f'report has multiple {prefix} records')
    return dict(item.split('=', 1) for item in lines[0].split()[1:]) if lines else {}


def extract_fixture(journal, output, *, mem1=None, report=None, snapshot=None):
    if mem1 is not None and snapshot is not None:
        raise ValueError('provide only one memory input: raw MEM1 or snapshot manifest')
    source = journal.read_bytes()
    commands = capture.journal_commands(source.decode())
    if not commands:
        raise ValueError('capture has no command journal')
    memory_needed = requires_memory(commands)
    fifo = b''.join(commands)
    memory = mem1.read_bytes() if mem1 is not None else None
    loaded = memory_snapshot.load(snapshot, memory_snapshot.sha256(fifo)) if snapshot is not None else None
    if loaded is not None:
        memory = loaded.image
    result = dict(source=str(journal.resolve()), source_sha256=hashlib.sha256(source).hexdigest(),
                  commands=len(commands), bytes=len(fifo), captured_draws=draw_count(commands),
                  fifo_sha256=hashlib.sha256(fifo).hexdigest(), requires_mem1=memory_needed,
                  coverage='journal-only-extent-unverified', reported_draws=None,
                  mem1_source=str(mem1.resolve()) if mem1 is not None else None,
                  mem1_sha256=hashlib.sha256(memory).hexdigest() if memory is not None else None)
    if loaded is not None:
        result['snapshot'] = loaded.metadata
    if report is not None:
        report_bytes = report.read_bytes()
        stream = report_fields(report_bytes.decode(), 'GX_STREAM')
        renderer = report_fields(report_bytes.decode(), 'RENDER')
        result['report_source'] = str(report.resolve())
        result['report_sha256'] = hashlib.sha256(report_bytes).hexdigest()
        if 'draws' in renderer:
            result['reported_draws'] = int(renderer['draws'])
        if 'logged' in stream and 'decoded' in stream:
            logged, decoded = int(stream['logged']), int(stream['decoded'])
            if logged != len(commands) or decoded < logged:
                raise ValueError('report/journal command count mismatch')
            result['decoded_commands'] = decoded
            result['coverage'] = 'all-reported-commands' if decoded == logged else 'logged-command-prefix-only'
        if 'pending' in stream:
            result['pending_bytes'] = int(stream['pending'])
            if result['coverage'] == 'all-reported-commands' and result['pending_bytes']:
                result['coverage'] = 'all-reported-commands-partial-tail'
    output.mkdir(parents=True, exist_ok=False)
    (output / 'stream.fifo').write_bytes(fifo)
    if loaded is not None:
        loaded.stage(output)
    elif memory is not None:
        (output / 'mem1.bin').write_bytes(memory)
    (output / 'capture.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def latest_draw_capture(root, minimum_draws=1):
    reports = sorted(root.glob('vita3k-run-*/**/ww05.report'),
                     key=lambda path: path.stat().st_mtime, reverse=True)
    for report in reports:
        journal = report.with_suffix('.log')
        if not journal.exists():
            continue
        commands = capture.journal_commands(journal.read_text())
        if draw_count(commands) >= minimum_draws:
            return journal, report
    return None


def replay_fixture(directory, metadata):
    if metadata['requires_mem1'] and metadata['mem1_sha256'] is None:
        return 'verdict=memory-required\n', 2
    memory = directory / 'mem1.bin' if metadata['mem1_sha256'] is not None else None
    if metadata.get('snapshot'):
        if memory_snapshot.sha256((directory / 'stream.fifo').read_bytes()) != metadata['snapshot']['fifo_sha256']:
            raise ValueError('fixture snapshot FIFO SHA-256 mismatch')
        ranges = directory / 'mem1.ranges'
        if memory_snapshot.sha256(ranges.read_bytes()) != metadata['snapshot']['ranges_sha256']:
            raise ValueError('fixture snapshot coverage SHA-256 mismatch')
        if memory_snapshot.sha256(memory.read_bytes()) != metadata['mem1_sha256']:
            raise ValueError('fixture snapshot backing SHA-256 mismatch')
        output, refusal = bridge.run_stream(bridge.find_binary(), (directory / 'stream.fifo').read_bytes(),
                                            memory, ranges_path=ranges)
    else:
        output, refusal = bridge.run_stream(bridge.find_binary(), (directory / 'stream.fifo').read_bytes(), memory)
    if refusal is not None:
        return f'bridge ok=0 reason={refusal}\nverdict=rejected\n', 1
    summary = re.search(r'^bridge .*$', output, re.MULTILINE)
    if summary is None:
        raise ValueError('bridge did not report counters')
    counters = dict(item.split('=', 1) for item in summary[0].split()[1:])
    gaps = any(int(counters[name]) for name in ('plans_skipped', 'programs_refused', 'copies_refused'))
    gaps |= int(counters['programs']) < metadata['captured_draws']
    gaps |= re.search(r'^gap .*=[1-9]\d*$', output, re.MULTILINE) is not None
    return output + ('verdict=ran-with-coverage-gaps\n' if gaps else 'verdict=ran\n'), int(gaps)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal', type=Path, nargs='?')
    parser.add_argument('--latest', type=Path, help='read-only WW05 run root to search')
    parser.add_argument('--min-draws', type=int, default=1)
    parser.add_argument('--output', type=Path, required=True, help='new private fixture directory')
    parser.add_argument('--report', type=Path)
    memory_args = parser.add_mutually_exclusive_group()
    memory_args.add_argument('--mem1', type=Path, help='legacy raw MEM1 starting at physical zero')
    memory_args.add_argument('--snapshot', type=Path, help='versioned MEM1 binary/region manifest')
    parser.add_argument('--replay', action='store_true')
    args = parser.parse_args()
    if (args.journal is None) == (args.latest is None) or args.min_draws < 1:
        parser.error('provide one journal or --latest; --min-draws must be positive')
    try:
        journal, report = args.journal, args.report
        if args.latest is not None:
            selected = latest_draw_capture(args.latest, args.min_draws)
            if selected is None:
                print('verdict=no-captured-draw-sequence')
                return 0
            journal, report = selected
        elif report is None and journal.with_suffix('.report').exists():
            report = journal.with_suffix('.report')
        snapshot = args.snapshot
        adjacent = journal.with_suffix('.mem1.json')
        if args.mem1 is None and snapshot is None and adjacent.exists():
            snapshot = adjacent
        result = extract_fixture(journal, args.output, mem1=args.mem1, report=report, snapshot=snapshot)
        print(json.dumps(result, sort_keys=True))
        if not args.replay:
            return 0
        output, status = replay_fixture(args.output, result)
        (args.output / 'replay.txt').write_text(output)
        print(output, end='')
        return status
    except (OSError, ValueError) as error:
        print(f'refusal reason={error}\nverdict=rejected')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
