#!/usr/bin/env python3
"""VITA-WW-02 isolated Vita3K run for the GX vitaGL demo fixture.

Fresh private profile per run (never shared, never reused), no network, bwrap
containment, firmware vs0/sa0 staged from the local stack, and libshacccg.suprx
staged privately from the Cloudpunk stack for the Cg runtime compiler (a
system file owned locally, never committed or published). The fixture's report
and PNG capture are collected into the run's evidence. A short Wayland window
appears while the demo runs: the GUI does not start the content offscreen.

Environment:
  WW02_BUILD  build directory (default: <main checkout>/build-f2/ww02)
"""

import argparse
import errno
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import time
import uuid
from capture_profile import surface_sync_config

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts/vita'))
from vita_workspace import SHADER_COMPILER, STACK, VITAGL  # noqa: E402

DEFAULT_BUILD = ROOT / 'build-f2/ww02'
DEFAULT_STACK = STACK
# Official upstream vitaGL archive built with its stock Makefile and staged in the private stack.
VITAGL_ARCHIVE = VITAGL
TITLE_ID = "BWWG00002"
FIRMWARE = Path("data/Vita3K/Vita3K")
UX0_DATA = FIRMWARE / "ux0/data/ww02"
WRITABLE_DIRECTORIES = ("home", "config", "cache", "data", "state", "work", "xdg-runtime")
PHASE_TIMEOUT = 90.0
REPORT_PREFIXES = ("ww02 ", "gl version", "scenario ", "draw ", "plan skipped",
                   "program refused", "readback", "capture written", "copy ",
                   "draws=", "demo done", "FAIL", "PIXELS", "probe ", "fbo ")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_timeout(timeout):
    if not math.isfinite(timeout) or not 0 < timeout <= 600:
        raise ValueError('each phase deadline must be finite, positive and at most 600 seconds')


def copy_readonly(source, destination, cache):
    source, destination, cache = Path(source), Path(destination), Path(cache)
    if source.suffix.lower() == '.vpk':
        return shutil.copy2(source, destination)
    checksum = sha256(source)
    mode = stat.S_IMODE(source.stat().st_mode)
    cache.mkdir(parents=True, exist_ok=True, mode=0o700)
    stored = cache / f'{checksum}-{mode:04o}'
    if not stored.exists():
        temporary = cache / ('.stage-' + uuid.uuid4().hex)
        shutil.copy2(source, temporary)
        if sha256(temporary) != checksum:
            raise ValueError(f'readonly source changed while staging: {source}')
        temporary.replace(stored)
    if sha256(stored) != checksum or stat.S_IMODE(stored.stat().st_mode) != mode:
        raise ValueError(f'readonly cache SHA-256 mismatch: {stored}')
    try:
        os.link(stored, destination)
    except OSError as error:
        if error.errno != errno.EXDEV:
            raise
        shutil.copy2(stored, destination)
    return str(destination)


def find_recompcore():
    candidate = ROOT / "ref/vita-ww-01/RecompCore"
    if (candidate / "GXRuntime").is_dir():
        return candidate
    return ROOT / 'ref/vita-ww-01/RecompCore'


def build_vpk(build):
    vitasdk = STACK / "vitasdk"
    env = dict(os.environ, VITASDK=str(vitasdk))
    vpk_dir = build / "gx-vita"
    vpk_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cmake", "-G", "Ninja", "-S", str(ROOT / "vita/feasibility/gx"),
                    "-B", str(vpk_dir),
                    f"-DCMAKE_TOOLCHAIN_FILE={vitasdk}/share/vita.toolchain.cmake",
                    f"-DRECOMPCORE_DIR={find_recompcore()}",
                    f"-DWW02_VITAGL_LIBRARY={VITAGL_ARCHIVE}",
                    "-DCMAKE_BUILD_TYPE=Release"], env=env, check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", str(vpk_dir), "--target", "ww02_gx_demo.vpk"],
                   env=env, check=True, stdout=subprocess.DEVNULL)
    return vpk_dir / "ww02_gx_demo.vpk"


