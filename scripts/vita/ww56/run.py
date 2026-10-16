#!/usr/bin/env python3
"""WW56 bounded normal-boot trial of the current private candidate.

One bounded trial of the WW54 current-source candidate in a fresh private
profile: installer and guest run as separate transient user systemd units with
static MemoryMax/MemorySwapMax budgets, the supervisor runs outside any capped
scope, every phase proves its effective kernel limits before the guest command
is released, and two event-driven one-shot snapshots capture memory at the
GPU-context and first-present frontiers.

The candidate VPK path and SHA-256, the guest build source and the verified
disc are supplied by the coordinator at run time; nothing about the candidate
is hardcoded here. The trial stops early at the first useful frontier and
always records INCOMPLETE honestly. It claims no title, menu, pixels,
gameplay or FPS result.

The real trial runs only after independent review and coordinator go-ahead. Use
absolute paths: a transient service starts in the user manager's home, not the
caller's directory, and `--unit` together with `--scope` is broken on this
host's systemd 259.9. WW50's supervisor used the same service form:

    systemd-run --user --unit=ww56-supervisor.service --slice=app.slice --wait --pipe -- \
        /usr/bin/python3 -B <worktree>/scripts/vita/ww56/run.py --check ...
    systemd-run --user --unit=ww56-supervisor.service --slice=app.slice --wait --pipe -- \
        /usr/bin/python3 -B <worktree>/scripts/vita/ww56/run.py ...
"""

import argparse
import json
import math
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time
import traceback
import zipfile

SCRIPT_DIRECTORY = Path(__file__).resolve().parent
ROOT = SCRIPT_DIRECTORY.parents[2]

for part in ('armv7', 'ww05', 'perf'):
    sys.path.insert(0, str(ROOT / 'scripts/vita' / part))
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from activity import ProcessIdleObserver  # noqa: E402
from direct_launch import direct_phases  # noqa: E402
from fp_campaign import (emitted_config, restore_config, runtime_inputs,  # noqa: E402
                         verify_phase_logs)
from fp_libraries import file_identity, library_identity  # noqa: E402
from fp_launch import (REPORT_DIRECTORY, create_run, evidence_file,  # noqa: E402
                       reject_symlinks, run_owned, stage_runtime)
from fp_profile import FIRMWARE_DIRECTORY, config_identity, stage_firmware, verify_firmware  # noqa: E402
from frame_events import parse_frame_events  # noqa: E402
from game_build import STACK, checked_sources, sha256  # noqa: E402
from launch import PHASE_LOG_LIMIT, fresh_request, verify_installed  # noqa: E402
from stage_plan import (RUNTIME_SHA256, SHADER_COMPILER, stage_disc,  # noqa: E402
                        stage_shader_compiler, validate_continuation)

from budget_properties import (PROPERTIES, parse_properties,  # noqa: E402
                               verify_budget_properties, verify_cgroup_budget)
from headroom import (DEFAULT_GUEST_BUDGET_BYTES, DEFAULT_GUEST_HEADROOM_BYTES,  # noqa: E402
                      DEFAULT_INSTALL_BUDGET_BYTES, DEFAULT_INSTALL_HEADROOM_BYTES,
                      headroom_decision, parse_mem_available)
from journal import IncrementalLog, count_marker_lines  # noqa: E402
from native_context import NativeContextWindow  # noqa: E402
from native_artifacts import verify_receipt as verify_native_artifacts  # noqa: E402
from native_trace import decode as decode_native_trace  # noqa: E402
from snapshot import capture_snapshot  # noqa: E402
from stop_rules import StopState, evaluate_stop, is_failure  # noqa: E402
from trigger_events import PRESENT_MARKER, OneShotTriggerScanner  # noqa: E402
from diagnostic_readiness import diagnostic_readiness  # noqa: E402

INSTALL_DEADLINE_SECONDS = 60
PERMIT_TIMEOUT_SECONDS = 15


class PreconditionBlocked(RuntimeError):
    """The run must not proceed; the message names the missing precondition."""


def write(receipts, name, data):
    with (receipts / name).open('x') as stream:
        json.dump(data, stream, indent=2)
        stream.write('\n')


def git_head(path):
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=path, text=True).strip()


def git_clean(path):
    return not subprocess.check_output(['git', 'status', '--porcelain'], cwd=path, text=True)


def pinned_sources():
    try:
        return checked_sources()
    except (subprocess.CalledProcessError, ValueError, FileNotFoundError) as error:
        raise ValueError('verified pinned checkouts are required under ' + str(ROOT / 'ref/vita-ww-01') +
                         ' (see the WW56 runbook preconditions): ' + str(error)) from error


def properties(unit, keys=PROPERTIES):
    output = subprocess.check_output(['systemctl', '--user', 'show', unit,
                                      *['--property=' + key for key in keys]], text=True)
    return parse_properties(output)


def phase_properties(unit, diagnostic=False):
    if diagnostic:
        return properties(unit, keys=tuple(key for key in PROPERTIES if key != 'MemoryPeak'))
    return properties(unit)


INVOCATION_LINE = re.compile(r'Running as unit: (\S+); invocation ID: ([0-9a-f]+)')


def wrapper_invocation(phase_root, unit):
    if phase_root is None:
        return None
    path = Path(phase_root) / 'evidence/process.log'
    try:
        head = path.read_bytes()[:4096].decode('utf-8', errors='replace')
    except FileNotFoundError:
        return None
    for match in INVOCATION_LINE.finditer(head):
        if match.group(1) == unit:
            return match.group(2)
    return None


def require_fresh_unit(unit, diagnostic=False):
    current = phase_properties(unit, diagnostic)
    if current.get('LoadState') != 'not-found':
        raise ValueError('unit already exists; refusing to reuse or stop it: ' + unit +
                         ' (LoadState=' + str(current.get('LoadState')) + ')')
    return current


