#!/usr/bin/env python3
"""Read-only discovery of GX dump runs, immutable PNG gallery and notification watch."""

import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time
import uuid

import capture
import extract
import image
import memory_snapshot

FORMAT = 'gx-frame-gallery-v1'
DEFAULT_SOURCE = Path('/home/fred/projects/zeldaWWHDRecomp/build-f5')


def run_id(report):
    for parent in report.parents:
        if re.fullmatch(r'vita3k-run-[0-9a-f]{32}', parent.name):
            return parent.name
    raise ValueError('report is not in an isolated Vita3K run directory')


def manifests(report):
    standard = report.with_name('ww05.mem1.json')
    return [standard] if standard.is_file() else sorted(report.parent.glob('ww05.mem1-*.snapshot.json'))


def discover(source, recent_hours, now):
    reports = []
    for report in source.glob('**/vita3k-run-*/data/**/ww05.report'):
        if report.with_suffix('.log').is_file() and manifests(report):
            if recent_hours == 0 or now - report.stat().st_mtime <= recent_hours * 3600:
                reports.append(report)
    return sorted(reports, key=lambda path: (path.stat().st_mtime, str(path)))


def bound_prefix(commands, digest):
    memory_snapshot.require_hash(digest, 'FIFO hash')
    rolling = hashlib.sha256()
    for count, data in enumerate(commands, 1):
        rolling.update(data)
        if rolling.hexdigest() == digest:
            return count
    raise ValueError('snapshot FIFO SHA-256 does not bind any logged whole-command prefix')


def inspect_run(report, code_revision):
    journal = report.with_suffix('.log')
    journal_bytes, report_bytes = journal.read_bytes(), report.read_bytes()
    commands = capture.journal_commands(journal_bytes.decode())
    choices = []
    for snapshot in manifests(report):
        if snapshot.stat().st_size > 16 << 20:
            raise ValueError('snapshot manifest exceeds 16 MiB')
        raw = snapshot.read_bytes()
        manifest = json.loads(raw, object_pairs_hook=memory_snapshot.unique_object)
        if not isinstance(manifest, dict) or manifest.get('format') != memory_snapshot.FORMAT:
            raise ValueError(f'unsupported snapshot format: {snapshot}')
        count = bound_prefix(commands, manifest.get('fifo_sha256'))
        draws = extract.draw_count(commands[:count])
        choices.append((draws, count, snapshot.stat().st_mtime_ns, snapshot.name, snapshot, raw))
    if not choices:
        raise ValueError('no replay snapshot manifest yet')
    draws, count, _, _, snapshot, raw = max(choices)
    if draws == 0:
        raise ValueError('snapshot-bound prefix has no logged draw yet')
    manifest = json.loads(raw)
    identity = hashlib.sha256()
    for data in (journal_bytes, report_bytes, raw, code_revision.encode()):
        identity.update(hashlib.sha256(data).digest())
    header = re.search(r'^WW05\s+\S+\s+([0-9a-f]{40})\b', report_bytes.decode(), re.MULTILINE)
    return dict(run_id=run_id(report), report=report, journal=journal, snapshot=snapshot,
                revision=identity.hexdigest(), draws=draws,
                journal_sha256=memory_snapshot.sha256(journal_bytes),
                report_sha256=memory_snapshot.sha256(report_bytes),
                manifest_sha256=memory_snapshot.sha256(raw),
                through_command=count if count < len(commands) else None,
                fifo_sha256=manifest['fifo_sha256'], source_commit=header[1] if header else 'not-recorded',
                date=datetime.fromtimestamp(report.stat().st_mtime, timezone.utc).strftime('%Y-%m-%d %H:%M:%S (UTC)'))


def atomic_write(path, text):
    temporary = path.with_name('.' + path.name + '.' + uuid.uuid4().hex + '.tmp')
    temporary.write_text(text)
    temporary.replace(path)


def read_state(output):
    output.mkdir(parents=True, exist_ok=True, mode=0o700)
    state_path = output / 'GALLERY.json'
    if not state_path.exists():
        if (output / 'INDEX.md').exists():
            raise ValueError('refusing to overwrite an unowned gallery index')
        return dict(format=FORMAT, entries=[], deferred={})
    state = json.loads(state_path.read_text())
    if state.get('format') != FORMAT or not isinstance(state.get('entries'), list) or not isinstance(state.get('deferred'), dict):
        raise ValueError('invalid gallery state; refusing to overwrite it')
    return state


