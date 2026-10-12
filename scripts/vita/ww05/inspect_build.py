"""Offline ELF/stack/owner inspection only. Never loads or executes the game."""

import json
from pathlib import Path
import re
import subprocess
import struct

from game_build import OUTPUT, STACK, sha256


def inspect(build):
    elf = build / 'ww05_host'
    prefix = STACK / 'vitasdk/bin/arm-vita-eabi-'
    records = {}
    for name, arguments in {'elf': ['readelf', '-W', '-h', '-l', '-A'],
                            'sections': ['size', '-A'], 'symbols': ['nm', '-S', '--size-sort'],
                            'abi': ['objdump', '-s', '-j', '.fixture_abi']}.items():
        tool, *options = arguments
        content = subprocess.check_output([str(prefix) + tool, *options, str(elf)], text=True)
        (OUTPUT / 'logs' / ('inspection-' + name + '.log')).write_text(content)
        records[name] = content
    segments = []
    for offset, virtual, physical, files, memory, flags, alignment in re.findall(
        r'^\s*LOAD\s+(0x\w+)\s+(0x\w+)\s+(0x\w+)\s+(0x\w+)\s+(0x\w+)\s+([RWE ]+)\s+(0x\w+)', records['elf'], re.M):
        segments.append({'offset': int(offset, 16), 'virtual': int(virtual, 16),
                         'file_bytes': int(files, 16), 'memory_bytes': int(memory, 16), 'flags': flags.strip()})
    if not segments or 'Tag_ABI_VFP_args: VFP registers' not in records['elf']:
        raise ValueError('expected ARMv7 hard-float LOAD segments')
    sections = {name: int(size) for name, size in re.findall(r'^(\.\S+)\s+(\d+)\s+\d+', records['sections'], re.M)}
    stacks = []
    for path in build.rglob('*.su'):
        for line in path.read_text().splitlines():
            name, size, kind = line.split('\t')
            stacks.append({'name': name.rsplit(':', 1)[-1], 'bytes': int(size), 'kind': kind})
    stacks.sort(key=lambda entry: entry['bytes'], reverse=True)
    if not stacks or stacks[0]['bytes'] >= 262144:
        raise ValueError('compiled frame cannot fit the explicitly reserved worker stack')
    symbols = records['symbols']
    for symbol in ('cpu_init', 'cpu_free', 'ppc_guest_alias_add', 'staticrecomp_get_module', 'dolrecomp_indirect_dispatch',
                   'dvd_open_image', 'dvd_read_to_guest', 'bw_dvd_fopen', 'bw_dvd_fread'):
        if len(re.findall(r'^[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+[A-Za-z]\s+' + symbol + r'$', symbols, re.M)) != 1:
            raise ValueError('single runtime/dispatch owner violated: ' + symbol)
    data = (OUTPUT / 'composite/rel_data.inc').read_text()
    rows = re.findall(r'\{(\d+)u, (\d+)u, (0x[0-9A-Fa-f]+)u, (0x[0-9A-Fa-f]+|\d+)u, ([^}]+)\}', data)
    alias_estimate = sum(int(row[3], 0) for row in rows if not (row[0] == '336' and row[1] == '6'))
    with (OUTPUT / 'private/main.dol').open('rb') as stream:
        header = stream.read(256)
    text_snapshot_bytes = sum(struct.unpack_from('>7I', header, 0x90))
    result = {'elf_sha256': sha256(elf), 'elf_bytes': elf.stat().st_size,
              'PT_LOAD': segments, 'PT_LOAD_memory_bytes': sum(entry['memory_bytes'] for entry in segments),
              'sections': sections, 'largest_compiled_frame': stacks[0], 'stack_frames': stacks,
              'worker_reserved_bytes': 262144, 'newlib_heap_container_bytes': 100663296,
              'estimated_backings_inside_heap': {'mem1': 25165824, 'vm': 33554432, 'locked_cache': 16384,
                                                'exported_rel_aliases': alias_estimate,
                                                'compiled_text_snapshot': text_snapshot_bytes},
              'dol_loader_transient_requested_bytes': (OUTPUT / 'private/main.dol').stat().st_size,
               'actual_guest_allocations': 'NOT OBSERVED by offline inspection; see individual startup reports',
              'unknown': ['native stack high-water', 'physical residency', 'allocator overhead',
                          'physical Vita limits', 'SDK pool peak', 'largest contiguous block', 'GPU resources'],
              'accounting': 'heap backings are inside 96 MiB, not added twice; CPU/GX state are inside host allocation; '
                            'ELF PT_LOAD is nominal mapping, not observed resident/pool usage; stack frames are not call-chain peak'}
    (OUTPUT / 'footprint.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    inspect(OUTPUT / 'vita')
