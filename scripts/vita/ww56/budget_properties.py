"""Pre-exec static-budget verification for WW56 phase units.

Before a phase gate releases the guest command, the supervisor reads the
unit's properties with `systemctl --user show` and the unit's kernel limits
from its cgroup files, then proves all of them against the static budget. A
mismatch is a hard error: the guest command is never released.
"""

from __future__ import annotations

PROPERTIES = ('ControlGroup', 'MainPID', 'MemoryMax', 'EffectiveMemoryMax', 'MemorySwapMax',
              'OOMPolicy', 'ActiveState', 'SubState', 'Result',
              'ExecMainCode', 'ExecMainStatus', 'CPUUsageNSec', 'MemoryPeak')


def parse_properties(text):
    properties = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        key, separator, value = line.partition('=')
        if not separator or not key:
            raise ValueError('unreadable property line: ' + line)
        if key in properties:
            raise ValueError('duplicate property: ' + key)
        properties[key] = value
    return properties


def verify_budget_properties(props, *, budget_bytes, swap_max=0, oom_policy='stop'):
    if type(budget_bytes) is not int or budget_bytes <= 0:
        raise ValueError('budget must be a positive integer byte count')
    required = {'MemoryMax': str(budget_bytes), 'EffectiveMemoryMax': str(budget_bytes),
                'MemorySwapMax': str(swap_max), 'OOMPolicy': oom_policy}
    for key, expected in required.items():
        actual = props.get(key)
        if actual != expected:
            raise ValueError(key + ' is ' + repr(actual) + ', expected ' + repr(expected))
    return {key: props[key] for key in required}


def parse_cgroup_limit(text):
    value = text.strip()
    if value == 'max':
        return 'max'
    if value.isdecimal():
        return int(value)
    raise ValueError('memory limit must be "max" or a byte count: ' + repr(text))


def verify_cgroup_budget(memory_max_text, memory_swap_max_text, budget_bytes):
    if type(budget_bytes) is not int or budget_bytes <= 0:
        raise ValueError('budget must be a positive integer byte count')
    memory_max = parse_cgroup_limit(memory_max_text)
    swap_max = parse_cgroup_limit(memory_swap_max_text)
    if memory_max != budget_bytes:
        raise ValueError('kernel memory.max is ' + repr(memory_max) + ', expected ' + str(budget_bytes))
    if swap_max != 0:
        raise ValueError('kernel memory.swap.max is ' + repr(swap_max) + ', expected 0')
    return {'kernel_memory_max': memory_max, 'kernel_swap_max': swap_max}
