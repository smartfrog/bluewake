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
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))

from budget_properties import verify_cgroup_budget  # noqa: E402
from phase_identity import process_identity  # noqa: E402


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
    identity = {'pid': os.getpid(), 'cgroup': cgroup}
    if len(sys.argv) == 4:
        invocation = os.environ.get('INVOCATION_ID', '')
        if re.fullmatch(r'[0-9a-f]{32}', invocation) is None or invocation == '0' * 32:
            raise ValueError('gate requires the manager invocation ID')
        if re.fullmatch(r'[0-9a-f]{64}', sys.argv[3]) is None:
            raise ValueError('gate requires a fresh launch nonce')
        identity.update(launch_nonce=sys.argv[3], invocation_id=invocation,
                        starttime=process_identity(os.getpid())[0])
    ready = receipts / (phase + '-ready.json')
    temporary = receipts / (phase + '-ready.tmp')
    with temporary.open('x') as stream:
        os.fchmod(stream.fileno(), 0o600)
        stream.write(json.dumps(identity) + '\n')
    os.link(temporary, ready)
    temporary.unlink()
    deadline = time.monotonic() + spec['permit_timeout_seconds']
    while not (receipts / (phase + '-permit')).exists():
        if time.monotonic() >= deadline:
            raise TimeoutError('WW56 pre-exec authorization was not released')
        time.sleep(.05)
    os.chdir(spec['cwd'])
    os.execve(spec['command'][0], spec['command'], spec['environment'])


if __name__ == '__main__':
    raise SystemExit(main())