def write_state(output, state):
    atomic_write(output / 'GALLERY.json', json.dumps(state, indent=2) + '\n')
    rows = ['# Private GX replay gallery', '',
            'Images are actual framebuffer readbacks, not gameplay or physical-Vita certification.',
            'Draws = snapshot-bound journal; rendered = this replay; lane 16 totals are separate.', '',
            '| Run / capture code SHA | Date | Draws | Rendered | Lane 16 draws | Image | PNG SHA-256 | Result |',
            '| --- | --- | ---: | ---: | ---: | --- | --- | --- |']
    for entry in state['entries']:
        link = f'[{entry["run_id"]}]({entry["entry_path"]}) / `{entry["source_commit"]}`'
        png = entry.get('image')
        size = f'[{png["width"]}×{png["height"]}, {entry["image_bytes"]} bytes]({entry["image_path"]})' if png else '—'
        checksum = f'`{png["sha256"]}`' if png else '—'
        result = ('**NONBLACK** ' if entry['nonblack'] else '') + entry['verdict']
        rows.append(f'| {link} | {entry["date"]} | {entry["draws"]} | {entry.get("rendered", "—")} | '
                    f'{entry.get("lane16_draws", "—")} | {size} | {checksum} | {result} |')
    if any(entry.get('image') for entry in state['entries']):
        rows += ['', '## Frames in arrival order']
        for entry in state['entries']:
            if entry.get('image'):
                rows += ['', f'### {entry["run_id"]} · {entry["date"]} · {entry["draws"]} draws',
                         f'![{entry["verdict"]}]({entry["image_path"]})']
    if state['deferred']:
        rows += ['', '## Waiting or refused input']
        rows += [f'- `{name}`: {error.replace(chr(10), " ")}' for name, error in sorted(state['deferred'].items())]
    atomic_write(output / 'INDEX.md', '\n'.join(rows) + '\n')


def render_candidate(candidate, output, build, code_revision):
    name = candidate['revision'][:12] + '-' + uuid.uuid4().hex[:8]
    job = output / candidate['run_id'] / name
    job.mkdir(parents=True, mode=0o700)
    frame = job / 'frame'
    fixture = build / 'gallery-fixtures' / candidate['run_id'] / name
    fixture.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    entry = {key: candidate[key] for key in ('run_id', 'revision', 'date', 'draws', 'source_commit', 'fifo_sha256')}
    entry.update(event='gallery-failed', verdict='image-unavailable', nonblack=False, image=None,
                 renderer_revision=code_revision, snapshot=str(candidate['snapshot']),
                 source_report=str(candidate['report']), entry_path=str((job / 'entry.json').relative_to(output)))
    try:
        metadata = extract.extract_fixture(candidate['journal'], fixture, report=candidate['report'],
                                            snapshot=candidate['snapshot'], through_command=candidate['through_command'])
        if (metadata['fifo_sha256'] != candidate['fifo_sha256'] or
                metadata['captured_draws'] != candidate['draws'] or
                metadata['source_sha256'] != candidate['journal_sha256'] or
                metadata['report_sha256'] != candidate['report_sha256'] or
                metadata['snapshot']['manifest_sha256'] != candidate['manifest_sha256']):
            raise ValueError('capture input changed while staging; await the new revision')
        entry['lane16_draws'] = metadata.get('reported_draws')
        status = image.render_fixture(fixture, metadata, frame, build)
        manifest = json.loads((frame / 'image.json').read_text())
        entry['renderer_revision'] = manifest.get('code_revision', code_revision)
        entry['verdict'] = manifest['verdict']
        entry['status'] = status
        entry['manifest_path'] = str((frame / 'image.json').relative_to(output))
        if manifest['image'] is not None:
            summary = re.findall(r'^draws=(\d+)\b', (frame / 'report.txt').read_text(), re.MULTILINE)
            if len(summary) != 1:
                raise ValueError('renderer report lacks one actual submitted-draw count')
            entry['rendered'] = int(summary[0])
            entry['image'] = manifest['image']
            entry['image_path'] = str((frame / 'capture.png').relative_to(output))
            entry['image_bytes'] = (frame / 'capture.png').stat().st_size
            entry['nonblack'] = (manifest['verdict'] in ('image-captured', 'image-captured-with-coverage-gaps') and
                                 entry['image']['alpha_nonzero'] > 0 and entry['image']['distinct_rgba'] > 1)
            entry['event'] = 'gallery-nonblack' if entry['nonblack'] else 'gallery-added'
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        entry.update(event='gallery-failed', verdict='image-unavailable', image=None, nonblack=False, error=str(error))
    atomic_write(job / 'entry.json', json.dumps(entry, indent=2) + '\n')
    return entry


