"""One-shot MemAvailable headroom gate for WW56 phases.

The coordinator's evidence-based envelope: the guest unit may use 4 GiB and
the trial requires 4.5 GiB MemAvailable before the guest phase is released;
the installer keeps its 2.5 GiB budget and requires the budget plus 512 MiB.
A deficit blocks the phase with a clear precondition message; the run never
proceeds and never touches another application.

`parse_mem_available` mirrors the validation of
`scripts/vita/ww05/activity.py:available_memory_bytes` on text input so the
gate logic is testable without reading /proc.
"""

from __future__ import annotations

GIB = 1024 ** 3
DEFAULT_INSTALL_BUDGET_BYTES = 2684354560
DEFAULT_GUEST_BUDGET_BYTES = 4 * GIB
DEFAULT_INSTALL_HEADROOM_BYTES = DEFAULT_INSTALL_BUDGET_BYTES + 512 * 1024 ** 2
DEFAULT_GUEST_HEADROOM_BYTES = 4 * GIB + 512 * 1024 ** 2


def parse_mem_available(meminfo_text):
    records = [line.split() for line in meminfo_text.splitlines()]
    available = [fields for fields in records if fields and fields[0] == 'MemAvailable:']
    if (len(available) != 1 or len(available[0]) != 3 or available[0][2] != 'kB' or
            not available[0][1].isdecimal()):
        raise ValueError('available memory requires one actual MemAvailable value in kB')
    return int(available[0][1]) * 1024


def headroom_decision(available_bytes, required_bytes):
    for name, value in (('available', available_bytes), ('required', required_bytes)):
        if type(value) is not int or value < 0:
            raise ValueError(name + ' byte count must be a nonnegative integer')
    return {'ok': available_bytes >= required_bytes, 'available_bytes': available_bytes,
            'required_bytes': required_bytes,
            'deficit_bytes': max(0, required_bytes - available_bytes),
            'memory_source': '/proc/meminfo:MemAvailable in kB'}
