#!/usr/bin/env python3
"""Inventory captured command boundaries without guessing opcodes in payloads."""

import argparse
from collections import Counter
import json
from pathlib import Path

import capture


def inventory(path):
    commands = capture.journal_commands(path.read_text())
    if not commands:
        raise ValueError(f'no command journal: {path}')
    counts = Counter(f'{data[0]:02X}' for data in commands)
    bp = Counter(f'{data[1]:02X}' for data in commands if data[0] == 0x61)
    cp = Counter(f'{data[1]:02X}' for data in commands if data[0] == 0x08)
    xf = Counter(f'{int.from_bytes(data[3:5], "big"):04X}'
                 for data in commands if data[0] == 0x10)
    return dict(source=str(path), commands=len(commands),
                bytes=sum(map(len, commands)), coverage='logged-command-prefix-only',
                opcodes=dict(sorted(counts.items())), bp=dict(sorted(bp.items())),
                cp=dict(sorted(cp.items())), xf=dict(sorted(xf.items())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal', type=Path, nargs='+')
    args = parser.parse_args()
    for path in args.journal:
        print(json.dumps(inventory(path), sort_keys=True))


if __name__ == '__main__':
    main()