def stop_owned_unit(unit, state):
    current = phase_properties(unit, state.get('diagnostic', False))
    recorded = state.get('invocation_id') or wrapper_invocation(state.get('phase_root'), unit)
    loaded_active = (current.get('LoadState') == 'loaded' and
                     current.get('ActiveState') in ('active', 'activating', 'reloading'))
    if current.get('ControlGroup') or loaded_active:
        if recorded is None or current.get('InvocationID') != recorded:
            state['foreign_unit_untouched'] = {'unit': unit, 'recorded_invocation': recorded,
                                               'current': current}
            raise ValueError('refusing to stop ' + unit + ': its invocation is not the one this run created')
        subprocess.run(['systemctl', '--user', 'stop', unit], check=True,
                       timeout=state.get('cleanup_stop_timeout_seconds', 30))
    return current


def cleanup_phase_unit(unit, state):
    state['properties_before_cleanup'] = stop_owned_unit(unit, state)
    return state['properties_before_cleanup']


def verify_uncapped_supervisor(unit):
    relative = Path('/proc/self/cgroup').read_text().strip().split('::', 1)[1]
    if not relative.endswith('/app.slice/' + unit):
        raise ValueError(
            'WW56 supervisor must run in app.slice/' + unit + '; observed ' + relative +
            '; launch it with: systemd-run --user --unit=' + unit + ' --slice=app.slice --wait --pipe -- ' +
            '/usr/bin/python3 -B ' + str(SCRIPT_DIRECTORY / 'run.py') + ' ...')
    current = Path('/sys/fs/cgroup' + relative)
    ancestors = {}
    while current != Path('/sys/fs/cgroup'):
        value = (current / 'memory.max').read_text().strip()
        ancestors[str(current)] = value
        if value != 'max':
            raise ValueError('supervisor ancestor is capped: ' + str(current) + ' memory.max=' + value)
        current = current.parent
    return {'pid': os.getpid(), 'cgroup': relative, 'ancestor_memory_max': ancestors}


def socket_identity(path):
    path = Path(path)
    info = path.stat()
    if not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid():
        raise ValueError('display must be an existing Unix socket owned by the current user: ' + str(path))
    return {'path': str(path), 'uid': info.st_uid}


def record_identities(args):
    sources = pinned_sources()
    return {
        'launcher': {'path': str(ROOT), 'head': git_head(ROOT), 'clean': git_clean(ROOT)},
        'pins': {name: {'path': str(path), 'head': git_head(path), 'clean': git_clean(path)}
                 for name, path in sources.items()},
        'vpk': {'path': str(args.vpk), 'identity': file_identity(args.vpk)},
        'runtime': {'path': str(args.runtime), 'identity': file_identity(args.runtime)},
        'runtime_libraries': library_identity(args.libraries),
        'shader_compiler': {'path': str(args.shader_compiler), 'identity': file_identity(args.shader_compiler)},
        'disc': {'path': str(args.disc), 'identity': file_identity(args.disc)},
        'firmware': {name: library_identity(args.firmware / name, allow_symlinks=False)
                     for name in ('vs0', 'sa0')},
    }


def check_supervisor(args):
    return verify_uncapped_supervisor(args.supervisor_unit)


def check_launcher(args):
    head, clean = git_head(ROOT), git_clean(ROOT)
    if head != args.expected_head or not clean:
        raise ValueError('launcher worktree is not clean at the reviewed head ' + args.expected_head +
                         '; head=' + head + ' clean=' + str(clean))
    return {'head': head, 'clean': clean}


def check_inputs(args):
    if getattr(args, 'native_context_artifacts', None) is not None:
        receipt = verify_native_artifacts(args.native_context_artifacts, args.guest_source)
        if receipt['artifacts']['ww05.vpk']['identity']['sha256'] != args.vpk_sha256:
            raise ValueError('candidate VPK differs from preserved observation artifacts')
    identities = {'vpk': file_identity(args.vpk), 'runtime': file_identity(args.runtime),
                  'disc': file_identity(args.disc)}
    for label, identity, expected in (('vpk', identities['vpk'], args.vpk_sha256),
                                      ('runtime', identities['runtime'], args.runtime_sha256),
                                      ('disc', identities['disc'], args.disc_sha256)):
        if identity['sha256'] != expected:
            raise ValueError(label + ' does not match its supplied SHA-256')
    return {'vpk': identities['vpk'], 'runtime': identities['runtime'],
            'runtime_libraries': library_identity(args.libraries),
            'shader_compiler': file_identity(args.shader_compiler),
            'disc': identities['disc'],
            'firmware': {name: library_identity(args.firmware / name, allow_symlinks=False)
                         for name in ('vs0', 'sa0')},
            'wayland_socket': socket_identity(args.wayland_socket),
            'pins': {name: {'head': git_head(path), 'clean': git_clean(path)}
                     for name, path in pinned_sources().items()}}


def check_freshness(args):
    runroot = Path(args.run_root_base) / ('ww56-' + args.run_id)
    receipts = Path(args.receipts_root) / ('ww56-' + args.run_id)
    for label, path in (('runroot', runroot), ('receipts', receipts)):
        if path.exists() or path.is_symlink():
            raise ValueError(label + ' already exists; a fresh run requires fresh paths: ' + str(path))
    return {'runroot': str(runroot), 'receipts': str(receipts), 'fresh': True}


def check_mode(args):
    diagnostic = getattr(args, 'diagnostic_profile', None) is not None
    plan = None
    if diagnostic:
        try:
            plan = diagnostic_readiness(args)
        except (OSError, ValueError) as error:
            print(json.dumps({'kind': 'ww56-check', 'status': 'blocked',
                              'checks': {'ok': False, 'diagnostic_readiness':
                                         {'ok': False, 'detail': str(error)}}}, indent=2))
            return 2
    checks = {}
    for name, function in (('supervisor_cgroup', check_supervisor), ('launcher', check_launcher),
                           ('inputs', check_inputs), ('freshness', check_freshness)):
        try:
            checks[name] = {'ok': True, 'detail': function(args)}
        except BaseException as error:
            checks[name] = {'ok': False, 'detail': str(error)}
    checks['ok'] = all(entry['ok'] for entry in checks.values())
    result = {'kind': 'ww56-check', 'checks': checks}
    if diagnostic:
        result['status'] = 'ready-for-supervised-review' if checks['ok'] else 'blocked'
        if checks['ok']:
            result['plan'] = plan
    print(json.dumps(result, indent=2))
    return 0 if checks['ok'] else 2


