#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path


SPEC_SHA256 = 'c89b1585a9e3e6ad7b3fa90e71caf8b692ab6be03f4a730e73d81353557736f3'
EXCLUDED = (1, 26, 46, 129, 188, 189, 214, 224, 276, 379)
INHERITED = (9, 19, 60, 81, 84, 168, 400)


def render(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != SPEC_SHA256:
        raise ValueError('bulk catalog requires the frozen WW147A specification SHA-256')
    rows = json.loads(data)['modules']
    if [row['id'] for row in rows] != list(range(1, 416)):
        raise ValueError('bulk catalog requires ordered unique IDs 1..415')
    selected = []
    for row in rows:
        mid = row['id']
        if mid in EXCLUDED or mid in INHERITED:
            continue
        spec = row['candidate_module_spec'] | {
            'id': mid, 'capture_group': 'bulk-ww149a', 'source_kind': row['source_kind'],
            'member': row['member'], 'image_alignment': row['image_alignment'],
            'sdk_classification': row['sdk_classification']}
        selected.append(json.dumps(spec, sort_keys=True, separators=(',', ':')))
    return '[\n' + ',\n'.join(selected) + '\n]\n'


def main():
    parser = argparse.ArgumentParser(description='Generate immutable bulk REL metadata, never game bytes')
    parser.add_argument('spec', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with args.output.open('x') as stream:
        stream.write(render(args.spec))


if __name__ == '__main__':
    main()
