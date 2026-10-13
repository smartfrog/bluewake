"""Watch isolated Vita3K phases, retaining CPU/log evidence and aborting hangs."""

import json
import os
from pathlib import Path
import signal
import subprocess
import time
import zipfile

import launch


def descendants(parent):
    entries = {}
    for path in Path('/proc').glob('[0-9]*/stat'):
        try:
            text = path.read_text()
            rest = text[text.rfind(')') + 2:].split()
            entries[int(path.parent.name)] = (int(rest[1]), int(rest[11]) + int(rest[12]), text)
        except (FileNotFoundError, ProcessLookupError):
            continue
    owned = {parent}
    while True:
        added = {pid for pid, entry in entries.items() if entry[0] in owned} - owned
        if not added:
            return {pid: entries[pid] for pid in owned if pid in entries}
        owned.update(added)


def stop(process):
    for pid in descendants(process.pid):
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    try:
        return process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        for pid in descendants(process.pid):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        return process.wait(timeout=5)


def run_phase(root, stack, name, arguments, timeout, wayland_socket):
    log = root / 'phases' / f'{name}.log'
    report = root / launch.UX0_DATA / 'report.txt'
    started = time.monotonic()
    previous, previous_size, idle_since = {}, 0, None
    reason = 'exited'
    with log.open('wb') as output, (root / 'evidence' / f'{name}-watch.jsonl').open('w') as evidence:
        process = subprocess.Popen(launch.sandbox_command(root, stack, arguments, wayland_socket),
                                   cwd=root / 'work', stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        while process.poll() is None:
            now = time.monotonic()
            emulators = {pid: entry[1] for pid, entry in descendants(process.pid).items()
                         if '(Vita3K)' in entry[2]}
            delta = sum(ticks - previous.get(pid, ticks) for pid, ticks in emulators.items())
            cpu = delta / os.sysconf('SC_CLK_TCK') * 100 / 0.5
            previous = emulators
            size = log.stat().st_size + (report.stat().st_size if report.exists() else 0)
            evidence.write(json.dumps(dict(seconds=round(now-started, 2), cpu_percent=round(cpu, 2),
                                           pids=list(emulators), log_bytes=size)) + '\n')
            evidence.flush()
            with log.open('rb') as recent_log:
                recent_log.seek(max(0, log.stat().st_size - 65536))
                recent = recent_log.read().decode(errors='replace')
            if 'PC is 0x0' in recent or '|E| [MemoryWrite]: Invalid write' in recent:
                reason = 'emulator-memory-fault'
                status = stop(process)
                break
            if report.exists() and 'demo done' in report.read_text(errors='replace'):
                reason = 'demo-report-complete'
                status = stop(process)
                break
            if name == 'install':
                app = root / launch.FIRMWARE / 'ux0/app' / launch.TITLE_ID
                with zipfile.ZipFile(root / 'runtime' / arguments[-1]) as archive:
                    installed = all((app / entry.filename).is_file() and
                                    (app / entry.filename).stat().st_size == entry.file_size
                                    for entry in archive.infolist() if not entry.is_dir())
                if installed:
                    reason = 'vpk-files-installed'
                    status = stop(process)
                    break
            if emulators and cpu < 5 and size == previous_size:
                idle_since = idle_since if idle_since is not None else now
                if now - idle_since > 30:
                    reason = 'hung-low-cpu-silent'
                    status = stop(process)
                    break
            else:
                idle_since = None
            previous_size = size
            if now - started > timeout:
                reason = 'healthy-run-ceiling' if cpu >= 5 else 'ceiling'
                status = stop(process)
                break
            time.sleep(0.5)
        else:
            status = process.returncode
    return dict(phase=name, seconds=round(time.monotonic()-started, 3), status=status,
                reason=reason, log=str(log))