def plan_input(path, expected_sha256, label):
    identity = file_identity(path)
    if identity['sha256'] != expected_sha256:
        raise ValueError(label + ' does not match its supplied SHA-256')
    return {'source': str(path), 'identity': identity, 'sha256': expected_sha256}


def diagnostic_plan(args, profile=None):
    if not getattr(args, 'diagnostic_run', False):
        raise ValueError('live diagnostic path requires explicit --diagnostic-run')
    if args.diagnostic_profile is None or args.request_token is None:
        raise ValueError('diagnostic run requires a private profile and explicit request token')
    if not math.isfinite(args.hang_seconds) or not 0 < args.hang_seconds <= 30:
        raise ValueError('diagnostic hang window must be positive and at most 30 seconds')
    context = argparse.Namespace(**vars(args))
    context.check = True
    if profile is not None:
        context.diagnostic_profile = profile
    return diagnostic_readiness(context)


def profile_options(plan):
    return {'alt': True} if 'diagnostic_readiness' in plan else {}


def stage_trial(args):
    diagnostic = diagnostic_plan(args) if getattr(args, 'diagnostic_run', False) else None
    runroot = reject_symlinks(Path(args.run_root_base)) / ('ww56-' + args.run_id)
    receipt_base = reject_symlinks(Path(args.receipts_root)) if diagnostic is not None else Path(args.receipts_root)
    receipts = receipt_base / ('ww56-' + args.run_id)
    if runroot.exists() or runroot.is_symlink():
        raise ValueError('runroot already exists; a fresh private profile is required: ' + str(runroot))
    if receipts.exists() or receipts.is_symlink():
        raise ValueError('receipt directory already exists; preserve the old run: ' + str(receipts))
    receipts.mkdir(mode=0o700, parents=True)
    create_run(runroot)
    vpk = plan_input(args.vpk, args.vpk_sha256, 'candidate package')
    runtime = plan_input(args.runtime, args.runtime_sha256, 'runtime')
    assets = stage_runtime(runroot, args.runtime, args.runtime_sha256)
    shutil.copyfile(args.vpk, runroot / 'runtime/ww05.vpk')
    if sha256(runroot / 'runtime/ww05.vpk') != args.vpk_sha256:
        raise ValueError('staged candidate package changed')
    firmware = stage_firmware(runroot, args.firmware, surface_sync=False,
                              pref_path=runroot / FIRMWARE_DIRECTORY,
                              **({'alt': True} if diagnostic is not None else {}))
    shader = stage_shader_compiler(runroot, args.shader_compiler)
    disc = stage_disc(runroot, args.disc, args.disc_sha256)
    libraries = library_identity(args.libraries)
    phases = direct_phases(runroot, args.guest_ceiling_seconds, args.libraries, args.wayland_socket)
    plan = {
        'kind': 'ww56-bounded-normalboot-trial-v1',
        'run_id': args.run_id, 'runroot': str(runroot), 'receipts': str(receipts),
        'launcher_source': args.expected_head, 'guest_source': args.guest_source,
        'vpk': vpk, 'vpk_sha256': vpk['sha256'],
        'runtime': runtime, 'runtime_sha256': runtime['sha256'],
        'staged_runtime': library_identity(runroot / 'runtime', allow_symlinks=False),
        'runtime_inputs': runtime_inputs(args.runtime), 'runtime_assets': assets,
        'runtime_libraries': libraries, 'firmware': firmware, 'shader_compiler': shader, 'disc': disc,
        'private_config': config_identity(runroot, surface_sync=False,
                                          pref_path=runroot / FIRMWARE_DIRECTORY,
                                          **({'alt': True} if diagnostic is not None else {})),
        'wayland_socket': str(args.wayland_socket), 'audio': args.audio, 'continue_gx': args.continue_gx,
        'idle_timeout_seconds': args.hang_seconds, 'cpu_threshold': 0.05,
        'minimum_available_memory_bytes': 0, 'phases': phases,
    }
    if diagnostic is not None:
        plan['diagnostic_readiness'] = diagnostic
    (runroot / 'phases').mkdir(mode=0o700)
    for name, entry in plan['phases'].items():
        phase = runroot / 'phases' / name
        phase.mkdir(mode=0o700)
        (phase / 'evidence').mkdir(mode=0o700)
        with evidence_file(phase, 'plan.json') as stream:
            json.dump(entry, stream, indent=2)
            stream.write('\n')
    write(receipts, 'effective-plan.json', plan)
    (receipts / 'runroot.txt').write_text(str(runroot) + '\n')
    return runroot, receipts, plan


def verify_trial_inputs(runroot, plan, args):
    if getattr(args, 'native_context_artifacts', None) is not None:
        receipt = verify_native_artifacts(args.native_context_artifacts, args.guest_source)
        if receipt['artifacts']['ww05.vpk']['identity']['sha256'] != args.vpk_sha256:
            raise ValueError('candidate VPK differs from preserved observation artifacts')
    if git_head(ROOT) != args.expected_head or not git_clean(ROOT):
        raise ValueError('launcher worktree must stay clean at the reviewed head ' + args.expected_head)
    pinned_sources()
    if file_identity(args.vpk) != plan['vpk']['identity']:
        raise ValueError('candidate package changed since staging')
    if sha256(runroot / 'runtime/ww05.vpk') != plan['vpk_sha256']:
        raise ValueError('staged candidate package changed')
    if file_identity(args.runtime) != plan['runtime']['identity']:
        raise ValueError('runtime source changed')
    if runtime_inputs(args.runtime) != plan['runtime_inputs']:
        raise ValueError('runtime inputs changed')
    if library_identity(runroot / 'runtime', allow_symlinks=False) != plan['staged_runtime']:
        raise ValueError('staged runtime changed')
    if library_identity(args.libraries) != plan['runtime_libraries']:
        raise ValueError('runtime library tree changed')
    verify_firmware(plan['firmware'])
    if file_identity(args.shader_compiler) != plan['shader_compiler']['identity']:
        raise ValueError('shader compiler source changed')
    if file_identity(runroot / plan['shader_compiler']['install_path']) != plan['shader_compiler']['staged']:
        raise ValueError('staged shader compiler changed')
    if file_identity(args.disc) != plan['disc']['identity']:
        raise ValueError('verified disc image changed')
    link = runroot / plan['disc']['install_path']
    if not link.is_symlink() or os.readlink(link) != str(args.disc):
        raise ValueError('private disc link changed')