def sandbox_command(root, stack, arguments, wayland_socket):
    command = ["/usr/bin/bwrap", "--unshare-all", "--die-with-parent", "--new-session",
               "--ro-bind", "/usr", "/usr", "--ro-bind", "/etc", "/etc",
               "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib64", "/lib64",
               "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
               "--ro-bind", str(root), "/run/ww03",
               "--ro-bind", str(root / "runtime"), "/stack/vita3k",
               "--chdir", "/stack/vita3k", "--clearenv"]
    for name in WRITABLE_DIRECTORIES:
        command.extend(["--bind", str(root / name), "/run/ww03/" + name])
    for name in ("vs0", "sa0"):
        command.extend(["--ro-bind", str(root / FIRMWARE / name),
                        "/run/ww03/" + str(FIRMWARE / name)])
    command.extend(["--ro-bind", str(stack / "vita3k-env"), "/stack/vita3k-env"])
    environment = {
        "PATH": "/usr/bin", "LANG": "C.UTF-8", "HOME": "/run/ww03/home",
        "XDG_CONFIG_HOME": "/run/ww03/config", "XDG_CACHE_HOME": "/run/ww03/cache",
        "XDG_DATA_HOME": "/run/ww03/data", "XDG_STATE_HOME": "/run/ww03/state",
        "XDG_RUNTIME_DIR": "/run/ww03/xdg-runtime",
        "XDG_CONFIG_DIRS": "/run/ww03/config-system", "XDG_DATA_DIRS": "/run/ww03/data-system",
        "SDL_AUDIODRIVER": "dummy", "LIBGL_ALWAYS_SOFTWARE": "1",
        "FONTCONFIG_FILE": "/etc/fonts/fonts.conf",
        "LD_LIBRARY_PATH": "/stack/vita3k-env/lib",
        "QT_PLUGIN_PATH": "/stack/vita3k-env/lib/qt6/plugins",
    }
    if wayland_socket is not None:
        command.extend(["--ro-bind", str(wayland_socket),
                        "/run/ww03/xdg-runtime/wayland-0"])
        environment["QT_QPA_PLATFORM"] = "wayland"
        environment["WAYLAND_DISPLAY"] = "wayland-0"
    else:
        environment["QT_QPA_PLATFORM"] = "offscreen"
        environment["SDL_VIDEODRIVER"] = "offscreen"
    for name, value in environment.items():
        command.extend(["--setenv", name, value])
    return [*command, "/stack/vita3k/Vita3K", *arguments]


def run_phase(root, stack, name, arguments, timeout, wayland_socket):
    validate_timeout(timeout)
    log = root / "phases" / f"{name}.log"
    started = time.monotonic()
    with open(log, "wb") as stream:
        try:
            result = subprocess.run(
                sandbox_command(root, stack, arguments, wayland_socket),
                cwd=root / "work", stdin=subprocess.DEVNULL, stdout=stream,
                stderr=subprocess.STDOUT, start_new_session=True, timeout=timeout)
            status = result.returncode
        except subprocess.TimeoutExpired:
            status = 124
    return {"phase": name, "seconds": round(time.monotonic() - started, 3),
            "status": status, "log": str(log)}


