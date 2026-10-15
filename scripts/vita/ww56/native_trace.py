"""Decode fixed-offset WW117 metadata, rejecting torn records without a shutdown footer."""

import argparse
import json
from pathlib import Path
import struct

MAGIC = 0x37315757
FILE_BYTES = 593920
RECORD = struct.Struct('<32I')


def checksum(data):
    value = 2166136261
    for byte in data[:124]:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def decode(data):
    if len(data) > FILE_BYTES:
        raise ValueError('native-context trace exceeds its fixed file bound')
    records, invalid = [], []
    for offset in range(0, len(data), RECORD.size):
        raw = data[offset:offset + RECORD.size]
        if not any(raw):
            continue
        if len(raw) != RECORD.size:
            invalid.append(offset)
            continue
        words = RECORD.unpack(raw)
        if (words[0] != MAGIC or words[1] != 1 or words[3] != (~words[2] & 0xffffffff) or
                checksum(raw) != words[-1]):
            invalid.append(offset)
            continue
        records.append(dict(zip(('magic', 'version', 'sequence', 'inverse_sequence', 'stage', 'surface',
                                 'call_id', 'flags', 'target', 'wrapper', 'expected_ctx', 'pc',
                                 'expected_pc', 'sp', 'continuation'), words[:15]),
                            registers=list(words[15:28]), stack=list(words[28:31]), offset=offset))
    header = next((record for record in records if record['offset'] == 0), None)
    failures = [record for record in records if record['stage'] == 9]
    pinned = [record for record in records if record['offset'] in (128, 256, 384, 512)]
    rolling = sorted((record for record in records if record['offset'] >= 4096),
                     key=lambda record: record['sequence'])
    returned = {record['call_id'] for record in rolling if record['stage'] == 2}
    unmatched = [record['call_id'] for record in rolling
                 if record['stage'] == 1 and record['call_id'] not in returned]
    armed = any(record['stage'] == 5 for record in records)
    complete = header is not None and armed and not invalid and not failures and not unmatched
    return {'file_bytes': len(data), 'valid_records': len(records), 'invalid_offsets': invalid,
            'header_valid': header is not None, 'window_armed': armed,
            'failure_flags': [record['flags'] for record in failures], 'unmatched_entries': unmatched,
            'coverage': 'available' if complete else 'inconclusive', 'pinned': pinned,
            'rolling': rolling, 'native_fault_classification': 'unresolved: trace-only, core policy deferred'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    args = parser.parse_args()
    print(json.dumps(decode(args.trace.read_bytes()), indent=2))


if __name__ == '__main__':
    main()