def verify_private_config(runroot, plan):
    if config_identity(runroot, surface_sync=False,
                       pref_path=runroot / FIRMWARE_DIRECTORY, **profile_options(plan)) != plan['private_config']:
        raise ValueError('private configuration is not the canonical private profile')


def installation_gates(runroot, plan, args, phase):
    canonical = emitted_config(runroot, phase, surface_sync=False,
                               pref_path=runroot / FIRMWARE_DIRECTORY, **profile_options(plan))
    verify_phase_logs(runroot, phase, PHASE_LOG_LIMIT)
    verify_trial_inputs(runroot, plan, args)
    payload = verify_installed(runroot, plan)
    restore_config(runroot, canonical, surface_sync=False, pref_path=runroot / FIRMWARE_DIRECTORY,
                   **profile_options(plan))
    verify_private_config(runroot, plan)
    return payload


def guest_gates(runroot, plan, args, phase):
    canonical = emitted_config(runroot, phase, surface_sync=False,
                               pref_path=runroot / FIRMWARE_DIRECTORY, **profile_options(plan))
    verify_trial_inputs(runroot, plan, args)
    verify_installed(runroot, plan)
    verify_phase_logs(runroot, phase, PHASE_LOG_LIMIT)
    restore_config(runroot, canonical, surface_sync=False, pref_path=runroot / FIRMWARE_DIRECTORY,
                   **profile_options(plan))
    verify_private_config(runroot, plan)


def install_ready_probe(runroot):
    with zipfile.ZipFile(runroot / 'runtime/ww05.vpk') as archive:
        expected = {name: archive.getinfo(name).file_size
                    for name in ('eboot.bin', 'main.dol', 'sce_sys/param.sfo')}
    title = runroot / 'data/Vita3K/Vita3K/ux0/app/BWWG00005'

    def probe():
        try:
            return all((title / name).stat().st_size == size for name, size in expected.items())
        except FileNotFoundError:
            return False

    return probe


