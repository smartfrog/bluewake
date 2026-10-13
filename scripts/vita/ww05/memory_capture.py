"""Bounded private MEM1 snapshots tied to the logged draw interval."""

import hashlib
import json
import os
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gx.capture import journal_commands
from report import MAX_JOURNAL_COMMANDS
from publication import read_artifact, completed_names

MAX_CAPTURES = 16
MEM1_BYTES = 24 * 1024 * 1024
DRAW_FAMILIES = (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8)


def validate_memory_captures(count, continue_gx, command_window=None):
    if type(count) is not int or not 0 <= count <= MAX_CAPTURES:
        raise ValueError(f'MEM1 capture count must be 0..{MAX_CAPTURES}')
    if count and not continue_gx:
        raise ValueError('MEM1 capture requires an opt-in command journal')
    if command_window is not None:
        if (not isinstance(command_window, (list, tuple)) or len(command_window) != 2 or
                any(type(value) is not int for value in command_window) or not count or
                not 1 <= command_window[0] <= command_window[1] <= continue_gx):
            raise ValueError('MEM1 command window requires snapshots and a complete logged command prefix')


def snapshot_outputs(directory):
    return tuple(directory / f'ww05.mem1-{index}.{kind}{partial}'
                 for index in range(1, MAX_CAPTURES + 1)
                 for kind in ('bin', 'json') for partial in ('', '.partial'))