def scan_once(source, output, build, *, recent_hours=24, code_revision):
    source, output, build = Path(source).resolve(), Path(output).resolve(), Path(build).resolve()
    if not source.is_dir():
        raise ValueError('capture source directory does not exist')
    if output.is_relative_to(source) or source.is_relative_to(output) or build.is_relative_to(source):
        raise ValueError('gallery/build output must be disjoint from read-only source runs')
    state = read_state(output)
    write_state(output, state)
    for report in discover(source, recent_hours, time.time()):
        name = run_id(report)
        try:
            candidate = inspect_run(report, code_revision)
            completed = any(entry['revision'] == candidate['revision'] for entry in state['entries'])
            if completed and name not in state['deferred']:
                continue
            memory_snapshot.load(candidate['snapshot'], candidate['fifo_sha256'])
        except (OSError, ValueError) as error:
            message = str(error)
            if state['deferred'].get(name) != message:
                state['deferred'][name] = message
                write_state(output, state)
                yield dict(event='gallery-deferred', run_id=name, error=message, nonblack=False)
            continue
        if completed:
            state['deferred'].pop(name, None)
            write_state(output, state)
            continue
        entry = render_candidate(candidate, output, build, code_revision)
        state['entries'].append(entry)
        state['deferred'].pop(name, None)
        write_state(output, state)
        yield entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--build', type=Path, default=image.DEFAULT_BUILD)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--recent-hours', type=float, default=24, help='0 includes all captured runs')
    parser.add_argument('--watch', action='store_true')
    parser.add_argument('--interval', type=float, default=30)
    parser.add_argument('--notify', action='store_true', help='exit after one update to wake the owning session')
    args = parser.parse_args()
    if not math.isfinite(args.interval) or not math.isfinite(args.recent_hours) or args.interval <= 0 or args.recent_hours < 0:
        parser.error('interval must be positive and recent-hours nonnegative')
    output = (args.output or args.build / 'images').resolve()
    if not args.source.is_dir():
        parser.error('capture source directory does not exist')
    if output.is_relative_to(args.source.resolve()) or args.build.resolve().is_relative_to(args.source.resolve()):
        parser.error('output/build may not be inside read-only source runs')
    output.mkdir(parents=True, exist_ok=True, mode=0o700)
    read_state(output)
    with (output / '.gallery.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error('another gallery watcher owns this output')
        watch_state = dict(pid=os.getpid(), source=str(args.source.resolve()), output=str(output),
                           started=datetime.now(timezone.utc).isoformat(), interval=args.interval)
        try:
            while True:
                code = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=image.ROOT, text=True).strip()
                atomic_write(output / 'WATCH.json', json.dumps(dict(watch_state, status='scanning'), indent=2) + '\n')
                for event in scan_once(args.source, output, args.build, recent_hours=args.recent_hours, code_revision=code):
                    atomic_write(output / 'UPDATE.json', json.dumps(event, indent=2) + '\n')
                    print(json.dumps(event, sort_keys=True), flush=True)
                    if args.notify:
                        return 0
                if not args.watch:
                    return 0
                atomic_write(output / 'WATCH.json', json.dumps(dict(watch_state, status='waiting'), indent=2) + '\n')
                time.sleep(args.interval)
        finally:
            atomic_write(output / 'WATCH.json', json.dumps(dict(watch_state, status='stopped'), indent=2) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