class PhaseWatch:
    """Supervise one phase unit: release, watch, snapshot, stop, preserve."""

    def __init__(self, name, args, runroot, receipts, plan, entry, unit, budget, headroom_required,
                 deadline, state, probe=False, ready_probe=None):
        self.name = name
        self.args = args
        self.runroot = runroot
        self.receipts = receipts
        self.plan = plan
        self.unit = unit
        self.budget = budget
        self.headroom_required = headroom_required
        self.state = state
        self.probe = probe
        self.ready_probe = ready_probe
        self.diagnostic = getattr(args, 'diagnostic_run', False)
        self.phase_root = Path(entry['evidence_root'])
        self.journal_path = str(runroot / REPORT_DIRECTORY / 'ww05.log')
        self.readers = {str(path): IncrementalLog(path) for path in self._log_paths()}
        self.idle = ProcessIdleObserver(args.hang_seconds, 0.05, tuple(self._log_paths()), 0)
        self.scanner = OneShotTriggerScanner()
        self.native_context = NativeContextWindow()
        self.native_expected = name == 'guest' and getattr(args, 'native_context_artifacts', None) is not None
        self.stop_state = StopState(ceiling_seconds=deadline,
                                    present_grace_seconds=args.present_grace_seconds,
                                    rel_settle_seconds=args.rel_settle_seconds,
                                    started=time.monotonic())
        self.failure_lines = []
        self.new_lines = 0
        self.next_record = 0
        self.drain_count = 0

    def _log_paths(self):
        return (self.phase_root / 'evidence/process.log', self.runroot / 'cache/Vita3K/vita3k.log',
                self.runroot / REPORT_DIRECTORY / 'ww05.log',
                self.runroot / REPORT_DIRECTORY / 'ww05.partial',
                self.runroot / REPORT_DIRECTORY / 'ww05.report')

    def _release(self):
        ready = self.receipts / (self.name + '-ready.json')
        if not ready.exists():
            return None
        identity = json.loads(ready.read_text())
        props = phase_properties(self.unit, self.diagnostic)
        if props.get('ControlGroup') != identity['cgroup']:
            raise ValueError('unit ControlGroup differs from the gate cgroup: ' + self.name)
        if props.get('MainPID') != str(identity['pid']):
            raise ValueError('unit MainPID differs from the gate pid: ' + self.name)
        if self.diagnostic:
            invocation = wrapper_invocation(self.phase_root, self.unit)
            if not invocation or props.get('InvocationID') != invocation:
                raise ValueError('refusing to release a phase invocation not created by the owned wrapper: ' + self.unit)
        budget_evidence = verify_budget_properties(props, budget_bytes=self.budget)
        own = Path('/proc/self/cgroup').read_text().strip().split('::', 1)[1]
        if Path(identity['cgroup']).parent != Path(own).parent or identity['cgroup'] == own:
            raise ValueError('phase unit must be a sibling of the uncapped supervisor: ' + self.name)
        base = Path('/sys/fs/cgroup' + identity['cgroup'])
        kernel = verify_cgroup_budget((base / 'memory.max').read_text(),
                                      (base / 'memory.swap.max').read_text(), self.budget)
        headroom = headroom_decision(parse_mem_available(Path('/proc/meminfo').read_text()),
                                     self.headroom_required)
        write(self.receipts, self.name + '-verified-preexec.json',
              {'unit': self.unit, 'properties': props, 'budget_evidence': budget_evidence,
               'kernel': kernel, 'headroom': headroom, 'supervisor_cgroup': own,
               'guest_cgroup': identity['cgroup'], 'pid': identity['pid']})
        if not headroom['ok'] and not self.probe:
            message = ('BLOCKED: MemAvailable ' + str(headroom['available_bytes']) +
                       ' bytes is below the required ' + str(headroom['required_bytes']) +
                       ' bytes before ' + self.name + '; no guest command was released and no ' +
                       'other application was touched')
            write(self.receipts, self.name + '-blocked.json',
                  {'phase': self.name, 'unit': self.unit, 'headroom': headroom,
                   'guest_cgroup': identity['cgroup'], 'message': message})
            raise PreconditionBlocked(message)
        self.state.update(pid=identity['pid'], cgroup=identity['cgroup'], released=True,
                          properties_before_exec=props,
                          invocation_id=props.get('InvocationID') or None)
        (self.receipts / (self.name + '-permit')).touch(exist_ok=False)
        print('RELEASE ' + self.name + ' isolated kernel cap verified', flush=True)
        return None

    def _read_logs(self):
        lines, journal = [], []
        for path, reader in self.readers.items():
            new, count = reader.read()
            lines.extend(new)
            if path == self.journal_path:
                journal.extend(new)
        return lines, journal

    def _ingest(self, now, lines, journal):
        self.native_context.feed(journal, now=now, wall=time.time())
        if self.native_expected or self.native_context.events or self.native_context.error:
            self.state['native_context'] = self.native_context.receipt()
            self.stop_state.ceiling_seconds = min(self.stop_state.ceiling_seconds, 180)
        events = parse_frame_events(journal).presents
        if events:
            if self.state['presentations']:
                if (events[0].index <= self.state['presentations'][-1]['index'] or
                        events[0].us <= self.state['presentations'][-1]['us']):
                    raise ValueError('presentation stream repeats or goes backwards across incremental reads')
            self.state['presentations'].extend(vars(event) for event in events)
            if self.stop_state.first_present_at is None:
                self.stop_state.note_present(now)
        for line in journal:
            if '[rel-frontier]' in line and self.stop_state.first_rel_at is None:
                self.stop_state.note_rel(now)
                self.state['first_rel'] = line
        failures = [line for line in lines if is_failure(line)]
        if failures:
            self.failure_lines.extend(failures)
        pid = self.state.get('pid')
        if self.diagnostic:
            return
        for trigger in self.scanner.feed(lines):
            if pid is None:
                self.state['snapshots'].append({'event': trigger.kind, 'missed': 'process never released'})
                print('SNAPSHOT-MISSED ' + trigger.kind + ' process never released', flush=True)
                continue
            directory = self.receipts / ('snapshot-' + trigger.kind)
            try:
                summary = capture_snapshot(Path('/proc', str(pid)), self.state['cgroup'], directory,
                                           event=trigger.kind, trigger=vars(trigger))
            except (FileNotFoundError, ProcessLookupError) as error:
                self.state['snapshots'].append({'event': trigger.kind, 'missed': str(error)})
                print('SNAPSHOT-MISSED ' + trigger.kind + ' ' + str(error), flush=True)
                continue
            self.state['snapshots'].append({'event': trigger.kind, 'directory': directory.name,
                                            'totals': summary['totals'],
                                            'duration_seconds': summary['duration_seconds']})
            print('SNAPSHOT ' + trigger.kind + ' ' + json.dumps(summary['totals']), flush=True)

    def _observe_frontier(self, now, lines, journal):
        self._ingest(now, lines, journal)
        stop = evaluate_stop(self.stop_state, now=now, failure_lines=self.failure_lines)
        if stop is None:
            stop = self.native_context.stop(now)
        if stop is None and self.ready_probe is not None and self.ready_probe():
            stop = {'stop_reason': 'installed_payload', 'payload': verify_installed(self.runroot, self.plan)}
        return stop

    def final_drain(self, reason):
        now = time.monotonic()
        lines, journal = self._read_logs()
        self.new_lines += len(lines)
        self._ingest(now, lines, journal)
        if self.native_expected or self.native_context.events or self.native_context.error:
            path = self.runroot / REPORT_DIRECTORY / 'ww117-native-ctx.bin'
            try:
                if path.stat().st_size > 593920:
                    raise ValueError('native-context trace exceeds its fixed file bound')
                decoded = decode_native_trace(path.read_bytes())
                self.state['native_context']['trace'] = {
                    key: value for key, value in decoded.items() if key != 'rolling'}
            except (FileNotFoundError, ValueError) as error:
                self.state['native_context']['trace'] = {'coverage': 'inconclusive', 'error': str(error)}
        if self.state.get('presentations_at_stop') is None:
            self.state['presentations_at_stop'] = len(self.state['presentations'])
        self.drain_count += 1
        self.state['terminal_drain'] = {
            'reason': reason, 'drain_count': self.drain_count, 'new_lines': self.new_lines,
            'presentations_total': len(self.state['presentations']),
            'presentations_at_stop': self.state['presentations_at_stop'],
            'last_present': self.state['presentations'][-1] if self.state['presentations'] else None,
            'first_rel': self.state.get('first_rel'),
            'failure_lines': list(self.failure_lines),
            'snapshots': [entry['event'] for entry in self.state['snapshots']]}
        record = {'phase': self.name, 'monotonic': now, 'terminal_drain': reason,
                  'drain_count': self.drain_count, 'new_lines': self.new_lines,
                  'presentations_total': len(self.state['presentations']),
                  'presentations_at_stop': self.state['presentations_at_stop'],
                  'snapshots': self.state['terminal_drain']['snapshots']}
        with (self.receipts / 'cpu-log-watch.jsonl').open('a') as stream:
            stream.write(json.dumps(record) + '\n')
        print('DRAIN ' + self.name + ' ' + json.dumps(record), flush=True)
        self.new_lines = 0

    def __call__(self, _wrapper_pid):
        now = time.monotonic()
        if not self.state['released']:
            return self._release()
        pid = self.state['pid']
        if not Path('/proc', str(pid)).exists():
            self.final_drain('managed-native-exit')
            self.state['properties_at_exit'] = stop_owned_unit(self.unit, self.state)
            return {'stop_reason': 'managed-native-exit',
                    'native_properties': self.state['properties_at_exit']}
        if self.probe:
            return None
        if self.diagnostic:
            self._verify_owned_process()
        lines, journal = self._read_logs()
        self.new_lines += len(lines)
        stop = self._observe_frontier(now, lines, journal)
        if stop is None:
            observation = self.idle(pid)
            if observation is not None:
                stop = observation
        if stop is None and now < self.next_record:
            return None
        self.next_record = now + 1
        record = {'phase': self.name, 'monotonic': now, 'pid': pid,
                  'cpu_fraction': self.idle.window.cpu_fraction, 'new_lines': self.new_lines,
                  'tail': (lines or journal)[-2:], 'presentations': len(self.state['presentations']),
                  'snapshots': [entry['event'] for entry in self.state['snapshots']],
                  'stop': stop}
        self.new_lines = 0
        with (self.receipts / 'cpu-log-watch.jsonl').open('a') as stream:
            stream.write(json.dumps(record) + '\n')
        print('WATCH ' + json.dumps(record), flush=True)
        if stop:
            self.state['requested_stop'] = stop
            if self.state.get('presentations_at_stop') is None:
                self.state['presentations_at_stop'] = len(self.state['presentations'])
            if self.diagnostic:
                self._verify_owned_process()
            subprocess.run(['systemctl', '--user', 'kill', '--signal=SIGTERM',
                            '--kill-whom=all', self.unit], check=True, timeout=5)
            return stop
        return None

    def _verify_owned_process(self):
        current = properties(self.unit, keys=('ControlGroup', 'MainPID', 'InvocationID'))
        if (not self.state.get('invocation_id') or
                current.get('InvocationID') != self.state['invocation_id'] or
                current.get('MainPID') != str(self.state['pid']) or
                current.get('ControlGroup') != self.state['cgroup']):
            raise ValueError('refusing to observe or signal a changed phase invocation: ' + self.unit)


