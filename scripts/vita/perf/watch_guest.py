#!/usr/bin/env python3
"""Watch one perf guest: sample CPU, stop a faulted or hung run, keep the log.

Usage: watch_guest.py RUNDIR

RUNDIR holds `guest.pid`, the guest log under `profile/` and the emulator
console log at `profile/cache/Vita3K/vita3k.log`. The watcher appends
`cpu-samples.jsonl`, stops the guest on the first fault line (see
`scripts/vita/watch_faults.py`) or on a hang, and writes `live-end.txt` with the
reason and the matched line. Start it before the guest: this run's own
`ww05.log`/`ww05.partial` count as present outputs. It never deletes or truncates
either log.

A copied profile carries the previous run's console log, so the preflight refuses
a run directory whose console log already records a fault instead of reporting
that old fault as this run's. Rotate the console log before reusing a profile.
"""

import argparse
import json
import os
import signal
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from watch_faults import fault_match  # noqa: E402

FIXTURE_DIR = 'profile/data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture'
GUEST_LOG = FIXTURE_DIR + '/ww05.log'
CONSOLE_LOG = 'profile/cache/Vita3K/vita3k.log'
REQUIRED_REQUESTS = ('ww05.request',)
OPTIONAL_WITH_CONTINUE = ('ww05.audio', 'ww05.mem1')
PREVIOUS_OUTPUTS = ('ww05.dispatch', 'ww05.log', 'ww05.partial', 'ww05.report')
PRESENT = '[frame-present]'
SAMPLE_SECONDS = 0.5
DEADLINE_SECONDS = 240
HANG_SECONDS = 30
# Shader pre-compilation blocks in the driver and logs nothing between programs,
# so a low-CPU silent stretch there is normal: give it far longer than a spin.
IDLE_SILENT_SECONDS = 120
STALE_SCAN_BYTES = 4 * 1024 * 1024
IDLE_CPU = 0.05
BUSY_CPU = 0.5
TERM_GRACE_SECONDS = 1.0
CLOCK_TICKS = os.sysconf('SC_CLK_TCK')


def stat(pid):
    try:
        raw = Path(f'/proc/{pid}/stat').read_text()
    except OSError:
        return None
    fields = raw.rsplit(')', 1)[1].split()
    return {'state': fields[0], 'ticks': int(fields[11]) + int(fields[12]),
            'rss_pages': int(fields[21]), 'threads': int(fields[17])}


def alive(pid):
    current = stat(pid)
    return current is not None and current['state'] != 'Z'


def stop_guest(pid, grace=TERM_GRACE_SECONDS):
    """SIGTERM the guest, then SIGKILL it if it is still there after `grace`."""
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + grace
    while time.monotonic() < deadline:
        if not alive(pid):
            return
        time.sleep(0.05)
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def read_new(path, offset, pending=''):
    """Return the complete new lines since `offset`, the new offset and the tail."""
    size = path.stat().st_size if path.exists() else 0
    if size < offset:
        offset, pending = 0, ''
    if not path.exists():
        return [], offset, pending
    with path.open('rb') as stream:
        stream.seek(offset)
        raw = stream.read()
    complete, _, pending = (pending + raw.decode(errors='replace')).rpartition('\n')
    return complete.splitlines(), offset + len(raw), pending


def has_present(lines):
    return any(PRESENT in line for line in lines)


def verdict(cpu, silent_s, idle_s, since_present_s):
    """Name the hang the samples show, or None while the guest is healthy."""
    if cpu is None:
        return None
    if cpu > BUSY_CPU and since_present_s > HANG_SECONDS:
        return 'hung-no-present'
    if cpu < IDLE_CPU and silent_s > IDLE_SILENT_SECONDS and idle_s > IDLE_SILENT_SECONDS:
        return 'hung-low-cpu-silent'
    return None


def write_end(root, reason, detail=None):
    path = root / 'live-end.txt'
    text = f'reason={reason}\n'
    if detail:
        text += f'detail={detail.strip()}\n'
    path.write_text(text)
    return path


