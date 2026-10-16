"""Kernel process identity shared by the phase gate and its supervisor."""

from collections import namedtuple
from pathlib import Path


OwnedIdentity = namedtuple('OwnedIdentity', 'pid cgroup starttime invocation_id launch_nonce')


def process_identity(pid):
    root = Path('/proc') / str(pid)
    stat = (root / 'stat').read_text()
    starttime = int(stat.rsplit(')', 1)[1].split()[19])
    cgroup = (root / 'cgroup').read_text().strip().split('::', 1)[1]
    return starttime, cgroup


def original_process_gone(owned):
    try:
        current = process_identity(owned.pid)
    except (FileNotFoundError, ProcessLookupError):
        return True
    if current != (owned.starttime, owned.cgroup):
        raise ValueError('refusing reused phase PID ownership: ' + str(owned.pid))
    return False