def run_phase(name, args, runroot, receipts, plan, record, *, budget, headroom, deadline,
              probe=False, ready_probe=None):
    entry = plan['phases'][name]
    phase_root = Path(entry['evidence_root'])
    unit = 'ww56-' + args.run_id + '-' + name + '.service'
    state = {'unit': unit, 'budget_bytes': budget, 'headroom_required_bytes': headroom,
             'deadline_seconds': deadline, 'pid': None, 'snapshots': [], 'presentations': [],
             'released': False, 'phase_root': str(phase_root), 'invocation_id': None,
             'presentations_at_stop': None}
    record['phases'][name] = state
    if getattr(args, 'diagnostic_run', False):
        state['diagnostic'] = True
    state['properties_before_creation'] = require_fresh_unit(unit, state.get('diagnostic', False))
    state['cleanup_stop_timeout_seconds'] = 2 * args.cleanup_grace_seconds + 10
    spec = {'command': entry['command'], 'environment': entry['environment'], 'cwd': entry['cwd'],
            'budget_bytes': budget, 'unit': unit, 'permit_timeout_seconds': PERMIT_TIMEOUT_SECONDS}
    write(receipts, name + '-exec.json', spec)
    command = ['/usr/bin/systemd-run', '--user', '--unit=' + unit, '--slice=app.slice', '--wait', '--pipe',
               '--property=MemoryMax=' + str(budget), '--property=MemorySwapMax=0',
               '--property=OOMPolicy=stop', '--property=KillMode=control-group',
               '--property=LimitCORE=0',
               '--property=TimeoutStopSec=' + format(args.cleanup_grace_seconds, 'g') + 's',
               '--property=RemainAfterExit=yes',
               '--property=RuntimeMaxSec=' + str(int(deadline + 15)),
               '/usr/bin/python3', '-B', str(SCRIPT_DIRECTORY / 'exec_gate.py'), str(receipts), name]
    watch = PhaseWatch(name, args, runroot, receipts, plan, entry, unit, budget, headroom, deadline, state,
                       probe=probe, ready_probe=ready_probe)
    failure = None
    try:
        state['run_owned_status'] = run_owned(command, runroot, deadline + 10, evidence_root=phase_root,
                                              phase_observer=watch, environment=dict(os.environ),
                                              cwd=ROOT)
    except BaseException as error:
        failure = error
        state['error'] = str(error)
        raise
    finally:
        late_error = None
        try:
            watch.final_drain('wrapper-exit')
        except BaseException as error:
            state['drain_error'] = str(error)
            print('DRAIN-ERROR ' + str(error), flush=True)
            late_error = error
        try:
            state['properties_before_cleanup'] = stop_owned_unit(unit, state)
        except BaseException as error:
            state['cleanup_error'] = str(error)
            late_error = late_error or error
        try:
            state['properties_after_cleanup'] = phase_properties(unit, state.get('diagnostic', False))
        except BaseException as error:
            state['cleanup_properties_error'] = str(error)
            late_error = late_error or error
        try:
            state['wrapper'] = json.loads((phase_root / 'evidence/process.json').read_text())
        except FileNotFoundError:
            state['wrapper'] = None
        except (OSError, ValueError) as error:
            state['wrapper_error'] = str(error)
            late_error = late_error or error
        try:
            watch.final_drain('post-cleanup')
        except BaseException as error:
            state['drain_error'] = str(error)
            print('DRAIN-ERROR ' + str(error), flush=True)
            late_error = late_error or error
        state['presentations_total'] = len(state['presentations'])
        try:
            journal_text = (runroot / REPORT_DIRECTORY / 'ww05.log').read_text(errors='replace')
        except FileNotFoundError:
            journal_text = ''
        except OSError as error:
            state['journal_error'] = str(error)
            journal_text = ''
            late_error = late_error or error
        state['journal_present_lines'] = count_marker_lines(journal_text, PRESENT_MARKER)
        state['presentations_undrained'] = max(0, state['journal_present_lines'] - state['presentations_total'])
        state['native_pid_absent_after_cleanup'] = (state['pid'] is None or
                                                    not Path('/proc', str(state['pid'])).exists())
        if state['pid'] is not None and not state['native_pid_absent_after_cleanup']:
            late_error = late_error or ValueError('phase native process was not reaped: ' + name)
        try:
            write(receipts, name + '-disposition.json', state)
        except BaseException as error:
            state['disposition_error'] = str(error)
            print('DISPOSITION-ERROR ' + str(error), flush=True)
            late_error = late_error or error
        print('STOP ' + name + ' ' + json.dumps(state.get('properties_before_cleanup', {})), flush=True)
        if failure is None and late_error is not None:
            raise late_error
    return state


