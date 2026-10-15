#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
import re
import struct

import ship_contract


def render(receipt):
    selected = receipt['selected_rel_contracts']
    if not re.fullmatch('[0-9a-f]{64}', selected.get('build_identity', '')):
        raise ValueError('bulk policy requires an exact aggregate identity')
    contracts = selected['contracts']
    members = ship_contract.ACTOR_BATCHES['bulk']
    if set(contracts) != set(map(str, members)):
        raise ValueError('bulk policy requires exactly the 405 selected certificates')
    lines = []
    encoded = struct.pack('>I', 1)
    actors = 0
    for mid in members:
        contract = contracts[str(mid)]
        spec = ship_contract.selected_module(mid)
        if 'raw_sha256' in spec and contract['rel_sha256'] != spec['raw_sha256']:
            raise ValueError(f'module {mid}: bulk policy original REL pin mismatch')
        identity = contract['build_identity']
        if len(identity) != 64 or any(c not in '0123456789abcdef' for c in identity):
            raise ValueError(f'module {mid}: invalid certificate identity')
        actor = mid not in (60, 168, 400)
        bss = sum(size for _, offset, size in spec.get('section_layout', ()) if not offset)
        sdk = ('OWNER_PUBLISHED' if bss else 'CERTIFIED_INLINE_ZERO') if actor else 'EXISTING'
        value = {'EXISTING': 0, 'OWNER_PUBLISHED': 1, 'CERTIFIED_INLINE_ZERO': 2}[sdk]
        encoded += struct.pack('>III', mid, actor, value) + identity.encode()
        actors += actor
        lines.append(f'    {{{mid}u, "{identity}", {str(actor).lower()}, WW_ACTOR_SDK_{sdk}}},')
    pin = 0xCBF29CE484222325
    for byte in encoded:
        pin = ((pin ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    lines += [f'#define WW_ACTOR_COUNT {actors}u', f'#define WW_ACTOR_POLICY_PIN 0x{pin:016x}ull',
              f'#define WW_ACTOR_POLICY_IDENTITY "inline-zero-v1-fnv1a64-{pin:016x}"',
              f'#define WW_ACTOR_SET_IDENTITY "{selected["build_identity"]}"']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description='Pin the exact generated bulk certificate and SDK policy table')
    parser.add_argument('receipt', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with args.output.open('x') as stream:
        stream.write(render(json.loads(args.receipt.read_text())))


if __name__ == '__main__':
    main()
