"""Bounded private MEM1 snapshots tied to the logged draw interval."""

import hashlib
import json
import os
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gx.capture import journal_commands

MAX_CAPTURES = 2
MEM1_BYTES = 24 * 1024 * 1024
DRAW_FAMILIES = (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8)


def validate_memory_captures(count, continue_gx):
    if type(count) is not int or not 0 <= count <= MAX_CAPTURES:
        raise ValueError('MEM1 capture count must be 0..2')
    if count and not continue_gx:
        raise ValueError('MEM1 capture requires an opt-in command journal')


def snapshot_outputs(directory):
    return tuple(directory / f'ww05.mem1-{index}.{kind}{partial}'
                 for index in range(1, MAX_CAPTURES + 1)
                 for kind in ('bin', 'json') for partial in ('', '.partial'))


def verify_snapshots(directory, *, token, source, package, count, journal):
    from fp_profile import read_profile_file

    validate_memory_captures(count, bool(journal))
    commands = journal_commands(journal) if count else []
    fifo_hash = hashlib.sha256(b''.join(commands)).hexdigest()
    draws = [(index, data[0]) for index, data in enumerate(commands, 1)
             if (data[0] & 0xF8) in DRAW_FAMILIES][:count]
    provenance = {}
    for line in journal.splitlines() if count else ():
        if line.startswith('[gx-command]'):
            fields = dict(item.split('=', 1) for item in line.split()[1:])
            provenance[int(fields['index'])] = fields
    expected = {f'ww05.mem1-{index}.{kind}' for index in range(1, len(draws) + 1) for kind in ('bin', 'json')}
    observed = {path.name for path in directory.glob('ww05.mem1-*')}
    if observed != expected:
        raise ValueError('MEM1 snapshot outputs are missing, partial, unexpected or not requested')
    snapshots = []
    previous_us = -1
    for index, (command, opcode) in enumerate(draws, 1):
        metadata_path = directory / f'ww05.mem1-{index}.json'
        content = read_profile_file(metadata_path, 4096, owned=True)
        record = json.loads(content)
        expected_fields = {'kind', 'token', 'source', 'vpk_sha256', 'snapshot', 'limit', 'file',
                           'physical_base', 'guest_base', 'bytes', 'command', 'opcode', 'pc',
                           'completion_pc', 'cycles', 'us', 'presented', 'phase', 'scope'}
        if not isinstance(record, dict) or set(record) != expected_fields:
            raise ValueError('MEM1 snapshot metadata schema mismatch')
        expected_values = {'kind': 'ww05-mem1-snapshot-v1', 'token': token, 'source': source,
                           'vpk_sha256': package, 'snapshot': index, 'limit': count,
                           'file': f'ww05.mem1-{index}.bin', 'physical_base': '00000000',
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
        data = read_profile_file(binary_path, MEM1_BYTES, owned=True)
        if len(data) != MEM1_BYTES:
            raise ValueError('MEM1 snapshot is not the complete actual 24 MiB extent')
        record.update(sha256=hashlib.sha256(data).hexdigest(), metadata_sha256=hashlib.sha256(content).hexdigest(),
                      path=str(binary_path), metadata_path=str(metadata_path))
        record['replay_manifest'] = dict(format='gx-mem1-snapshot-v1', binary=binary_path.name,
                                        binary_sha256=record['sha256'], fifo_sha256=fifo_hash,
                                        regions=[dict(address=0, offset=0, size=MEM1_BYTES)],
                                        producer=dict(record))
        snapshots.append(record)
    return {'kind': 'ww05-mem1-capture-manifest-v1', 'requested': count, 'captured': len(snapshots),
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