def run_probe(args, runroot, receipts, record):
    unit = 'ww56-' + args.run_id + '-probe.service'
    probe_root = receipts / 'probe'
    (probe_root / 'evidence').mkdir(parents=True, mode=0o700)
    entry = {'command': ['/usr/bin/true'], 'environment': {'PATH': '/usr/bin', 'LANG': 'C.UTF-8'},
             'cwd': str(receipts), 'evidence_root': str(probe_root)}
    state = run_phase('probe', args, runroot, receipts, {'phases': {'probe': entry}}, record,
                      budget=args.install_budget_bytes, headroom=args.install_headroom_bytes,
                      deadline=INSTALL_DEADLINE_SECONDS, probe=True)
    props = state.get('properties_at_exit') or state.get('properties_before_cleanup') or {}
    if props.get('ExecMainCode') != '1' or props.get('ExecMainStatus') != '0':
        raise ValueError('isolation probe disposition unexpected: ' + json.dumps(props))
    record['isolation_probe_passed'] = True


def run_trial(args):
    os.umask(0o077)
    started = time.monotonic()
    record = {'kind': 'ww56-bounded-normalboot-trial-v1', 'run_id': args.run_id,
              'completion': 'INCOMPLETE', 'phases': {},
              'memory_sampling': 'one-shot event-driven snapshots only; no polling loop and no threshold kills',
              'memory_watcher_disabled': True, 'no_source_edits_or_build': True,
              'install_budget_bytes': args.install_budget_bytes,
              'guest_budget_bytes': args.guest_budget_bytes,
              'install_headroom_bytes': args.install_headroom_bytes,
              'guest_headroom_bytes': args.guest_headroom_bytes,
              'guest_ceiling_seconds': args.guest_ceiling_seconds,
              'rel_settle_seconds': args.rel_settle_seconds,
              'present_grace_seconds': args.present_grace_seconds}
    receipts = None
    try:
        if getattr(args, 'diagnostic_run', False):
            record['diagnostic_readiness'] = diagnostic_plan(args)
            record['memory_sampling'] = 'disabled; CPU, log progress and owned process state only'
            for gate in (check_supervisor, check_launcher, check_inputs, check_freshness):
                gate(args)
        record['supervisor'] = verify_uncapped_supervisor(args.supervisor_unit)
        record['identities_before'] = record_identities(args)
        runroot, receipts, plan = stage_trial(args)
        record.update(runroot=str(runroot), receipts=str(receipts), guest_source=plan['guest_source'],
                      vpk_sha256=plan['vpk_sha256'], runtime_sha256=plan['runtime_sha256'])
        verify_trial_inputs(runroot, plan, args)
        verify_private_config(runroot, plan)
        run_probe(args, runroot, receipts, record)
        install_entry = plan['phases']['install']
        install = run_phase('install', args, runroot, receipts, plan, record,
                            budget=args.install_budget_bytes, headroom=args.install_headroom_bytes,
                            deadline=INSTALL_DEADLINE_SECONDS,
                            ready_probe=install_ready_probe(runroot))
        stop = install.get('requested_stop') or {}
        if stop.get('stop_reason') != 'installed_payload':
            raise ValueError('installer did not report the installed payload: ' + json.dumps(stop))
        phase = Path(install_entry['evidence_root'])
        payload = installation_gates(runroot, plan, args, phase)
        request_options = {}
        if getattr(args, 'diagnostic_run', False):
            diagnostic_plan(args)
            request_options['token'] = args.request_token
        token = fresh_request(runroot, plan['guest_source'], plan['vpk_sha256'], args.continue_gx, 0,
                              args.audio == 'deferred', None, **request_options)
        if getattr(args, 'diagnostic_run', False):
            record['staged_diagnostic_readiness'] = diagnostic_plan(args, runroot)
        write(receipts, 'guest-request.json',
              {'token': token, 'source': plan['guest_source'], 'vpk_sha256': plan['vpk_sha256'],
               'audio': args.audio, 'continue_gx': args.continue_gx,
               'actor_injection': False, 'PNG': False, 'MEM1': False, 'payload': payload})
        guest = run_phase('guest', args, runroot, receipts, plan, record,
                          budget=args.guest_budget_bytes, headroom=args.guest_headroom_bytes,
                          deadline=args.guest_ceiling_seconds)
        record['guest_stop'] = guest.get('requested_stop') or guest.get('properties_before_cleanup', {}).get('Result')
        guest_gates(runroot, plan, args, Path(plan['phases']['guest']['evidence_root']))
        record['postflight_input_and_payload_gates'] = 'PASS'
    except PreconditionBlocked as error:
        record.update(completion='BLOCKED', error=str(error), blocked=True)
    except BaseException as error:
        record.update(completion='ABORTED', error=str(error), traceback=traceback.format_exc())
        print('ERROR ' + traceback.format_exc(), flush=True)
    finally:
        record.update(duration_seconds=time.monotonic() - started,
                      finished_local=time.strftime('%Y-%m-%dT%H:%M:%S%z'))
        try:
            record['identities_after'] = record_identities(args)
            record['identity_before_equals_after'] = (
                record['identities_after'] == record.get('identities_before'))
        except BaseException as error:
            record['postflight_identity_error'] = str(error)
        if receipts is not None:
            write(receipts, 'result.json', record)
            print('RESULT ' + str(receipts / 'result.json'), flush=True)
    return 3 if record.get('blocked') else 2 if record.get('error') else 0


def sha256_argument(value):
    if re.fullmatch(r'[0-9a-f]{64}', value) is None:
        raise argparse.ArgumentTypeError('must be a lowercase 64-hex SHA-256')
    return value


