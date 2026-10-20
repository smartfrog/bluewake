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

ROOT = Path(__file__).resolve().parents[3]

import capture
import extract
import image
import memory_snapshot
import novelty

FORMAT = 'gx-frame-gallery-v1'
DEFAULT_SOURCE = ROOT / 'build-f5'


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
    last_draw = max(number for number, data in enumerate(commands[:count], 1)
                    if data[0] & 0xF8 in extract.DRAW_FAMILIES)
    manifest = json.loads(raw)
    identity = hashlib.sha256()
    for data in (journal_bytes, report_bytes, raw, code_revision.encode()):
        identity.update(hashlib.sha256(data).digest())
    header = re.search(r'^WW05\s+\S+\s+([0-9a-f]{40})\b', report_bytes.decode(), re.MULTILINE)
    return dict(run_id=run_id(report), report=report, journal=journal, snapshot=snapshot,
                 revision=identity.hexdigest(), draws=draws, last_draw_command=last_draw,
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
        if entry.get('new_visual_content'):
            label = '**NEW CONTENT CANDIDATE** '
        elif entry.get('visual', {}).get('classification') == 'logo-compatible':
            label = 'logo-compatible '
        else:
            label = '**NONBLACK** ' if entry['nonblack'] else ''
        result = label + entry['verdict']
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


def render_candidate(candidate, output, build, code_revision, known_logo=None):
    name = candidate['revision'][:12] + '-' + uuid.uuid4().hex[:8]
    job = output / candidate['run_id'] / name
    job.mkdir(parents=True, mode=0o700)
    frame = job / 'frame'
    fixture = build / 'gallery-fixtures' / candidate['run_id'] / name
    fixture.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    entry = {key: candidate[key] for key in ('run_id', 'revision', 'date', 'draws', 'source_commit',
                                            'fifo_sha256', 'last_draw_command')}
    entry.update(event='gallery-failed', verdict='image-unavailable', nonblack=False, image=None,
                 new_visual_content=False,
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
            if known_logo is not None and manifest['verdict'] in ('image-captured', 'image-captured-with-coverage-gaps'):
                entry['visual'] = novelty.classify_frame(frame / 'capture.png', entry['image']['sha256'], known_logo)
                entry['nonblack'] = entry['visual']['nonblack_RGB_pixels'] > 0
                entry['new_visual_content'] = entry['visual']['classification'] == 'new-content-candidate'
                entry['event'] = ('gallery-new-content' if entry['new_visual_content'] else
                                  'gallery-known-logo' if entry['nonblack'] else 'gallery-added')
        if manifest['verdict'] == 'image-readback-blocked':
            entry['event'] = 'gallery-failed'
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        entry.update(event='gallery-failed', verdict='image-unavailable', image=None,
                     nonblack=False, new_visual_content=False, error=str(error))
    atomic_write(job / 'entry.json', json.dumps(entry, indent=2) + '\n')
    return entry


def scan_once(source, output, build, *, recent_hours=24, code_revision, after_command=0, known_logo=None):
    if type(after_command) is not int or after_command < 0:
        raise ValueError('command threshold must be a nonnegative integer')
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
            if candidate['last_draw_command'] <= after_command:
                continue
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
        entry = render_candidate(candidate, output, build, code_revision, known_logo)
        state['entries'].append(entry)
        state['deferred'].pop(name, None)
        write_state(output, state)
        yield entry


def notification_required(event, *, new_content_only):
    return (not new_content_only or bool(event.get('new_visual_content')) or
            event['event'] in ('gallery-failed', 'gallery-deferred'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--build', type=Path, default=image.DEFAULT_BUILD)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--recent-hours', type=float, default=24, help='0 includes all captured runs')
    parser.add_argument('--watch', action='store_true')
    parser.add_argument('--interval', type=float, default=30)
    parser.add_argument('--notify', action='store_true',
                        help='exit on an update; with --known-logo, only visual candidates or blockers')
    parser.add_argument('--after-command', type=int, default=0,
                        help='replay only snapshots whose last bound draw is after this command')
    parser.add_argument('--known-logo', type=Path,
                        help='verified full-red logo PNG; notify only on content outside its envelope')
    args = parser.parse_args()
    if (not math.isfinite(args.interval) or not math.isfinite(args.recent_hours) or
            args.interval <= 0 or args.recent_hours < 0):
        parser.error('interval must be positive and recent-hours nonnegative')
    if args.after_command < 0:
        parser.error('command threshold must be a nonnegative integer')
    try:
        known_logo = novelty.load_logo(args.known_logo) if args.known_logo is not None else None
    except (OSError, ValueError) as error:
        parser.error(str(error))
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
                            started=datetime.now(timezone.utc).isoformat(), interval=args.interval,
                            after_command=args.after_command,
                            known_logo_sha256=known_logo['sha256'] if known_logo is not None else None)
        try:
            while True:
                code = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=image.ROOT, text=True).strip()
                atomic_write(output / 'WATCH.json', json.dumps(dict(watch_state, status='scanning'), indent=2) + '\n')
                for event in scan_once(args.source, output, args.build, recent_hours=args.recent_hours,
                                       code_revision=code, after_command=args.after_command, known_logo=known_logo):
                    atomic_write(output / 'UPDATE.json', json.dumps(event, indent=2) + '\n')
                    print(json.dumps(event, sort_keys=True), flush=True)
                    if args.notify and notification_required(event, new_content_only=known_logo is not None):
                        return 1 if known_logo is not None and event['event'] in ('gallery-failed', 'gallery-deferred') else 0
                if not args.watch:
                    return 0
                atomic_write(output / 'WATCH.json', json.dumps(dict(watch_state, status='waiting'), indent=2) + '\n')
                time.sleep(args.interval)
        finally:
            atomic_write(output / 'WATCH.json', json.dumps(dict(watch_state, status='stopped'), indent=2) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
