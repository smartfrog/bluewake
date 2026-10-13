#!/usr/bin/env python3
"""WW56 in-scope gate: prove the unit's static budget, then wait for the
supervisor's one-shot permit and exec the phase command.

The gate runs inside the transient systemd unit before any guest code. It
refuses to exec when the kernel limits do not match the planned budget, writes
its own pid and cgroup for the supervisor, and never touches another unit.
"""

import json
import os
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))

from budget_properties import verify_cgroup_budget  # noqa: E402


def main():
    receipts = Path(sys.argv[1])
    phase = sys.argv[2]
    spec = json.loads((receipts / (phase + '-exec.json')).read_text())
    cgroup = Path('/proc/self/cgroup').read_text().strip().split('::', 1)[1]
    base = Path('/sys/fs/cgroup' + cgroup)
    verify_cgroup_budget((base / 'memory.max').read_text(),
                         (base / 'memory.swap.max').read_text(), spec['budget_bytes'])
    if base.name != spec['unit']:
        raise ValueError('gate cgroup ' + base.name + ' is not the planned unit ' + spec['unit'])
    if base.parent.name != 'app.slice':
        raise ValueError('gate unit must stay in app.slice outside capped scopes: ' + str(base.parent))
    (receipts / (phase + '-ready.json')).write_text(
        json.dumps({'pid': os.getpid(), 'cgroup': cgroup}) + '\n')
    deadline = time.monotonic() + spec['permit_timeout_seconds']
    while not (receipts / (phase + '-permit')).exists():
        if time.monotonic() >= deadline:
            raise TimeoutError('WW56 pre-exec authorization was not released')
        time.sleep(.05)
    os.chdir(spec['cwd'])
    os.execve(spec['command'][0], spec['command'], spec['environment'])


if __name__ == '__main__':
    raise SystemExit(main())
