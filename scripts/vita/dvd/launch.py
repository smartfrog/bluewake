#!/usr/bin/env python3
"""Run the VITA-WW-13 probe once in a fresh isolated Vita3K profile.

Uses the established isolation machinery (scripts/vita/armv7/fp_launch.py and
fp_profile.py): bwrap with unshared namespaces, a private HOME/XDG set, the
public runtime staged read-only, the installed system firmware vs0/sa0, and
an offscreen Qt/SDL platform so no desktop window appears. Never uses the
real HOME, never reuses a profile.

Two phases, as the established launcher does: install the probe VPK, then run
the installed title. The guest writes its report to ux0:data/ww13/probe.report
because Vita3K does not relay guest stdout; the launcher polls that file and
stops the emulator once the verdict is complete.
"""

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))

from fp_launch import create_run, run_owned, sandbox_command, stage_runtime  # noqa: E402
from fp_profile import stage_firmware, verify_firmware  # noqa: E402
sys.path.insert(0, str(ROOT / 'scripts/vita'))
from vita_workspace import FIRMWARE, STACK, VITA3K  # noqa: E402

DEFAULT_VITA3K = VITA3K
DEFAULT_LIBRARIES = STACK / 'vita3k-env'
DEFAULT_FIRMWARE = FIRMWARE
DEFAULT_OUT = ROOT / 'build-f13/ww13-seam/vita3k'
PROBE_IMAGE = 'ux0:data/ww13/probe.img'
PROBE_REPORT = 'ux0:data/ww13/probe.report'
TITLE_ID = 'BWWG00013'


def sha256(path):
    import hashlib
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def run_guest_watch(command, run_dir, timeout, report_path):
    phase = run_dir / 'phases/guest'
    (phase / 'evidence').mkdir(parents=True)
    log_path = phase / 'evidence/process.log'
    started = time.time()
    verdict = None
    with log_path.open('wb') as log:
        process = subprocess.Popen(command, cwd=run_dir / 'work', stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=log,
                                   env={'PATH': '/usr/bin', 'LANG': 'C.UTF-8'},
                                   start_new_session=True)
        try:
            deadline = started + timeout
            while time.time() < deadline:
                if report_path.is_file():
                    content = report_path.read_text(errors='replace')
                    if 'REAL PASS' in content or 'REAL FAIL' in content:
                        verdict = 'REAL PASS' if 'REAL PASS' in content else 'REAL FAIL'
                        time.sleep(1.0)
                        break
                if process.poll() is not None:
                    break
                time.sleep(0.5)
        finally:
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
    record = {'exit': process.returncode, 'duration_seconds': round(time.time() - started, 3),
              'verdict': verdict, 'stop': 'verdict' if verdict else 'deadline-or-exit'}
    (phase / 'evidence/process.json').write_text(json.dumps(record, indent=2) + '\n')
    return record, log_path.read_text(errors='replace')


def launch(vita3k, vpk, image, out_root, timeout, label, paths=(), libraries=None, firmware=None,
           install_timeout=60, wayland_socket=None):
    run_dir = Path(out_root) / f'vita3k-run-{label}-{uuid.uuid4().hex[:8]}'
    create_run(run_dir)
    runtime_sha = sha256(vita3k)
    stage_runtime(run_dir, vita3k, runtime_sha)
    shutil.copyfile(vpk, run_dir / 'runtime/ww13.vpk')
    firmware_records = stage_firmware(run_dir, firmware) if firmware else {}
    verify_firmware(firmware_records)
    profile = run_dir / 'data/Vita3K/Vita3K'
    image_target = profile / 'ux0/data/ww13/probe.img'
    image_target.parent.mkdir(parents=True)
    shutil.copyfile(image, image_target)
    report_path = profile / 'ux0/data/ww13/probe.report'

    install_dir = run_dir / 'phases/install'
    (install_dir / 'evidence').mkdir(parents=True)
    install_command = sandbox_command(
        run_dir, 'Vita3K', ['--console', '--backend-renderer', 'OpenGL', 'ww13.vpk'], libraries,
        wayland_socket=wayland_socket)
    install_status = run_owned(install_command, run_dir, install_timeout, evidence_root=install_dir)
    installed = (profile / 'ux0/app' / TITLE_ID / 'eboot.bin').is_file()

    app_args = ', '.join(['real', PROBE_IMAGE, PROBE_REPORT, *paths])
    guest_command = sandbox_command(
        run_dir, 'Vita3K',
        ['--console', '--backend-renderer', 'OpenGL', '--installed-path', TITLE_ID, '-Z', app_args],
        libraries, wayland_socket=wayland_socket)
    guest_record, console = run_guest_watch(guest_command, run_dir, timeout, report_path)
    report = report_path.read_text(errors='replace') if report_path.is_file() else ''
    evidence = {
        'label': label,
        'run_directory': str(run_dir),
        'vita3k': {'path': str(vita3k), 'sha256': runtime_sha},
        'libraries': str(libraries) if libraries else None,
        'firmware': str(firmware) if firmware else None,
        'wayland_socket': str(wayland_socket) if wayland_socket else None,
        'vpk': {'path': str(vpk), 'sha256': sha256(vpk), 'title': TITLE_ID},
        'image': {'source': str(image), 'guest_path': PROBE_IMAGE,
                  'bytes': Path(image).stat().st_size},
        'install': {'status': install_status, 'installed': installed},
        'guest': guest_record,
        'sandbox_guest_command': guest_command,
        'timeout_seconds': timeout,
        'probe_pass': 'REAL PASS' in report,
        'probe_fail': 'REAL FAIL' in report,
        'probe_report': report,
        'console_tail': '\n'.join(console.splitlines()[-20:]),
    }
    (run_dir / 'evidence/ww13-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vita3k', type=Path, default=DEFAULT_VITA3K)
    parser.add_argument('--vpk', type=Path, required=True)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--libraries', type=Path, default=DEFAULT_LIBRARIES)
    parser.add_argument('--firmware', type=Path, default=DEFAULT_FIRMWARE)
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--label', default='probe')
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--install-timeout', type=int, default=60)
    parser.add_argument('--wayland-socket', type=Path, default=None,
                        help='authorized display socket; without it the sandbox stays offscreen')
    parser.add_argument('--path', action='append', default=[], dest='paths',
                        help='disc path to resolve through the pin (repeatable)')
    args = parser.parse_args()
    evidence = launch(args.vita3k, args.vpk, args.image, args.out, args.timeout, args.label,
                      args.paths, args.libraries, args.firmware, args.install_timeout,
                      args.wayland_socket)
    print(f"installed={evidence['install']['installed']} guest_exit={evidence['guest']['exit']} "
          f"pass={evidence['probe_pass']} run={evidence['run_directory']}")
    return 0 if evidence['probe_pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
