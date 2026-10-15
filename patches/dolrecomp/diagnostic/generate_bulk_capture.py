#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
from pathlib import Path


EXCLUDED = {1, 26, 46, 129, 188, 189, 214, 224, 276, 379}
INHERITED = {9, 19, 60, 84, 168, 400}
SPEC_SHA256 = 'c89b1585a9e3e6ad7b3fa90e71caf8b692ab6be03f4a730e73d81353557736f3'


def read_rows(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != SPEC_SHA256:
        raise ValueError('bulk capture requires the frozen WW147A specification SHA-256')
    table = json.loads(data)
    rows = table['modules']
    if [row['id'] for row in rows] != list(range(1, 416)):
        raise ValueError('bulk specification must contain ordered unique IDs 1..415')
    selected = []
    names = set()
    for row in rows:
        mid = row['id']
        expected = mid not in EXCLUDED | INHERITED
        if row['bulk_certificate_candidate'] != (mid not in EXCLUDED) or row['bulk_new_capture_candidate'] != expected:
            raise ValueError(f'module {mid}: frozen exclusion/inheritance mismatch')
        if not expected:
            continue
        name = row['candidate_module_spec']['name']
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name) or name in names:
            raise ValueError(f'module {mid}: invalid or duplicate module name')
        names.add(name)
        if row['capture_basename'] != f'{name}-{mid}.ship-code-input' or bytes.fromhex(row['capture_magic_hex']) != f'REL{mid:05d}'.encode():
            raise ValueError(f'module {mid}: frozen capture name/magic mismatch')
        size = row['candidate_module_spec']['exec'][3]
        if not 0 < size <= 0x100000 or size % 4:
            raise ValueError(f'module {mid}: invalid executable capture size')
        selected.append({'id': mid, 'name': name, 'capture_magic_hex': row['capture_magic_hex'],
                         'capture_basename': row['capture_basename'], 'executable_size': size})
    if len(selected) != 399:
        raise ValueError('bulk capture requires exactly 399 new modules')
    return selected


def render(rows):
    return ''.join(f'        {{.capture={{.module_id={row["id"]}u, .magic="{bytes.fromhex(row["capture_magic_hex"]).decode("ascii")}"}}, '
                   f'.basename="{row["capture_basename"]}", .executable_size={row["executable_size"]}u}},\n'
                   for row in rows)


def main():
    parser = argparse.ArgumentParser(description='Generate the frozen private bulk capture diagnostic table')
    parser.add_argument('spec', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    text = render(read_rows(args.spec))
    with args.output.open('x') as stream:
        stream.write(text)


if __name__ == '__main__':
    main()
