#!/usr/bin/env python3
"""Stage a private WW05 profile and exact authorization proposal. No execution entry point."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import uuid
import zipfile

from game_build import OUTPUT, ROOT, STACK, checked_sources, sha256

sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))
from fp_launch import create_run, evidence_file, sandbox_command, stage_runtime
from fp_libraries import directory_fd, library_identity
from fp_profile import config_identity, stage_firmware
from fp_campaign import runtime_inputs

RUNTIME_SHA256 = 'cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30'


def make_plan(root, args, source, package, run):
    if not math.isfinite(args.timeout) or not 0 < args.timeout <= 60:
        raise ValueError('each phase deadline must be finite, positive and at most 60 seconds')
    commands = {'install': ['--console', '--backend-renderer', 'OpenGL', 'ww05.vpk'],
                'guest': ['--backend-renderer', 'OpenGL', '--installed-path', 'BWWG00005']}
    phases = {name: {'command': sandbox_command(root, 'Vita3K', command, args.libraries, args.wayland_socket),
                     'timeout_seconds': args.timeout, 'process_number': index,
                     'evidence_root': str(root / 'phases' / name)}
              for index, (name, command) in enumerate(commands.items(), 1)}
    return {'ticket': 'VITA-WW-05', 'method': 'startup-gx-v1', 'title': 'BWWG00005',
            'run': run, 'source': source, 'vpk_sha256': package,
            'authorization': f'VITA-WW-05:{source}:{run}:startup-gx:1,2',
            'real_execution': False, 'process_budget': 2, 'process_budget_used': 0,
            'retry': 'FORBIDDEN', 'phases': phases,
            'cwd': str(root / 'work'), 'sandbox_cwd': '/stack/vita3k',
            'RLIMIT_FSIZE': resource.getrlimit(resource.RLIMIT_FSIZE),
            'report_poll_seconds': 0.05, 'report_exit_grace_seconds': 0.25,
            'completion_policy': '124/125 remain INCOMPLETE; reported frontier verdict is separate',
            'cleanup': 'owned process group TERM; wait at most 2 seconds, then KILL/reap; private PID namespace',
            'preparation_gate': 'installer fully stopped/reaped; exact owned single-link installed SELF/SFO/DOL bytes; '
                                'no crash/config error; preserve emitted config, accept only canonical or user-id: 00, '
                                'restore canonical before guest; verify all readonly input identities',
            'request_policy': 'only after VERIFIED preparation: exclusive ww05.request with fresh 32-hex token, '
                              'source and package; refuse preexisting request/partial/report/log, never delete',
            'report': 'data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.report',
            'report_validator': str(ROOT / 'scripts/vita/ww05/report.py'),
            'frontier': 'first complete real FIFO command at CC008000..CC00801F; no padding; pinned nonincremental '
                        'parser once; opcode/raw bytes/start and completion PC/SP; edge-service latch; rendered=0',
            'execution_gate': 'coordinator exact authorization plus independently reviewed root-plan SHA256; '
                              'recheck clean source, artifacts, runtime, config, firmware and permanent reservation; '
                              'reject existing phase process evidence before any Popen; no probe/help/trace'}


def verify_package(evidence):
    if (evidence['title'], evidence['method']) != ('BWWG00005', 'startup-gx-v1'):
        raise ValueError('unexpected WW05 build identity')
    paths = {Path(name).name: (Path(name), entry) for name, entry in evidence['artifacts'].items()}
    for name in ('ww05_host', 'ww05_host.velf', 'eboot.bin', 'ww05.vpk_param.sfo', 'ww05.vpk'):
        path, entry = paths[name]
        if path.stat().st_size != entry['bytes'] or sha256(path) != entry['sha256']:
            raise ValueError('WW05 build artifact changed: ' + name)
    dol = OUTPUT / 'private/main.dol'
    expected_dol = json.loads((OUTPUT / 'inputs.json').read_text())['dol_sha256']
    if sha256(dol) != expected_dol:
        raise ValueError('verified DOL changed')
    vpk = paths['ww05.vpk'][0]
    with zipfile.ZipFile(vpk) as archive:
        if sorted(archive.namelist()) != ['eboot.bin', 'main.dol', 'sce_sys/param.sfo'] or archive.testzip() is not None:
            raise ValueError('WW05 private package entries/CRC mismatch')
        for entry, path in (('eboot.bin', paths['eboot.bin'][0]), ('main.dol', dol),
                            ('sce_sys/param.sfo', paths['ww05.vpk_param.sfo'][0])):
            with archive.open(entry) as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if archive.getinfo(entry).file_size != path.stat().st_size or actual != sha256(path):
                raise ValueError('WW05 package payload mismatch: ' + entry)
    return vpk, paths['ww05.vpk'][1]['sha256']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--wayland-socket', type=Path, required=True)
    args = parser.parse_args()
    args.libraries = STACK / 'vita3k-env'
    if (OUTPUT / 'campaign-reservation.json').exists():
        raise ValueError('WW05 campaign already reserved; no second preparation or retry')
    source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True):
        raise ValueError('plan requires an immutable clean delivery checkpoint')
    checked_sources()
    evidence = json.loads((OUTPUT / 'build-evidence.json').read_text())
    if evidence['source'] != source:
        raise ValueError('build is not at the delivery SHA')
    if sha256(OUTPUT / 'footprint.json') != evidence['footprint_sha256']:
        raise ValueError('offline footprint evidence changed')
    if resource.getrlimit(resource.RLIMIT_FSIZE) != (resource.RLIM_INFINITY, resource.RLIM_INFINITY):
        raise ValueError('RLIMIT_FSIZE must remain unlimited for the copied emulator')
    vpk, package = verify_package(evidence)
    run = uuid.uuid4().hex
    root = OUTPUT / ('vita3k-run-' + run)
    create_run(root)
    assets = stage_runtime(root, STACK / 'vita3k/Vita3K', RUNTIME_SHA256)
    shutil.copyfile(vpk, root / 'runtime/ww05.vpk')
    if sha256(root / 'runtime/ww05.vpk') != package:
        raise ValueError('staged private package changed')
    firmware = stage_firmware(root, STACK / 'firmware')
    libraries = library_identity(args.libraries)
    plan = make_plan(root, args, source, package, run)
    plan.update(runtime_sha256=RUNTIME_SHA256,
                runtime_source='observed-banner:bbd5c36;local-RPATH-relocation;mapping:UNVERIFIED',
                runtime_assets=assets, runtime_input=str(STACK / 'vita3k/Vita3K'),
                runtime_inputs=runtime_inputs(STACK / 'vita3k/Vita3K'),
                staged_runtime=library_identity(root / 'runtime', allow_symlinks=False),
                firmware=firmware, private_config=config_identity(root), runtime_libraries=libraries,
                source_vpk=str(vpk), build_evidence_sha256=sha256(OUTPUT / 'build-evidence.json'),
                footprint=json.loads((OUTPUT / 'footprint.json').read_text()))
    (root / 'phases').mkdir(mode=0o700)
    for name, entry in plan['phases'].items():
        phase = root / 'phases' / name
        phase.mkdir(mode=0o700)
        (phase / 'evidence').mkdir(mode=0o700)
        with evidence_file(phase, 'plan.json') as stream:
            json.dump(entry, stream, indent=2)
            stream.write('\n')
    with evidence_file(root, 'plan.json') as stream:
        json.dump(plan, stream, indent=2)
        stream.write('\n')
    with directory_fd(OUTPUT) as directory:
        fd = os.open('campaign-reservation.json', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=directory)
    with os.fdopen(fd, 'w') as stream:
        json.dump({'source': source, 'run': run, 'authorization': plan['authorization'],
                   'process_numbers': [1, 2], 'retry': 'FORBIDDEN',
                   'plan_sha256': sha256(root / 'evidence/plan.json')}, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    print(f'PLAN {root}/evidence/plan.json\nSHA256 {sha256(root / "evidence/plan.json")}')
    print('STAGED ONLY — zero Vita3K processes; no guest request; STOP pending coordinator authorization')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