def head_argument(value):
    if re.fullmatch(r'[0-9a-f]{40}', value) is None:
        raise argparse.ArgumentTypeError('must be a lowercase 40-hex commit')
    return value


def grace_argument(value):
    number = float(value)
    if number == 0:
        return None
    if not math.isfinite(number) or number < 0:
        raise argparse.ArgumentTypeError('must be 0 to disable or a finite positive number of seconds')
    return number


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected-head', type=head_argument, required=True,
                        help='reviewed launcher commit; the worktree must be clean at it')
    parser.add_argument('--vpk', type=Path, required=True,
                        help='current-source candidate VPK supplied by the coordinator')
    parser.add_argument('--vpk-sha256', type=sha256_argument, required=True)
    parser.add_argument('--runtime', type=Path, default=STACK / 'vita3k/Vita3K')
    parser.add_argument('--runtime-sha256', type=sha256_argument, default=RUNTIME_SHA256)
    parser.add_argument('--guest-source', type=head_argument, required=True,
                        help='guest build source identity for the private ww05.request')
    parser.add_argument('--native-context-artifacts', type=Path,
                        help='private preserved ELF receipt for a trace-only observation build; verified before exec')
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--disc-sha256', type=sha256_argument, required=True)
    parser.add_argument('--wayland-socket', type=Path, required=True)
    parser.add_argument('--shader-compiler', type=Path, default=SHADER_COMPILER)
    parser.add_argument('--libraries', type=Path, default=STACK / 'vita3k-env')
    parser.add_argument('--firmware', type=Path, default=STACK / 'firmware')
    parser.add_argument('--run-id', required=True,
                        help='fresh identifier; the runroot is build-f5/ww56-<id>')
    parser.add_argument('--run-root-base', type=Path,
                        default=Path('/home/fred/projects/zeldaWWHDRecomp/build-f5'))
    parser.add_argument('--receipts-root', type=Path, default=Path('/tmp/opencode'))
    parser.add_argument('--supervisor-unit', default='ww56-supervisor.service')
    parser.add_argument('--guest-ceiling-seconds', type=float, default=180.0)
    parser.add_argument('--rel-settle-seconds', type=grace_argument, default=5.0,
                        help='stop this many seconds after the first requested REL; 0 disables')
    parser.add_argument('--present-grace-seconds', type=grace_argument, default=10.0,
                        help='stop this many seconds after the first presentation when no REL follows; 0 disables')
    parser.add_argument('--hang-seconds', type=float, default=30.0)
    parser.add_argument('--cleanup-grace-seconds', type=float, default=10.0,
                        help='unit stop grace (TimeoutStopSec) before systemd escalates, 1 to 60 seconds; core dumps stay disabled')
    parser.add_argument('--audio', choices=('deferred', 'strict'), default='deferred')
    parser.add_argument('--continue-gx', type=int, default=64)
    parser.add_argument('--install-budget-bytes', type=int, default=DEFAULT_INSTALL_BUDGET_BYTES)
    parser.add_argument('--guest-budget-bytes', type=int, default=DEFAULT_GUEST_BUDGET_BYTES)
    parser.add_argument('--install-headroom-bytes', type=int, default=DEFAULT_INSTALL_HEADROOM_BYTES)
    parser.add_argument('--guest-headroom-bytes', type=int, default=DEFAULT_GUEST_HEADROOM_BYTES)
    parser.add_argument('--check', action='store_true',
                        help='verify preconditions read-only; launch nothing and write nothing')
    parser.add_argument('--diagnostic-profile', type=Path,
                        help='existing private cpu-opt-off profile with staged diagnostic requests; read-only input, not authorization')
    parser.add_argument('--request-token',
                        help='explicit 32-hex token binding all three diagnostic requests')
    parser.add_argument('--diagnostic-run', action='store_true',
                        help='opt in to the CPU/log-only supervised diagnostic path; operator approval remains required')
    args = parser.parse_args()
    if args.diagnostic_run and (args.check or args.diagnostic_profile is None or args.request_token is None):
        parser.error('--diagnostic-run requires --diagnostic-profile and --request-token, without --check')
    if args.diagnostic_profile is not None and (not (args.check or args.diagnostic_run) or args.request_token is None):
        parser.error('--diagnostic-profile requires --check or --diagnostic-run and an explicit --request-token')
    if args.request_token is not None and args.diagnostic_profile is None:
        parser.error('--request-token requires --diagnostic-profile')
    if re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', args.run_id) is None:
        parser.error('run id must be a simple identifier')
    for label, value in (('install', args.install_budget_bytes), ('guest', args.guest_budget_bytes),
                         ('install headroom', args.install_headroom_bytes),
                         ('guest headroom', args.guest_headroom_bytes)):
        if value <= 0:
            parser.error(label + ' byte count must be positive')
    if args.install_headroom_bytes < args.install_budget_bytes:
        parser.error('install headroom must be at least the install budget')
    if args.guest_headroom_bytes < args.guest_budget_bytes:
        parser.error('guest headroom must be at least the guest budget')
    if not math.isfinite(args.guest_ceiling_seconds) or not 0 < args.guest_ceiling_seconds <= 600:
        parser.error('guest ceiling must be finite, positive and at most 600 seconds')
    if not math.isfinite(args.hang_seconds) or args.hang_seconds <= 0:
        parser.error('hang window must be finite and positive')
    if args.diagnostic_run and args.hang_seconds > 30:
        parser.error('diagnostic hang window must be at most 30 seconds')
    if not math.isfinite(args.cleanup_grace_seconds) or not 1 <= args.cleanup_grace_seconds <= 60:
        parser.error('cleanup grace must be finite and between 1 and 60 seconds')
    try:
        validate_continuation(args.continue_gx)
    except ValueError as error:
        parser.error(str(error))
    if args.audio == 'deferred' and not args.continue_gx:
        parser.error('deferred audio requires the explicit continuation profile')
    return args


def main():
    args = parse_arguments()
    if args.check:
        return check_mode(args)
    return run_trial(args)


if __name__ == '__main__':
    raise SystemExit(main())