def wait_guest(pidfile, deadline):
    while time.monotonic() < deadline:
        try:
            pid = int(pidfile.read_text().strip())
        except (OSError, ValueError):
            pid = None
        if pid is not None and stat(pid) is not None:
            return pid
        time.sleep(SAMPLE_SECONDS)
    return None


def stale_console(root):
    """Name a fault a previous run left in the console log, or None."""
    console = root / CONSOLE_LOG
    if not console.exists():
        return None
    size = console.stat().st_size
    with console.open('rb') as stream:
        raw = stream.read(STALE_SCAN_BYTES)
        if size > STALE_SCAN_BYTES:
            stream.seek(size - STALE_SCAN_BYTES)
            raw += stream.read(STALE_SCAN_BYTES)
    match = fault_match(raw.decode(errors='replace'))
    if match is None:
        return None
    return f'{CONSOLE_LOG} already records a fault: {match.strip()[:120]}'


def preflight(root):
    """Name the first fixture problem that would waste a run, or None."""
    fixture = root / FIXTURE_DIR
    for name in REQUIRED_REQUESTS:
        if not (fixture / name).exists():
            return f'missing {name}'
    for name in OPTIONAL_WITH_CONTINUE:
        if (fixture / name).exists() and not (fixture / 'ww05.continue').exists():
            return f'{name} needs ww05.continue'
    for name in PREVIOUS_OUTPUTS:
        if (fixture / name).exists():
            return f'present {name}'
    return stale_console(root)


def watch(root, deadline_seconds=DEADLINE_SECONDS):
    problem = preflight(root)
    if problem is not None:
        write_end(root, 'preflight-failed', problem)
        return 1
    log = root / GUEST_LOG
    console = root / CONSOLE_LOG
    started = time.monotonic()
    deadline = started + deadline_seconds
    pid = wait_guest(root / 'guest.pid', deadline)
    if pid is None:
        write_end(root, 'no-guest')
        return 1
    samples = (root / 'cpu-samples.jsonl').open('a', buffering=1)
    offset, pending = 0, ''
    console_offset, console_pending = 0, ''
    previous, last_size = None, 0
    last_growth, idle_since, last_present = started, None, started
    reason, detail = 'exited', None
    try:
        while time.monotonic() < deadline:
            now = time.monotonic()
            lines, offset, pending = read_new(log, offset, pending)
            console_lines, console_offset, console_pending = read_new(
                console, console_offset, console_pending)
            size = (log.stat().st_size if log.exists() else 0) + (
                console.stat().st_size if console.exists() else 0)
            if size != last_size:
                last_size, last_growth = size, now
            if has_present(lines):
                last_present = now
            match = fault_match(lines + console_lines + [pending, console_pending])
            if match is not None:
                reason, detail = 'guest-fault', match
                stop_guest(pid)
                break
            current = stat(pid)
            if current is None:
                break
            cpu = None if previous is None else (
                (current['ticks'] - previous[1]) / CLOCK_TICKS / (now - previous[0]))
            previous = (now, current['ticks'])
            if cpu is not None and cpu < IDLE_CPU:
                idle_since = idle_since if idle_since is not None else now
            else:
                idle_since = None
            silent_s, idle_s = now - last_growth, now - (idle_since or now)
            since_present_s = now - last_present
            samples.write(json.dumps({
                't': round(now, 3), 'pid': pid,
                'cpu_fraction': None if cpu is None else round(cpu, 4),
                'rss_mb': round(current['rss_pages'] * 4096 / 2**20, 1),
                'threads': current['threads'], 'log_bytes': size,
                'log_silent_s': round(silent_s, 1),
                'since_present_s': round(since_present_s, 1)}) + '\n')
            hang = verdict(cpu, silent_s, idle_s, since_present_s)
            if hang is not None:
                reason = hang
                detail = (f'cpu={cpu:.2f} silent_s={silent_s:.1f} '
                          f'since_present_s={since_present_s:.1f}')
                stop_guest(pid)
                break
            time.sleep(SAMPLE_SECONDS)
        else:
            reason = 'ceiling'
    finally:
        samples.close()
        write_end(root, reason, detail)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path, help='perf run directory holding guest.pid and profile/')
    return watch(parser.parse_args().run)


if __name__ == '__main__':
    raise SystemExit(main())