def verify_staging(root, vpk):
    """Validate the whole staged profile before any window is opened."""
    required = [
        root / FIRMWARE / "vs0" / "vsh",
        root / FIRMWARE / "sa0",
        root / FIRMWARE / "ur0/data/libshacccg.suprx",
        root / "runtime/Vita3K",
        root / "runtime" / vpk.name,
        root / UX0_DATA,
        root / "config/Vita3K/config.yml",
    ]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise SystemExit("staging incomplete: " + ", ".join(missing))
    config = (root / "config/Vita3K/config.yml").read_text()
    for key, value in (("initial-setup", "false"), ("show-welcome", "false"),
                       ("check-for-updates-mode", "0"),
                       ("warn-missing-firmware", "false")):
        if f"{key}: {value}" not in config:
            raise SystemExit(f"staging config not ready: {key} is not {value}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=os.environ.get("WW02_BUILD", str(DEFAULT_BUILD)))
    parser.add_argument("--stack", default=str(DEFAULT_STACK))
    parser.add_argument("--vpk", default=None)
    parser.add_argument("--timeout", type=float, default=PHASE_TIMEOUT)
    parser.add_argument("--fixture", type=Path, help="validated private extracted fixture to stage")
    parser.add_argument("--watched", action="store_true", help="monitor logs/CPU and abort silent hangs")
    parser.add_argument("--surface-sync", action="store_true", help="enable GPU surface writeback for capture")
    parser.add_argument("--share-readonly-stack", action="store_true", help="deduplicate immutable firmware/runtime bytes only")
    parser.add_argument("--wayland-socket", type=Path,
                        default=Path(f"/run/user/{os.getuid()}/wayland-0"))
    args = parser.parse_args()
    try:
        validate_timeout(args.timeout)
    except ValueError as error:
        parser.error(str(error))
    phase_runner = run_phase
    if args.watched:
        import watch
        phase_runner = watch.run_phase

    build = Path(args.output)
    stack = Path(args.stack)
    if not args.wayland_socket.exists():
        raise SystemExit(f"missing Wayland socket: {args.wayland_socket}")
    vpk = Path(args.vpk) if args.vpk else build_vpk(build)
    if not vpk.is_file():
        raise SystemExit(f"missing demo VPK: {vpk}")

    root = build / f"vita3k-run-{uuid.uuid4().hex}"
    root.mkdir(mode=0o700)
    for name in (*WRITABLE_DIRECTORIES, "evidence", "phases", "config-system", "data-system"):
        (root / name).mkdir(mode=0o700)
    copy_static = shutil.copy2
    if args.share_readonly_stack:
        copy_static = lambda source, destination: copy_readonly(source, destination, build / 'readonly-stack-cache')
    for name in ("vs0", "sa0"):
        shutil.copytree(stack / "firmware" / name, root / FIRMWARE / name, symlinks=True, copy_function=copy_static)
    # The Cg runtime compiler, staged privately for the profile only.
    shacccg_source = SHADER_COMPILER
    if not shacccg_source.is_file():
        raise SystemExit(f"missing libshacccg.suprx at {shacccg_source}")
    ur0_data = root / FIRMWARE / "ur0/data"
    ur0_data.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(shacccg_source, ur0_data / "libshacccg.suprx")
    (root / UX0_DATA).mkdir(parents=True, exist_ok=True)
    if args.fixture is not None:
        from image import validate_fixture
        validate_fixture(args.fixture, json.loads((args.fixture / 'capture.json').read_text()))
        for name in ('stream.fifo', 'mem1.bin', 'mem1.ranges'):
            source = args.fixture / name
            if source.is_file():
                shutil.copyfile(source, root / UX0_DATA / name)
    shutil.copytree(stack / "vita3k", root / "runtime", copy_function=copy_static)
    shutil.copyfile(vpk, root / "runtime" / vpk.name)
    config_dir = root / "config/Vita3K"
    config_dir.mkdir(parents=True, exist_ok=True)
    (config_dir / "config.yml").write_text(
        "---\ninitial-setup: false\nshow-welcome: false\n"
        "check-for-updates-mode: 0\nwarn-missing-firmware: false\n"
        "backend-renderer: OpenGL\n"
        "pref-path: /run/ww03/data/Vita3K/Vita3K\nuser-id: 00\n"
        "user-name: BlueWake\ncpu-opt: true\ndiscord-rich-presence: false\n")
    if args.surface_sync:
        path = config_dir / 'config.yml'
        path.write_text(surface_sync_config(path.read_text()))
    verify_staging(root, vpk)

    phases = [phase_runner(root, stack, "install",
                        ["--console", "--backend-renderer", "OpenGL", vpk.name],
                        args.timeout, args.wayland_socket)]
    # The install phase rewrites the config with the stock welcome/update
    # defaults, which stop the CLI from starting the content: restore them.
    config_path = root / "config/Vita3K/config.yml"
    text = config_path.read_text()
    text = text.replace("show-welcome: true", "show-welcome: false")
    text = text.replace("check-for-updates-mode: 1", "check-for-updates-mode: 0")
    text = text.replace("warn-missing-firmware: true",
                        "warn-missing-firmware: false")
    if "warn-missing-firmware" not in text:
        text += "warn-missing-firmware: false\n"
    if args.surface_sync:
        text = surface_sync_config(text)
    config_path.write_text(text)
    verify_staging(root, vpk)
    phases.append(phase_runner(root, stack, "run",
                            ["--backend-renderer", "OpenGL", "--installed-path", TITLE_ID],
                            args.timeout, args.wayland_socket))

    report_path = root / UX0_DATA / "report.txt"
    capture_path = root / UX0_DATA / "capture.png"
    report_lines = []
    if report_path.is_file():
        report_lines = [line for line in report_path.read_text(errors="replace").splitlines()
                        if line.startswith(REPORT_PREFIXES)]
    done = [line for line in report_lines if line.startswith("demo done")]
    code = int(done[0].split()[-1]) if done else 1
    status = {0: "green", 2: "path-ok-pixels-blocked"}.get(code, "failed")
    ok = code in (0, 2)
    evidence = {"report": None, "capture": None}
    if report_path.is_file():
        shutil.copyfile(report_path, root / "evidence/report.txt")
        evidence["report"] = "evidence/report.txt"
    if capture_path.is_file():
        shutil.copyfile(capture_path, root / "evidence/capture.png")
        evidence["capture"] = "evidence/capture.png"
    raw_capture = root / UX0_DATA / 'capture-rgba.png'
    if raw_capture.is_file():
        shutil.copyfile(raw_capture, root / 'evidence/capture-rgba.png')
        evidence['capture_rgba'] = 'evidence/capture-rgba.png'

    result = {"ticket": "VITA-WW-02", "run": root.name.removeprefix("vita3k-run-"),
              "title": TITLE_ID, "ok": ok, "status": status,
              "vpk": {"path": str(vpk), "sha256": sha256(vpk)},
              "phases": phases, "evidence": evidence,
              "report_lines": report_lines}
    result['readonly_stack_shared'] = args.share_readonly_stack
    (root / "evidence" / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"run": result["run"], "ok": ok, "status": status,
                      "evidence": evidence, "report_lines": report_lines},
                     indent=2))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