def verify_snapshots(directory, *, token, source, package, count, journal, publication, audio='strict', command_window=None):
    commands = journal_commands(journal) if count else []
    validate_memory_captures(count, MAX_JOURNAL_COMMANDS if commands else 0, command_window)
    if audio not in ('strict', 'deferred'):
        raise ValueError('invalid MEM1 audio policy')
    draws = [(index, data[0]) for index, data in enumerate(commands, 1)
             if (data[0] & 0xF8) in DRAW_FAMILIES and
             (command_window is None or command_window[0] <= index <= command_window[1])]
    if command_window is not None and len(draws) > count:
        raise ValueError('scene MEM1 draw window exhausted its snapshot budget')
    draws = draws[:count]
    if command_window is not None and len(commands) >= command_window[1] and not draws:
        raise ValueError('scene MEM1 command window contains no recorded draws')
    selected_commands = {index for index, _ in draws}
    prefix_hashes = {}
    fifo_digest = hashlib.sha256()
    for index, data in enumerate(commands, 1):
        fifo_digest.update(data)
        if index in selected_commands:
            prefix_hashes[index] = fifo_digest.hexdigest()
    fifo_hash = fifo_digest.hexdigest()
    provenance = {}
    window_witnesses = []
    for line in journal.splitlines() if count else ():
        if line.startswith('[gx-command]'):
            fields = dict(item.split('=', 1) for item in line.split()[1:])
            if int(fields['index']) in selected_commands:
                provenance[int(fields['index'])] = fields
        elif command_window is not None and line.startswith('[mem1-window]'):
            window_witnesses.append(line)
            if len(window_witnesses) > MAX_CAPTURES:
                raise ValueError('MEM1 command window witness count exceeds its bound')
    logicals = [directory / f'ww05.mem1-{index}.{kind}' for index in range(1, len(draws) + 1) for kind in ('bin', 'json')]
    expected = completed_names(logicals, token=token, publication=publication, limit=MEM1_BYTES)
    observed = {path.name for path in directory.glob('ww05.mem1-*')}
    if command_window is not None:
        observed -= {'ww05.mem1-start', 'ww05.mem1-end'}
    if observed != expected:
        raise ValueError('MEM1 snapshot outputs are missing, partial, unexpected or not requested')
    if command_window is not None and len(window_witnesses) != len(draws):
        raise ValueError('MEM1 command window witness count mismatch')
    snapshots = []
    previous_us = -1
    for index, (command, opcode) in enumerate(draws, 1):
        metadata_path = directory / f'ww05.mem1-{index}.json'
        content = read_artifact(metadata_path, 4096, token=token, publication=publication)
        record = json.loads(content)
        expected_fields = {'kind', 'token', 'source', 'vpk_sha256', 'snapshot', 'limit', 'file',
                           'physical_base', 'guest_base', 'bytes', 'command', 'opcode', 'pc',
                           'completion_pc', 'cycles', 'us', 'presented', 'phase', 'scope'}
        if not isinstance(record, dict) or set(record) not in (expected_fields, expected_fields | {'audio'}):
            raise ValueError('MEM1 snapshot metadata schema mismatch')
        if record.get('audio', 'strict') != audio:
            raise ValueError('MEM1 snapshot audio policy differs from the declared profile')
        suffix = '.partial' if publication == 2 else ''
        expected_values = {'kind': f'ww05-mem1-snapshot-v{publication}', 'token': token, 'source': source,
                           'vpk_sha256': package, 'snapshot': index, 'limit': count,
                            'file': f'ww05.mem1-{index}.bin{suffix}', 'physical_base': '00000000',
                           'guest_base': '80000000', 'bytes': MEM1_BYTES, 'command': command,
                           'opcode': f'{opcode:02X}', 'pc': provenance[command]['pc'],
                           'completion_pc': provenance[command]['completion_pc'], 'phase': 'before-renderer-write',
                           'scope': 'raw physical MEM1 only; external aliases and MEM2 are not captured'}
        if any(record[key] != value for key, value in expected_values.items()):
            raise ValueError('MEM1 snapshot identity, draw, address or extent mismatch')
        for key in ('snapshot', 'limit', 'bytes', 'command', 'cycles', 'us', 'presented'):
            if type(record[key]) is not int or not 0 <= record[key] <= (1 << 64) - 1:
                raise ValueError('MEM1 snapshot numeric field mismatch')
        if record['us'] <= previous_us or any(not isinstance(record[key], str) or
                re.fullmatch(r'[0-9A-F]{8}', record[key]) is None for key in ('pc', 'completion_pc')):
            raise ValueError('MEM1 snapshot time or writer provenance mismatch')
        previous_us = record['us']
        binary_path = directory / record['file']
        data = read_artifact(directory / f'ww05.mem1-{index}.bin', MEM1_BYTES, token=token, publication=publication)
        if len(data) != MEM1_BYTES:
            raise ValueError('MEM1 snapshot is not the complete actual 24 MiB extent')
        record.update(sha256=hashlib.sha256(data).hexdigest(), metadata_sha256=hashlib.sha256(content).hexdigest(),
                      path=str(binary_path), metadata_path=str(metadata_path) + suffix)
        if command_window is not None:
            window_marker = (f'[mem1-window] snapshot={index} start_command={command_window[0]} '
                             f'end_command={command_window[1]} command={command}')
            if window_witnesses.count(window_marker) != 1:
                raise ValueError('MEM1 snapshot is missing its bound command window witness')
            record['command_window'] = list(command_window)
            record['replay_scope'] = 'selected draw-time MEM1; earlier resource versions and external aliases are not certified'
        record['replay_manifest'] = dict(format='gx-mem1-snapshot-v1', binary=binary_path.name,
                                        binary_sha256=record['sha256'],
                                        fifo_sha256=prefix_hashes[command] if command_window is not None else fifo_hash,
                                        regions=[dict(address=0, offset=0, size=MEM1_BYTES)],
                                        producer=dict(record))
        snapshots.append(record)
    return {'kind': f'ww05-mem1-capture-manifest-v{publication}', 'publication_version': publication,
            'audio': audio, 'requested': count, 'captured': len(snapshots),
            'command_window': command_window,
            'window_complete': command_window is None or len(commands) >= command_window[1],
            'snapshots': snapshots, 'policy': 'time-matched raw MEM1 only; no invented bytes, zero-filled replay, alias reconstruction or whole-run memory-version claim'}


def publish_replay_manifests(directory, manifest):
    from fp_libraries import directory_fd

    for snapshot in manifest['snapshots']:
        name = f"ww05.mem1-{snapshot['snapshot']}.snapshot.json"
        with directory_fd(directory) as parent:
            descriptor = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                                 0o600, dir_fd=parent)
        with os.fdopen(descriptor, 'w') as stream:
            json.dump(snapshot['replay_manifest'], stream, indent=2)
            stream.write('\n')
        snapshot['replay_manifest_path'] = str(directory / name)
