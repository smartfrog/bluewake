#!/usr/bin/env python3
"""Install and run WW05 once in a new isolated private profile, preserving all evidence."""

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
from stage_plan import stage, validate_continuation, verify_package
from report import matched, validate, validate_commands
from fp_campaign import emitted_config, restore_config, runtime_inputs, verify_phase_logs, verify_stopped
from fp_launch import REPORT_DIRECTORY, evidence_file, run_owned
from fp_libraries import directory_fd, file_identity, library_identity
from fp_profile import config_identity, read_profile_file, verify_firmware


def verify_inputs(root, plan):
    source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if source != plan['source'] or subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True):
        raise ValueError('source checkpoint changed')
    checked_sources()
    if sha256(OUTPUT / 'footprint.json') != plan['build_evidence']['footprint_sha256']:
        raise ValueError('offline footprint evidence changed')
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
        placeholder = root / disc['install_path']
        with directory_fd(placeholder.parent):
            if placeholder.is_symlink() or placeholder.stat().st_size != 0:
                raise ValueError('read-only disc bind placeholder changed')


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


def fresh_request(root, source, package, continue_gx=0):
    validate_continuation(continue_gx)
    directory = root
    for part in REPORT_DIRECTORY.parts:
        with directory_fd(directory) as parent:
            try:
                os.mkdir(part, mode=0o700, dir_fd=parent)
            except FileExistsError:
                pass
        directory = directory / part
    token = uuid.uuid4().hex
    with directory_fd(directory) as parent:
        if os.fstat(parent).st_uid != os.getuid():
            raise ValueError('report directory must be privately owned')
        if set(os.listdir(parent)) & {'ww05' + suffix for suffix in ('.request', '.partial', '.report', '.log', '.dispatch', '.continue')}:
            raise ValueError('preexisting WW05 request/output; refusing to delete or reuse it')
        descriptor = os.open('ww05.request', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                             0o600, dir_fd=parent)
    with os.fdopen(descriptor, 'w') as stream:
        stream.write(f'{token} {source} {package}\n')
    if continue_gx:
        with directory_fd(directory) as parent:
            descriptor = os.open('ww05.continue', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                                 0o600, dir_fd=parent)
        with os.fdopen(descriptor, 'w') as stream:
            stream.write(f'{token} {source} {package} {continue_gx}\n')
    return token


def execute_run(root, plan):
    record = {'run': plan['run'], 'source': plan['source'], 'vpk_sha256': plan['vpk_sha256'],
              'continue_gx': plan.get('continue_gx', 0),
              'completion': 'INCOMPLETE', 'reported_frontier': 'UNAVAILABLE',
              'frontier_verified': False, 'rendered': False}
    with evidence_file(root, 'execution.json') as stream:
        json.dump({'source': plan['source'], 'run': plan['run'], 'real_execution': True,
                   'policy': 'product owner permits iterative isolated WW05 execution; every attempt is preserved'}, stream)
        stream.write('\n')
    try:
        verify_inputs(root, plan)
        if config_identity(root) != plan['private_config']:
            raise ValueError('private configuration changed before installation')
        install = plan['phases']['install']
        phase = Path(install['evidence_root'])
        status = run_owned(install['command'], root, install['timeout_seconds'], evidence_root=phase)
        process = json.loads((phase / 'evidence/process.json').read_text())
        record.update(install_status=status, install_process=process)
        verify_stopped(process)
        if not ((status == 0 and process['exit'] == 0 and process['stop_reason'] == 'exit') or
                (status == 124 and process['stop_reason'] == 'timeout' and
                 process['exit'] in (0, -signal.SIGTERM, -signal.SIGKILL))):
            raise ValueError('unexpected installer disposition')
        canonical = emitted_config(root, phase)
        verify_phase_logs(root, phase)
        verify_inputs(root, plan)
        payload = verify_installed(root, plan)
        restore_config(root, canonical)
        token = fresh_request(root, plan['source'], plan['vpk_sha256'], plan.get('continue_gx', 0))
        with evidence_file(phase, 'preparation.json') as stream:
            json.dump({'readiness': 'VERIFIED', 'payload': payload, 'guest_token': token}, stream, indent=2)
            stream.write('\n')
        guest = plan['phases']['guest']
        phase = Path(guest['evidence_root'])
        with evidence_file(phase, 'request.json') as stream:
            json.dump({'token': token, 'source': plan['source'], 'vpk_sha256': plan['vpk_sha256'],
                       'continue_gx': plan.get('continue_gx', 0)}, stream)
            stream.write('\n')
        status = run_owned(guest['command'], root, guest['timeout_seconds'], evidence_root=phase)
        process = json.loads((phase / 'evidence/process.json').read_text())
        record.update(process_status=status, process=process)
        verify_stopped(process)
        canonical = emitted_config(root, phase)
        verify_phase_logs(root, phase)
        verify_inputs(root, plan)
        verify_installed(root, plan)
        restore_config(root, canonical)
        content = read_profile_file(root / REPORT_DIRECTORY / 'ww05.report', 65536, owned=True)
        text = content.decode('utf-8')
        verdict = validate(text, token, plan['source'], plan['vpk_sha256'], plan.get('continue_gx', 0))
        _, presented = matched(r'END frontier=(PASS|FAIL) live=0 rendered=(\d+)', text.splitlines()[-1])
        record.update(presented=int(presented), rendered=int(presented) > 0)
        if plan.get('continue_gx'):
            journal = read_profile_file(root / REPORT_DIRECTORY / 'ww05.log', 64 * 1024 * 1024, owned=True)
            logged = validate_commands(journal.decode('utf-8'), text, plan['continue_gx'])
            record.update(commands_verified=logged, log_sha256=hashlib.sha256(journal).hexdigest())
        record.update(reported_frontier=verdict, frontier_verified=verdict == 'PASS',
                      report_sha256=hashlib.sha256(content).hexdigest(), report_path=str(REPORT_DIRECTORY / 'ww05.report'),
                       completion='COMPLETE' if process['stop_reason'] == 'exit' else 'INCOMPLETE',
                       status=status or (0 if verdict == 'PASS' else 1))
    except Exception as error:
        record.update(error=str(error), traceback=traceback.format_exc(), status=2)
    with evidence_file(root, 'result.json') as stream:
        json.dump(record, stream, indent=2)
        stream.write('\n')
    return record['status']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--wayland-socket', type=Path, required=True)
    parser.add_argument('--continue-gx', type=int, default=0, help='journal first N real commands, then keep executing (1..256)')
    parser.add_argument('--disc', type=Path, help='read-only bind of the already verified private image at the WW13 install path')
    root, plan = stage(parser.parse_args())
    status = execute_run(root, plan)
    print(f'RESULT {root}/evidence/result.json')
    return status


if __name__ == '__main__':
    raise SystemExit(main())
