#!/usr/bin/env python3
"""Stage a fresh private WW05 profile and record direct native commands without launching."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import uuid
import zipfile

from game_build import OUTPUT, ROOT, STACK, PINS, COMPOSITE_DIGEST, checked_sources, sha256
from report import MAX_JOURNAL_COMMANDS, MAX_JOURNAL_BYTES
from memory_capture import MAX_CAPTURES, validate_frame_window, validate_memory_captures

sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))
from fp_launch import create_run, evidence_file, reject_symlinks, stage_runtime
from fp_libraries import file_identity, library_identity
from fp_profile import FIRMWARE_DIRECTORY, config_identity, stage_firmware, read_profile_file
from fp_campaign import runtime_inputs
from activity import CpuIdleWindow, MIN_EXTENDED_AVAILABLE_MEMORY_BYTES
from direct_launch import RENDERER, direct_phases

RUNTIME_SHA256 = 'cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30'
SHADER_COMPILER = Path('/home/fred/cloudpunk-vita/tools/vita3k/data/Vita3K/Vita3K/ur0/data/libshacccg.suprx')


def stage_shader_compiler(root, source):
    source = reject_symlinks(source)
    identity = file_identity(source)
    target = root / 'data/Vita3K/Vita3K/ur0/data/libshacccg.suprx'
    target.parent.mkdir(parents=True, mode=0o700)
    with source.open('rb') as stream, target.open('xb') as destination:
        shutil.copyfileobj(stream, destination)
    staged = file_identity(target)
    if staged['sha256'] != identity['sha256'] or staged['bytes'] != identity['bytes']:
        raise ValueError('staged shader compiler mismatch')
    return {'source': str(source), 'identity': identity, 'staged': staged,
            'install_path': str(target.relative_to(root))}


def validate_timeout(timeout, *, scene_capture=False, idle_timeout=30):
    maximum = 1800 if scene_capture else 600
    if not math.isfinite(timeout) or not 0 < timeout <= maximum:
        raise ValueError(f'each phase deadline must be finite, positive and at most {maximum} seconds')
    if timeout > 600 and idle_timeout > 30:
        raise ValueError('extended scene capture requires the CPU/output watchdog within 30 seconds')


def validate_continuation(limit):
    if type(limit) is not int or not 0 <= limit <= MAX_JOURNAL_COMMANDS:
        raise ValueError(f'continuation journal limit must be 1..{MAX_JOURNAL_COMMANDS}, or zero for the default frontier')


def stage_disc(root, path, expected_sha256):
    path = reject_symlinks(path)
    identity = file_identity(path)
    if identity['sha256'] != expected_sha256:
        raise ValueError('optional disc does not match the verified WW05 input image')
    destination = root / 'data/Vita3K/Vita3K/ux0/data/bluewake/GZLE01.iso'
    destination.parent.mkdir(parents=True, mode=0o700)
    destination.symlink_to(path)
    return {'path': str(path), 'identity': identity, 'install_path': str(destination.relative_to(root)),
            'transport': 'private profile symlink to verified disc; guest I/O opens readonly; no OS write confinement'}


def make_plan(root, args, source, package, run):
    if getattr(args, 'render_node', None) is not None:
        raise ValueError('direct native WW launch uses host GPU selection, not a render-node sandbox guard')
    renderer = dict(RENDERER)
    limit = getattr(args, 'continue_gx', 0)
    memory_captures = getattr(args, 'mem1_snapshots', 0)
    command_window = getattr(args, 'mem1_command_window', None)
    frame_window = getattr(args, 'frame_window', None)
    defer_audio = getattr(args, 'defer_audio', False)
    if type(defer_audio) is not bool or (defer_audio and not limit):
        raise ValueError('audio deferral requires an explicit boolean and the continuation journal')
    idle_timeout = getattr(args, 'idle_timeout', 30)
    CpuIdleWindow(idle_timeout, 0.05)
    validate_continuation(limit)
    validate_memory_captures(memory_captures, limit, command_window)
    validate_frame_window(frame_window, memory_captures)
    validate_timeout(args.timeout, scene_capture=command_window is not None, idle_timeout=idle_timeout)
    extended = args.timeout > 600
    phases = direct_phases(root, args.timeout, args.libraries, args.wayland_socket)
    plan = {'ticket': 'VITA-WW-05', 'method': 'startup-gx-v1', 'title': 'BWWG00005',
            'renderer': renderer, 'execution_mode': 'direct-native',
            'continue_gx': limit, 'mem1_snapshots': memory_captures,
            'mem1_command_window': command_window, 'journal_byte_limit': MAX_JOURNAL_BYTES,
            'defer_audio': defer_audio, 'audio': 'deferred' if defer_audio else 'strict',
            'live_frame_capture': bool(memory_captures), 'frame_capture_start': frame_window[0] if frame_window else 1,
            'frame_capture_limit': frame_window[1] if frame_window else 128,
            'idle_timeout_seconds': idle_timeout, 'cpu_threshold': 0.05,
            'extended_scene_capture': extended,
            'minimum_available_memory_bytes': MIN_EXTENDED_AVAILABLE_MEMORY_BYTES if extended else 0,
            'memory_pressure_policy': 'extended window only; require 2.5 GiB MemAvailable before each launch; '
                                      'TERM/reap owned process on live pressure; never SIGSTOP or retain a resident paused guest',
            'activity_policy': 'CPU >= 5% OR growing process/cache log, guest journal/report/dispatch, or installed payload; '
                               'exclude observer-written monitoring evidence; only silence AND low CPU can be inactive',
            'continuation_policy': 'opt-in; journal first N commands; no guest execution cap; '
                                    'preserve first frontier; stop on unsupported behavior or owned phase deadline',
            'memory_window_policy': 'explicit opt-in diagnostic stop after the actual completed end command; '
                                    'snapshot every selected draw before renderer write; no guest time or completion injection',
            'run': run, 'source': source, 'vpk_sha256': package,
            'real_execution': False, 'retry': 'fresh private profile for every attempt', 'phases': phases,
            'cwd': str(root / 'runtime'),
            'RLIMIT_FSIZE': resource.getrlimit(resource.RLIMIT_FSIZE),
            'completion_policy': '124/125 remain INCOMPLETE; reported frontier verdict is separate',
            'cleanup': 'owned process group TERM; wait at most 2 seconds, then KILL/reap',
            'isolation': 'fresh private HOME/XDG preferences and saves; no sandbox or OS filesystem confinement',
            'preparation_gate': 'installer fully stopped/reaped; exact owned single-link installed SELF/SFO/DOL bytes; '
                                'no crash/config error; preserve emitted config, accept only canonical or user-id: 00, '
                                'restore canonical before guest; verify all readonly input identities',
            'request_policy': 'only after VERIFIED preparation: exclusive ww05.request with fresh 32-hex token, '
                              'source and package; refuse preexisting request/partial/report/log, never delete',
            'report': 'data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.report',
            'report_validator': str(ROOT / 'scripts/vita/ww05/report.py'),
            'frontier': 'first complete real FIFO command at CC008000..CC00801F; no padding; pinned nonincremental '
                        'parser once; opcode/raw bytes/start and completion PC/SP; edge-service latch; rendered=0',
            'input_policy': 'recheck clean source, artifacts, runtime, config and firmware before launch; '
                            'reject existing phase process evidence; preserve every attempt'}
    return plan


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


def checked_build_receipt(path, launcher_source, baseline=False):
    path = reject_symlinks(path)
    evidence = json.loads(read_profile_file(path, 1024 * 1024, owned=True))
    source = evidence['source']
    if not isinstance(source, str) or len(source) != 40 or any(c not in '0123456789abcdef' for c in source):
        raise ValueError('build source must be a full commit identity')
    if evidence['pins'] != PINS:
        raise ValueError('baseline build pins differ from the verified runtime')
    if evidence['composite_digest'] != COMPOSITE_DIGEST:
        raise ValueError('baseline composite identity differs')
    snapshot = reject_symlinks(Path(evidence['snapshot']))
    if snapshot.parent != OUTPUT or not snapshot.name.startswith('artifacts-'):
        raise ValueError('build snapshot must stay in the owned artifact directory')
    if baseline:
        if path != snapshot / 'build-evidence.json':
            raise ValueError('baseline receipt must be the preserved snapshot receipt')
        ancestry = subprocess.run(['git', 'merge-base', '--is-ancestor', source, launcher_source],
                                  cwd=ROOT, capture_output=True, text=True)
        if ancestry.returncode == 1:
            raise ValueError('baseline source is not an ancestor of the launcher')
        if ancestry.returncode != 0:
            raise RuntimeError('cannot verify baseline ancestry: ' + ancestry.stderr)
    elif source != launcher_source:
        raise ValueError('build is not at the delivery SHA')
    footprint = snapshot / 'footprint.json'
    if sha256(reject_symlinks(footprint)) != evidence['footprint_sha256']:
        raise ValueError('snapshot footprint evidence changed')
    if not baseline and sha256(OUTPUT / 'footprint.json') != evidence['footprint_sha256']:
        raise ValueError('offline footprint evidence changed')
    return evidence


CANDIDATE_DESCRIPTOR_KIND = 'ww05-private-candidate-v1'
CANDIDATE_RECEIPT_STATUS = 'PASS-private-offline-build-inspection'


def is_full_hex_identity(value, length):
    return isinstance(value, str) and len(value) == length and all(c in '0123456789abcdef' for c in value)


def load_candidate_descriptor(path):
    path = reject_symlinks(path)
    descriptor = json.loads(read_profile_file(path, 64 * 1024, owned=True))
    if not isinstance(descriptor, dict):
        raise ValueError('candidate descriptor must be a JSON object')
    if descriptor.get('kind') != CANDIDATE_DESCRIPTOR_KIND:
        raise ValueError('unknown candidate descriptor schema')
    fields = {'kind', 'receipt', 'source', 'composite_digest', 'vpk_sha256', 'eboot_sha256', 'pad_input'}
    if set(descriptor) != fields:
        raise ValueError('candidate descriptor fields differ from the reviewed schema')
    if not isinstance(descriptor['receipt'], str) or not Path(descriptor['receipt']).is_absolute():
        raise ValueError('candidate descriptor receipt must be an absolute path')
    if not is_full_hex_identity(descriptor['source'], 40):
        raise ValueError('candidate descriptor source must be a full commit identity')
    for field in ('composite_digest', 'vpk_sha256', 'eboot_sha256'):
        if not is_full_hex_identity(descriptor[field], 64):
            raise ValueError('candidate descriptor ' + field + ' must be a full SHA-256 identity')
    if type(descriptor['pad_input']) is not bool:
        raise ValueError('candidate descriptor pad_input must be a boolean')
    return descriptor


def checked_candidate_receipt(descriptor, launcher_source):
    path = reject_symlinks(Path(descriptor['receipt']))
    evidence = json.loads(read_profile_file(path, 1024 * 1024, owned=True))
    if not isinstance(evidence, dict):
        raise ValueError('candidate receipt must be a JSON object')
    if evidence.get('status') != CANDIDATE_RECEIPT_STATUS:
        raise ValueError('unknown candidate receipt schema')
    source = evidence.get('source')
    if not is_full_hex_identity(source, 40):
        raise ValueError('candidate receipt source must be a full commit identity')
    if source != descriptor['source']:
        raise ValueError('candidate receipt source differs from the descriptor')
    if evidence.get('pins') != PINS:
        raise ValueError('candidate receipt pins differ from the verified runtime')
    if evidence.get('composite_digest') != descriptor['composite_digest']:
        raise ValueError('candidate receipt composite identity differs from the descriptor')
    if evidence.get('current_composite_digest') != descriptor['composite_digest']:
        raise ValueError('candidate receipt current composite identity differs from the descriptor')
    config = evidence.get('config')
    if not isinstance(config, dict):
        raise ValueError('candidate receipt config is missing')
    pad_input = config.get('pad_input')
    if not isinstance(pad_input, dict) or pad_input.get('enabled') is not descriptor['pad_input']:
        raise ValueError('candidate receipt PAD state differs from the descriptor')
    ancestry = subprocess.run(['git', 'merge-base', '--is-ancestor', source, launcher_source],
                              cwd=ROOT, capture_output=True, text=True)
    if ancestry.returncode == 1:
        raise ValueError('candidate source is not an ancestor of the launcher')
    if ancestry.returncode != 0:
        raise RuntimeError('cannot verify candidate ancestry: ' + ancestry.stderr)
    return evidence


def verify_candidate_package(descriptor, evidence):
    artifacts = evidence.get('artifacts')
    if not isinstance(artifacts, dict) or not {'ww05.vpk', 'eboot.bin'} <= set(artifacts):
        raise ValueError('candidate receipt artifacts are missing')
    for name, entry in artifacts.items():
        if (not isinstance(entry, dict) or not isinstance(entry.get('path'), str) or
                not Path(entry['path']).is_absolute()):
            raise ValueError('candidate receipt artifact path is invalid: ' + name)
        if type(entry.get('bytes')) is not int or not is_full_hex_identity(entry.get('sha256'), 64):
            raise ValueError('candidate receipt artifact identity is invalid: ' + name)
        identity = file_identity(reject_symlinks(Path(entry['path'])))
        if identity['bytes'] != entry['bytes'] or identity['sha256'] != entry['sha256']:
            raise ValueError('candidate artifact changed: ' + name)
    if artifacts['ww05.vpk']['sha256'] != descriptor['vpk_sha256']:
        raise ValueError('candidate VPK does not match the descriptor')
    if artifacts['eboot.bin']['sha256'] != descriptor['eboot_sha256']:
        raise ValueError('candidate eboot does not match the descriptor')
    members = evidence.get('vpk_members')
    if not isinstance(members, dict) or sorted(members) != ['eboot.bin', 'main.dol', 'sce_sys/param.sfo']:
        raise ValueError('candidate receipt VPK members are missing')
    for name, entry in members.items():
        if type(entry.get('bytes')) is not int or not is_full_hex_identity(entry.get('sha256'), 64):
            raise ValueError('candidate receipt VPK member identity is invalid: ' + name)
    vpk = Path(artifacts['ww05.vpk']['path'])
    with zipfile.ZipFile(vpk) as archive:
        if sorted(archive.namelist()) != ['eboot.bin', 'main.dol', 'sce_sys/param.sfo'] or archive.testzip() is not None:
            raise ValueError('candidate package entries/CRC mismatch')
        for name, entry in members.items():
            info = archive.getinfo(name)
            with archive.open(name) as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if info.file_size != entry['bytes'] or actual != entry['sha256']:
                raise ValueError('candidate package payload mismatch: ' + name)
    if any(members['eboot.bin'][field] != artifacts['eboot.bin'][field] for field in ('bytes', 'sha256')):
        raise ValueError('candidate package eboot identity differs from the receipt artifact')
    return vpk, artifacts['ww05.vpk']['sha256']


def candidate_metadata(descriptor, path, evidence, package):
    return {'descriptor': str(path), 'descriptor_sha256': sha256(path),
            'receipt': str(Path(descriptor['receipt'])), 'receipt_sha256': sha256(Path(descriptor['receipt'])),
            'source': evidence['source'], 'composite_digest': evidence['composite_digest'],
            'vpk_sha256': package, 'eboot_sha256': evidence['artifacts']['eboot.bin']['sha256'],
            'pad_input': descriptor['pad_input']}


def stage(args):
    if getattr(args, 'render_node', None) is not None:
        raise ValueError('direct native WW launch does not accept a render-node sandbox guard')
    if getattr(args, 'candidate_build', None) is not None and getattr(args, 'baseline_build', None) is not None:
        raise ValueError('candidate and baseline build selection are mutually exclusive')
    CpuIdleWindow(getattr(args, 'idle_timeout', 30), 0.05)
    validate_continuation(getattr(args, 'continue_gx', 0))
    validate_memory_captures(getattr(args, 'mem1_snapshots', 0), getattr(args, 'continue_gx', 0),
                             getattr(args, 'mem1_command_window', None))
    validate_timeout(args.timeout, scene_capture=getattr(args, 'mem1_command_window', None) is not None,
                     idle_timeout=getattr(args, 'idle_timeout', 30))
    args.libraries = STACK / 'vita3k-env'
    launcher_source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True):
        raise ValueError('plan requires an immutable clean delivery checkpoint')
    checked_sources()
    baseline_path = getattr(args, 'baseline_build', None)
    candidate_path = getattr(args, 'candidate_build', None)
    candidate = None
    footprint_path = None
    if candidate_path is not None:
        candidate_path = reject_symlinks(candidate_path)
        descriptor = load_candidate_descriptor(candidate_path)
        evidence = checked_candidate_receipt(descriptor, launcher_source)
        receipt_path = Path(descriptor['receipt'])
    else:
        receipt_path = Path(baseline_path) if baseline_path is not None else OUTPUT / 'build-evidence.json'
        evidence = checked_build_receipt(receipt_path, launcher_source, baseline=baseline_path is not None)
        footprint_path = Path(evidence['snapshot']) / 'footprint.json'
    source = evidence['source']
    if resource.getrlimit(resource.RLIMIT_FSIZE) != (resource.RLIM_INFINITY, resource.RLIM_INFINITY):
        raise ValueError('RLIMIT_FSIZE must remain unlimited for the copied emulator')
    if candidate_path is not None:
        vpk, package = verify_candidate_package(descriptor, evidence)
        candidate = candidate_metadata(descriptor, candidate_path, evidence, package)
    else:
        vpk, package = verify_package(evidence)
    run = uuid.uuid4().hex
    root = OUTPUT / ('vita3k-run-' + run)
    create_run(root)
    assets = stage_runtime(root, STACK / 'vita3k/Vita3K', RUNTIME_SHA256)
    shutil.copyfile(vpk, root / 'runtime/ww05.vpk')
    if sha256(root / 'runtime/ww05.vpk') != package:
        raise ValueError('staged private package changed')
    firmware = stage_firmware(root, STACK / 'firmware', surface_sync=bool(getattr(args, 'mem1_snapshots', 0)),
                              pref_path=root / FIRMWARE_DIRECTORY)
    shader_compiler = stage_shader_compiler(root, SHADER_COMPILER)
    libraries = library_identity(args.libraries)
    disc = None
    if getattr(args, 'disc', None) is not None:
        disc = stage_disc(root, args.disc, json.loads((OUTPUT / 'inputs.json').read_text())['disc_sha256'])
        args.disc = Path(disc['path'])
    plan = make_plan(root, args, source, package, run)
    plan.update(runtime_sha256=RUNTIME_SHA256, shader_compiler=shader_compiler,
                runtime_source='observed-banner:bbd5c36;local-RPATH-relocation;mapping:UNVERIFIED',
                runtime_assets=assets, runtime_input=str(STACK / 'vita3k/Vita3K'),
                runtime_inputs=runtime_inputs(STACK / 'vita3k/Vita3K'),
                staged_runtime=library_identity(root / 'runtime', allow_symlinks=False),
                 firmware=firmware, private_config=config_identity(root, surface_sync=plan['live_frame_capture'],
                                                                  pref_path=root / FIRMWARE_DIRECTORY), runtime_libraries=libraries,
                 wayland_socket=str(args.wayland_socket) if args.wayland_socket is not None else None, build_evidence=evidence,
                source_vpk=str(vpk), build_evidence_sha256=sha256(receipt_path),
                build_evidence_path=str(receipt_path), baseline_build=baseline_path is not None,
                candidate_build=candidate is not None, candidate=candidate,
                launcher_source=launcher_source, footprint_path=str(footprint_path) if footprint_path is not None else None,
                footprint=json.loads(footprint_path.read_text()) if footprint_path is not None else None)
    plan['disc'] = disc
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
    print(f'PLAN {root}/evidence/plan.json\nSHA256 {sha256(root / "evidence/plan.json")}', flush=True)
    return root, plan


def check(args):
    if getattr(args, 'render_node', None) is not None:
        raise ValueError('direct native WW launch does not accept a render-node sandbox guard')
    if getattr(args, 'candidate_build', None) is not None and getattr(args, 'baseline_build', None) is not None:
        raise ValueError('candidate and baseline build selection are mutually exclusive')
    CpuIdleWindow(getattr(args, 'idle_timeout', 30), 0.05)
    validate_continuation(getattr(args, 'continue_gx', 0))
    validate_memory_captures(getattr(args, 'mem1_snapshots', 0), getattr(args, 'continue_gx', 0),
                             getattr(args, 'mem1_command_window', None))
    validate_timeout(args.timeout, scene_capture=getattr(args, 'mem1_command_window', None) is not None,
                     idle_timeout=getattr(args, 'idle_timeout', 30))
    launcher_source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True):
        raise ValueError('check requires an immutable clean delivery checkpoint')
    checked_sources()
    baseline_path = getattr(args, 'baseline_build', None)
    candidate_path = getattr(args, 'candidate_build', None)
    candidate = None
    if candidate_path is not None:
        candidate_path = reject_symlinks(candidate_path)
        descriptor = load_candidate_descriptor(candidate_path)
        evidence = checked_candidate_receipt(descriptor, launcher_source)
        vpk, package = verify_candidate_package(descriptor, evidence)
        candidate = candidate_metadata(descriptor, candidate_path, evidence, package)
        receipt_path = Path(descriptor['receipt'])
    else:
        receipt_path = Path(baseline_path) if baseline_path is not None else OUTPUT / 'build-evidence.json'
        evidence = checked_build_receipt(receipt_path, launcher_source, baseline=baseline_path is not None)
        vpk, package = verify_package(evidence)
    print('CHECK ' + json.dumps({'kind': 'ww05-admission-check-v1', 'launcher_source': launcher_source,
                                 'guest_source': evidence['source'], 'receipt': str(receipt_path),
                                 'receipt_sha256': sha256(receipt_path), 'source_vpk': str(vpk),
                                 'vpk_sha256': package, 'baseline_build': baseline_path is not None,
                                 'candidate': candidate}, indent=2), flush=True)
    print('CHECK ONLY — nothing written; zero Vita3K processes', flush=True)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--wayland-socket', type=Path, required=True)
    parser.add_argument('--continue-gx', type=int, default=0)
    parser.add_argument('--mem1-snapshots', type=int, choices=range(MAX_CAPTURES + 1), default=0)
    parser.add_argument('--mem1-command-window', type=int, nargs=2, metavar=('START', 'END'))
    parser.add_argument('--frame-window', type=int, nargs=2, metavar=('START', 'END'),
                         help='capture live frame PNGs from present START through present END; requires --mem1-snapshots')
    parser.add_argument('--defer-audio', action='store_true')
    parser.add_argument('--disc', type=Path)
    parser.add_argument('--candidate-build', type=Path,
                        help='explicit private candidate descriptor; the receipt, artifacts and PAD state are verified strictly')
    parser.add_argument('--check', action='store_true',
                        help='verify admission read-only; launch nothing and write nothing')
    args = parser.parse_args()
    if args.check:
        return check(args)
    stage(args)
    print('STAGED ONLY — zero Vita3K processes; no guest request')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
