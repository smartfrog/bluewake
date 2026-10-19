#!/usr/bin/env python3
"""Install and run WW05 natively in a fresh private profile, preserving all evidence."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import stat
import subprocess
import traceback
import uuid
import zipfile

from game_build import OUTPUT, ROOT, checked_sources, sha256
from stage_plan import (stage, check, validate_continuation, validate_timeout, verify_package, checked_build_receipt,
                        is_full_hex_identity,
                        load_candidate_descriptor, checked_candidate_receipt, verify_candidate_package)
from report import MAX_JOURNAL_COMMANDS, matched, validate, validate_commands
from fp_campaign import emitted_config, restore_config, runtime_inputs, verify_phase_logs, verify_stopped
from fp_launch import REPORT_DIRECTORY, evidence_file, run_owned
from direct_launch import RENDERER, direct_phases
from fp_libraries import directory_fd, file_identity, library_identity
from fp_profile import FIRMWARE_DIRECTORY, config_identity, read_profile_file, verify_firmware
from activity import ProcessIdleObserver, available_memory_bytes
from memory_capture import (MAX_CAPTURES, publish_replay_manifests, snapshot_outputs, validate_frame_window,
                            validate_memory_captures, verify_snapshots)
from frame_capture import MAX_FRAMES, collect_frames, frame_outputs

PHASE_LOG_LIMIT = 64 * 1024 * 1024


def verify_inputs(root, plan):
    if plan.get('execution_mode') != 'direct-native' or plan.get('renderer') != RENDERER:
        raise ValueError('WW launch requires an honest direct-native plan')
    source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if source != plan.get('launcher_source', plan['source']) or subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True):
        raise ValueError('source checkpoint changed')
    checked_sources()
    receipt_path = Path(plan.get('build_evidence_path', OUTPUT / 'build-evidence.json'))
    if sha256(receipt_path) != plan['build_evidence_sha256']:
        raise ValueError('selected build receipt changed')
    candidate = None
    if plan.get('candidate_build', False):
        descriptor_path = Path(plan['candidate']['descriptor'])
        if sha256(descriptor_path) != plan['candidate']['descriptor_sha256']:
            raise ValueError('selected candidate descriptor changed')
        candidate = load_candidate_descriptor(descriptor_path)
        if checked_candidate_receipt(candidate, source) != plan['build_evidence']:
            raise ValueError('selected guest build identity changed')
    elif checked_build_receipt(receipt_path, source, plan.get('baseline_build', False)) != plan['build_evidence']:
        raise ValueError('selected guest build identity changed')
    validate_timeout(plan['phases']['guest']['timeout_seconds'],
                     scene_capture=plan.get('mem1_command_window') is not None,
                     idle_timeout=plan.get('idle_timeout_seconds', 30))
    expected = direct_phases(root, plan['phases']['guest']['timeout_seconds'],
                             Path(plan['runtime_libraries']['root']),
                             Path(plan['wayland_socket']) if plan['wayland_socket'] is not None else None)
    if plan['phases'] != expected:
        raise ValueError('direct native commands, environment or phase identities changed')
    if candidate is not None:
        vpk, package = verify_candidate_package(candidate, plan['build_evidence'])
    else:
        vpk, package = verify_package(plan['build_evidence'])
    if str(vpk) != plan['source_vpk'] or package != plan['vpk_sha256']:
        raise ValueError('source package identity changed')
    if sha256(root / 'runtime/ww05.vpk') != package:
        raise ValueError('staged package changed')
    if runtime_inputs(Path(plan['runtime_input'])) != plan['runtime_inputs']:
        raise ValueError('runtime source/assets changed')
    if library_identity(root / 'runtime', allow_symlinks=False) != plan['staged_runtime']:
        raise ValueError('staged runtime/assets/package changed')
    if library_identity(Path(plan['runtime_libraries']['root'])) != plan['runtime_libraries']:
        raise ValueError('copied public library tree changed')
    verify_firmware(plan['firmware'])
    if plan.get('shader_compiler') is not None:
        shader = plan['shader_compiler']
        if (file_identity(Path(shader['source'])) != shader['identity'] or
            file_identity(root / shader['install_path']) != shader['staged']):
            raise ValueError('private shader compiler changed')
    if plan.get('disc') is not None:
        disc = plan['disc']
        if file_identity(Path(disc['path'])) != disc['identity']:
            raise ValueError('verified private disc image changed')
        link = root / disc['install_path']
        with directory_fd(link.parent):
            if not link.is_symlink() or os.readlink(link) != disc['path']:
                raise ValueError('private disc link changed')


def verify_installed(root, plan):
    apps = root / 'data/Vita3K/Vita3K/ux0/app'
    title = apps / 'BWWG00005'
    for path, names in ((apps, {'BWWG00005'}), (title, {'eboot.bin', 'main.dol', 'sce_sys'}),
                        (title / 'sce_sys', {'param.sfo'})):
        with directory_fd(path) as descriptor:
            if os.fstat(descriptor).st_uid != os.getuid() or set(os.listdir(descriptor)) != names:
                raise ValueError('unexpected installed title entries or owner')
    payload = {}
    with zipfile.ZipFile(root / 'runtime/ww05.vpk') as archive:
        for name in ('eboot.bin', 'main.dol', 'sce_sys/param.sfo'):
            path = title / name
            info = path.lstat()
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_nlink != 1:
                raise ValueError('installed payload must have one private owner/link')
            identity = file_identity(path)
            with archive.open(name) as stream:
                expected = hashlib.file_digest(stream, 'sha256').hexdigest()
            if identity['bytes'] != archive.getinfo(name).file_size or identity['sha256'] != expected:
                raise ValueError('installed payload mismatch: ' + name)
            payload[name] = identity
    return payload


def request_content(token, source, package, count=None):
    suffix = '' if count is None else f' {count}'
    return f'{token} {source} {package}{suffix}\n'


def fresh_request(root, source, package, continue_gx=0, mem1_snapshots=0, defer_audio=False, mem1_command_window=None,
                  frame_window=None, *, token=None):
    validate_continuation(continue_gx)
    validate_memory_captures(mem1_snapshots, continue_gx, mem1_command_window)
    validate_frame_window(frame_window, mem1_snapshots)
    if type(defer_audio) is not bool or (defer_audio and not continue_gx):
        raise ValueError('audio deferral requires the explicit continuation profile')
    if token is not None and not is_full_hex_identity(token, 32):
        raise ValueError('explicit request token must be lowercase 32-hex')
    directory = root
    for part in REPORT_DIRECTORY.parts:
        with directory_fd(directory) as parent:
            try:
                os.mkdir(part, mode=0o700, dir_fd=parent)
            except FileExistsError:
                pass
        directory = directory / part
    token = uuid.uuid4().hex if token is None else token
    with directory_fd(directory) as parent:
        if os.fstat(parent).st_uid != os.getuid():
            raise ValueError('report directory must be privately owned')
        existing = set(os.listdir(parent))
        if ('frames' in existing or existing & {'ww05' + suffix for suffix in ('.request', '.partial', '.report', '.log', '.dispatch', '.continue', '.mem1', '.mem1-start', '.mem1-end', '.frame-start', '.frame-limit', '.audio')} or
            any(name.startswith('ww05.mem1-') for name in existing)):
            raise ValueError('preexisting WW05 request/output; refusing to delete or reuse it')
        descriptor = os.open('ww05.request', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                             0o600, dir_fd=parent)
    with os.fdopen(descriptor, 'w') as stream:
        stream.write(request_content(token, source, package))
    if continue_gx:
        with directory_fd(directory) as parent:
            descriptor = os.open('ww05.continue', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                                 0o600, dir_fd=parent)
        with os.fdopen(descriptor, 'w') as stream:
            stream.write(request_content(token, source, package, continue_gx))
    if mem1_snapshots:
        with directory_fd(root / 'evidence') as parent:
            os.mkdir('frames', mode=0o700, dir_fd=parent)
        with directory_fd(directory) as parent:
            os.symlink(str(root / 'evidence/frames'), 'frames', dir_fd=parent)
        with directory_fd(directory) as parent:
            descriptor = os.open('ww05.mem1', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                                 0o600, dir_fd=parent)
        with os.fdopen(descriptor, 'w') as stream:
            stream.write(request_content(token, source, package, mem1_snapshots))
    if mem1_command_window is not None:
        for suffix, value in zip(('start', 'end'), mem1_command_window):
            with directory_fd(directory) as parent:
                descriptor = os.open('ww05.mem1-' + suffix, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                                     0o600, dir_fd=parent)
            with os.fdopen(descriptor, 'w') as stream:
                stream.write(request_content(token, source, package, value))
    if frame_window is not None:
        for suffix, value in zip(('start', 'limit'), frame_window):
            with directory_fd(directory) as parent:
                descriptor = os.open('ww05.frame-' + suffix, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                                     0o600, dir_fd=parent)
            with os.fdopen(descriptor, 'w') as stream:
                stream.write(request_content(token, source, package, value))
    if defer_audio:
        with directory_fd(directory) as parent:
            descriptor = os.open('ww05.audio', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                                 0o600, dir_fd=parent)
        with os.fdopen(descriptor, 'w') as stream:
            stream.write(request_content(token, source, package, 1))
    return token


def activity_observer(root, plan, phase, extra_outputs=()):
    outputs = (phase / 'evidence/process.log', root / 'cache/Vita3K/vita3k.log',
               *(root / REPORT_DIRECTORY / ('ww05' + suffix)
                  for suffix in ('.log', '.partial', '.report', '.dispatch')),
               *snapshot_outputs(root / REPORT_DIRECTORY),
               *frame_outputs(root / 'evidence/frames', plan.get('frame_capture_start', 1),
                              plan.get('frame_capture_limit', MAX_FRAMES)),
               *extra_outputs)
    return ProcessIdleObserver(plan.get('idle_timeout_seconds', 30), plan.get('cpu_threshold', 0.05), outputs,
                              plan.get('minimum_available_memory_bytes', 0))


def memory_preflight(root, plan, phase):
    minimum = plan.get('minimum_available_memory_bytes', 0)
    if not minimum:
        return
    available = available_memory_bytes()
    with evidence_file(root, f'memory-before-{phase}.json') as stream:
        json.dump({'source': plan['source'], 'phase': phase, 'available_memory_bytes': available,
                   'minimum_available_memory_bytes': minimum, 'ready': available >= minimum,
                   'memory_source': '/proc/meminfo:MemAvailable in kB'}, stream, indent=2)
        stream.write('\n')
    if available < minimum:
        raise MemoryError(f'pause before {phase}: available user RAM {available} bytes is below {minimum} bytes')


def installer_observer(root, plan):
    with zipfile.ZipFile(root / 'runtime/ww05.vpk') as archive:
        expected = {name: archive.getinfo(name).file_size
                    for name in ('eboot.bin', 'main.dol', 'sce_sys/param.sfo')}
    title = root / 'data/Vita3K/Vita3K/ux0/app/BWWG00005'
    idle = activity_observer(root, plan, Path(plan['phases']['install']['evidence_root']),
                             tuple(title / name for name in expected))

    def observe(pid):
        try:
            ready = all((title / name).stat().st_size == size for name, size in expected.items())
        except FileNotFoundError:
            ready = False
        if ready:
            return {'stop_reason': 'installed_payload', 'payload': verify_installed(root, plan)}
        return idle(pid)

    return observe


def execute_run(root, plan):
    record = {'run': plan['run'], 'source': plan['source'], 'vpk_sha256': plan['vpk_sha256'],
              'renderer': dict(RENDERER), 'execution_mode': 'direct-native',
              'launcher_source': plan.get('launcher_source', plan['source']),
               'continue_gx': plan.get('continue_gx', 0),
               'mem1_snapshots': plan.get('mem1_snapshots', 0),
               'mem1_command_window': plan.get('mem1_command_window'),
               'audio': 'deferred' if plan.get('defer_audio', False) else 'strict',
               'extended_scene_capture': plan.get('extended_scene_capture', False),
               'minimum_available_memory_bytes': plan.get('minimum_available_memory_bytes', 0),
              'frame_capture_start': plan.get('frame_capture_start', 1),
              'frame_capture_limit': plan.get('frame_capture_limit', MAX_FRAMES),
              'completion': 'INCOMPLETE', 'reported_frontier': 'UNAVAILABLE',
              'frontier_verified': False, 'rendered': False}
    with evidence_file(root, 'execution.json') as stream:
        json.dump({'source': plan['source'], 'run': plan['run'], 'real_execution': True,
                   'renderer': record['renderer'], 'execution_mode': record['execution_mode'],
                   'audio': record['audio'],
                   'launcher_source': plan.get('launcher_source', plan['source']),
                   'policy': 'direct native WW05 execution with private preferences/saves; no OS confinement; every attempt is preserved'}, stream)
        stream.write('\n')
    try:
        verify_inputs(root, plan)
        surface_sync = bool(plan.get('mem1_snapshots', 0))
        if config_identity(root, surface_sync=surface_sync, pref_path=root / FIRMWARE_DIRECTORY) != plan['private_config']:
            raise ValueError('private configuration changed before installation')
        install = plan['phases']['install']
        phase = Path(install['evidence_root'])
        memory_preflight(root, plan, 'install')
        print(f"RUN phase=install evidence={phase}/evidence", flush=True)
        status = run_owned(install['command'], root, install['timeout_seconds'], evidence_root=phase,
                           phase_observer=installer_observer(root, plan), environment=install['environment'], cwd=install['cwd'])
        process = json.loads((phase / 'evidence/process.json').read_text())
        print(f"STOP phase=install reason={process['stop_reason']} seconds={process['duration_seconds']:.2f}", flush=True)
        record.update(install_status=status, install_process=process)
        verify_stopped(process)
        if not ((status == 0 and process['exit'] == 0 and process['stop_reason'] == 'exit') or
                (status == 125 and process['stop_reason'] == 'installed_payload' and
                 process['exit'] in (0, -signal.SIGTERM, -signal.SIGKILL)) or
                (status == 124 and process['stop_reason'] == 'timeout' and
                 process['exit'] in (0, -signal.SIGTERM, -signal.SIGKILL))):
            raise ValueError('unexpected installer disposition')
        canonical = emitted_config(root, phase, surface_sync=surface_sync, pref_path=root / FIRMWARE_DIRECTORY)
        verify_phase_logs(root, phase, PHASE_LOG_LIMIT)
        verify_inputs(root, plan)
        payload = verify_installed(root, plan)
        restore_config(root, canonical, surface_sync=surface_sync, pref_path=root / FIRMWARE_DIRECTORY)
        memory_preflight(root, plan, 'guest')
        frame_window = (plan.get('frame_capture_start', 1), plan.get('frame_capture_limit', MAX_FRAMES))
        frame_window = frame_window if plan.get('live_frame_capture') and frame_window != (1, MAX_FRAMES) else None
        token = fresh_request(root, plan['source'], plan['vpk_sha256'], plan.get('continue_gx', 0),
                              plan.get('mem1_snapshots', 0), plan.get('defer_audio', False),
                              plan.get('mem1_command_window'), frame_window)
        with evidence_file(phase, 'preparation.json') as stream:
            json.dump({'readiness': 'VERIFIED', 'payload': payload, 'guest_token': token, 'audio': record['audio']}, stream, indent=2)
            stream.write('\n')
        guest = plan['phases']['guest']
        phase = Path(guest['evidence_root'])
        with evidence_file(phase, 'request.json') as stream:
            json.dump({'token': token, 'source': plan['source'], 'vpk_sha256': plan['vpk_sha256'],
                        'audio': record['audio'],
                         'mem1_command_window': plan.get('mem1_command_window'),
                        'frame_capture_start': plan.get('frame_capture_start', 1),
                        'frame_capture_limit': plan.get('frame_capture_limit', MAX_FRAMES),
                        'continue_gx': plan.get('continue_gx', 0), 'mem1_snapshots': plan.get('mem1_snapshots', 0)}, stream)
            stream.write('\n')
        print(f"RUN phase=guest evidence={phase}/evidence", flush=True)
        status = run_owned(guest['command'], root, guest['timeout_seconds'], evidence_root=phase,
                            phase_observer=activity_observer(root, plan, phase), environment=guest['environment'], cwd=guest['cwd'])
        process = json.loads((phase / 'evidence/process.json').read_text())
        record.update(process_status=status, process=process)
        with evidence_file(root, 'guest-disposition.json') as stream:
            json.dump({'kind': 'ww05-guest-disposition-v1', 'run': plan['run'], 'source': plan['source'],
                       'launcher_source': record['launcher_source'], 'vpk_sha256': plan['vpk_sha256'],
                       'audio': record['audio'], 'process_status': status, 'process': process,
                       'validation': 'PENDING', 'reported_frontier': 'UNAVAILABLE', 'frontier_verified': False},
                      stream, indent=2)
            stream.write('\n')
        print(f"STOP phase=guest reason={process['stop_reason']} seconds={process['duration_seconds']:.2f}", flush=True)
        verify_stopped(process)
        canonical = emitted_config(root, phase, surface_sync=surface_sync, pref_path=root / FIRMWARE_DIRECTORY)
        verify_phase_logs(root, phase, PHASE_LOG_LIMIT)
        verify_inputs(root, plan)
        verify_installed(root, plan)
        restore_config(root, canonical, surface_sync=surface_sync, pref_path=root / FIRMWARE_DIRECTORY)
        content = read_profile_file(root / REPORT_DIRECTORY / 'ww05.report', 65536, owned=True)
        text = content.decode('utf-8')
        verdict = validate(text, token, plan['source'], plan['vpk_sha256'], plan.get('continue_gx', 0),
                           defer_audio=plan.get('defer_audio', False))
        _, presented = matched(r'END frontier=(PASS|FAIL) live=0 rendered=(\d+)', text.splitlines()[-1])
        record.update(presented=int(presented), rendered=int(presented) > 0)
        if plan.get('continue_gx'):
            journal = read_profile_file(root / REPORT_DIRECTORY / 'ww05.log', 64 * 1024 * 1024, owned=True)
            logged = validate_commands(journal.decode('utf-8'), text, plan['continue_gx'])
            record.update(commands_verified=logged, log_sha256=hashlib.sha256(journal).hexdigest())
        manifest = verify_snapshots(root / REPORT_DIRECTORY, token=token, source=plan['source'],
                                    package=plan['vpk_sha256'], count=plan.get('mem1_snapshots', 0),
                                     audio=record['audio'],
                                     command_window=plan.get('mem1_command_window'),
                                    journal=journal.decode('utf-8') if plan.get('continue_gx') else '')
        publish_replay_manifests(root / REPORT_DIRECTORY, manifest)
        with evidence_file(root, 'mem1-capture.json') as stream:
            json.dump(manifest, stream, indent=2)
            stream.write('\n')
        record.update(memory_captures_verified=manifest['captured'])
        images = collect_frames(root / 'evidence/frames', root / 'evidence/frames',
                                enabled=surface_sync, presented=int(presented), token=token,
                                source=plan['source'], package=plan['vpk_sha256'], audio=record['audio'],
                                start=plan.get('frame_capture_start', 1),
                                limit=plan.get('frame_capture_limit', MAX_FRAMES))
        with evidence_file(root, 'live-images.json') as stream:
            json.dump(images, stream, indent=2)
            stream.write('\n')
        record.update(frame_captures_verified=images['captures'])
        record.update(reported_frontier=verdict, frontier_verified=verdict == 'PASS',
                      report_sha256=hashlib.sha256(content).hexdigest(), report_path=str(REPORT_DIRECTORY / 'ww05.report'),
                       completion='COMPLETE' if process['stop_reason'] == 'exit' else 'INCOMPLETE',
                       status=status or (0 if verdict == 'PASS' else 1))
    except Exception as error:
        expected_report_missing = (isinstance(error, FileNotFoundError) and error.filename == 'ww05.report' and
                                   record.get('process', {}).get('stop_reason') in ('inactive', 'timeout', 'memory-pressure'))
        record.update(error=str(error), traceback=traceback.format_exc(),
                      status=record['process_status'] if expected_report_missing else 2)
    with evidence_file(root, 'result.json') as stream:
        json.dump(record, stream, indent=2)
        stream.write('\n')
    return record['status']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--idle-timeout', type=float, default=30,
                        help='abort only after this many seconds below 5%% CPU and without guest-output growth')
    parser.add_argument('--wayland-socket', type=Path, required=True)
    parser.add_argument('--continue-gx', type=int, default=0,
                        help=f'journal first N real commands, then keep executing (1..{MAX_JOURNAL_COMMANDS})')
    parser.add_argument('--mem1-snapshots', type=int, choices=range(MAX_CAPTURES + 1), default=0,
                         help=f'capture raw 24 MiB MEM1 before logged draws, at most {MAX_CAPTURES}; off for FPS baselines')
    parser.add_argument('--mem1-command-window', type=int, nargs=2, metavar=('START', 'END'),
                         help='snapshot every draw within this inclusive command window; retain all earlier FIFO state')
    parser.add_argument('--frame-window', type=int, nargs=2, metavar=('START', 'END'),
                         help='capture live frame PNGs from present START through present END; requires --mem1-snapshots')
    parser.add_argument('--defer-audio', action='store_true',
                        help='explicit no-op DSP bootstrap with declared synthetic mails/IRQ; reports audio=deferred; no PCM output')
    parser.add_argument('--disc', type=Path, help='link the verified image into the private profile; guest disc I/O is readonly, not OS-confined')
    parser.add_argument('--baseline-build', type=Path,
                        help='explicit preserved ancestor build receipt; guest and current launcher identities stay separate')
    parser.add_argument('--candidate-build', type=Path,
                        help='explicit private candidate descriptor; the receipt, artifacts and PAD state are verified strictly')
    parser.add_argument('--check', action='store_true',
                        help='verify admission read-only; launch nothing and write nothing')
    args = parser.parse_args()
    if args.check:
        return check(args)
    root, plan = stage(args)
    status = execute_run(root, plan)
    print(f'RESULT {root}/evidence/result.json')
    return status


if __name__ == '__main__':
    raise SystemExit(main())
