"""One-shot memory snapshots for WW56 trigger events.

A snapshot reads the owned process's smaps, status, maps and thread list once,
plus the guest unit's cgroup memory.stat/current/events once, and writes them
under the receipt directory. Raw addresses and thread names stay local to the
receipts; no process memory bytes are read. The summary groups mappings by
backing and permission and keeps its limitations with it: the groups are
evidence about mapping classes, never proof of guest, JIT or renderer
ownership.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import time

_header = re.compile(r'^([0-9a-f]+)-([0-9a-f]+) ([rwxps-]{4}) ([0-9a-f]+) (\S+) (\d+)\s*(.*)$')
_metric = re.compile(r'(\w+):\s+(\d+) kB')

METRIC_KEYS = ('Size', 'Rss', 'Pss', 'Anonymous', 'Private_Clean', 'Private_Dirty',
               'Shared_Clean', 'Shared_Dirty', 'Swap')
GROUPS = ('anonymous_backing', 'file_or_memfd_backed', 'special_named', 'all_executable_mappings',
          'executable_anonymous_or_memfd_JIT_candidate_NOT_proven_JIT',
          'named_renderer_library_or_device_mappings_NOT_all_renderer_memory')
LIMITATIONS = (
    'Primary backing groups are disjoint; executable/JIT/renderer groups are overlapping subsets.',
    'Anonymous is the smaps resident anonymous metric, not guest attribution.',
    'Executable anonymous or memfd mappings are candidates, not proof of Dynarmic ownership.',
    'Renderer anonymous allocations and unmapped device memory cannot be attributed from smaps.',
    'RSS may count aliases; PSS is the kernel proportional accounting, not a synchronized physical census.',
)


def summarize_smaps(text):
    mappings = []
    current = None
    for line in text.splitlines():
        match = _header.match(line)
        if match:
            current = {'perms': match[3], 'path': match[7], 'metrics': {}}
            mappings.append(current)
        elif current is not None:
            metric = _metric.fullmatch(line)
            if metric:
                current['metrics'][metric[1]] = int(metric[2]) * 1024
    if not mappings:
        raise ValueError('smaps has no readable mapping header')
    totals = {key: 0 for key in METRIC_KEYS}
    groups = {name: {'mapping_count': 0, **{key: 0 for key in METRIC_KEYS}} for name in GROUPS}
    for mapping in mappings:
        name = mapping['path']
        anonymous = not name or name.startswith('[anon') or name in ('[heap]', '[stack]')
        tags = ['anonymous_backing' if anonymous else 'file_or_memfd_backed' if name.startswith('/')
                else 'special_named']
        if 'x' in mapping['perms']:
            tags.append('all_executable_mappings')
            if anonymous or 'memfd:' in name:
                tags.append('executable_anonymous_or_memfd_JIT_candidate_NOT_proven_JIT')
        if re.search(r'/dev/dri/|/dev/dma_heap/|/dev/udmabuf|lib(?:EGL|GL|glapi|drm)|/dri/', name):
            tags.append('named_renderer_library_or_device_mappings_NOT_all_renderer_memory')
        for key in METRIC_KEYS:
            value = mapping['metrics'].get(key, 0)
            totals[key] += value
            for tag in tags:
                groups[tag][key] += value
        for tag in tags:
            groups[tag]['mapping_count'] += 1
    return {'units': 'bytes', 'mapping_count': len(mappings), 'totals': totals, 'groups': groups,
            'limitations': list(LIMITATIONS)}


def capture_snapshot(proc_root, cgroup_relative, destination, *, event, trigger):
    proc = Path(proc_root)
    if (proc / 'comm').read_text().strip() != 'Vita3K':
        raise ValueError('snapshot target is not the owned Vita3K process: ' + str(proc))
    if Path(os.readlink(proc / 'exe')).name != 'Vita3K':
        raise ValueError('snapshot target executable is not Vita3K: ' + str(proc))
    observed = (proc / 'cgroup').read_text().strip().split('::', 1)[1]
    if observed != cgroup_relative:
        raise ValueError('snapshot target left its owned cgroup: ' + observed)
    destination = Path(destination)
    destination.mkdir(mode=0o700)
    started = time.monotonic()
    smaps = (proc / 'smaps').read_text()
    (destination / 'smaps.private.txt').write_text(smaps)
    (destination / 'status.private.txt').write_text((proc / 'status').read_text())
    (destination / 'maps.private.txt').write_text((proc / 'maps').read_text())
    threads = [{'tid': int(task.name), 'comm': (task / 'comm').read_text().strip()}
               for task in sorted((proc / 'task').iterdir(), key=lambda item: int(item.name))]
    (destination / 'threads.private.json').write_text(json.dumps(threads, indent=2) + '\n')
    base = Path('/sys/fs/cgroup' + cgroup_relative)
    readings = {name: (base / name).read_text()
                for name in ('memory.stat', 'memory.current', 'memory.events')}
    for name, content in readings.items():
        (destination / (name + '.txt')).write_text(content)
    summary = summarize_smaps(smaps)
    summary.update(event=event, semantic_trigger=trigger, pid=int(proc.name), cgroup=cgroup_relative,
                   captured_monotonic=started, duration_seconds=time.monotonic() - started,
                   cgroup_readings=readings, thread_count=len(threads),
                   privacy='Raw addresses, maps and thread names stay local; no memory bytes read.')
    (destination / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    return summary
