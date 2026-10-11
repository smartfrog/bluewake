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
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD = Path("/home/fred/projects/zeldaWWHDRecomp/build-f2/ww02")
DEFAULT_STACK = Path("/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack")
CLOUDPUNK_STACK = Path("/home/fred/cloudpunk-vita/tools")
TITLE_ID = "BWWG00002"
FIRMWARE = Path("data/Vita3K/Vita3K")
UX0_DATA = FIRMWARE / "ux0/data/ww02"
WRITABLE_DIRECTORIES = ("home", "config", "cache", "data", "state", "work", "xdg-runtime")
PHASE_TIMEOUT = 90.0
REPORT_PREFIXES = ("ww02 ", "gl version", "scenario ", "draw ", "plan skipped",
                   "program refused", "readback", "capture written", "copy ",
                   "draws=", "demo done", "FAIL")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def find_recompcore():
    candidate = ROOT / "ref/vita-ww-01/RecompCore"
    if (candidate / "GXRuntime").is_dir():
        return candidate
    return Path("/home/fred/projects/zeldaWWHDRecomp/ref/vita-ww-01/RecompCore")


def build_vpk(build):
    vitasdk = CLOUDPUNK_STACK / "vitasdk"
    env = dict(os.environ, VITASDK=str(vitasdk))
    vpk_dir = build / "gx-vita"
    vpk_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cmake", "-G", "Ninja", "-S", str(ROOT / "vita/feasibility/gx"),
                    "-B", str(vpk_dir),
                    f"-DCMAKE_TOOLCHAIN_FILE={vitasdk}/share/vita.toolchain.cmake",
                    f"-DRECOMPCORE_DIR={find_recompcore()}",
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=os.environ.get("WW02_BUILD", str(DEFAULT_BUILD)))
    parser.add_argument("--stack", default=str(DEFAULT_STACK))
    parser.add_argument("--vpk", default=None)
    parser.add_argument("--timeout", type=float, default=PHASE_TIMEOUT)
    parser.add_argument("--wayland-socket", type=Path,
                        default=Path(f"/run/user/{os.getuid()}/wayland-0"))
    args = parser.parse_args()

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
    for name in ("vs0", "sa0"):
        shutil.copytree(stack / "firmware" / name, root / FIRMWARE / name, symlinks=True)
    # The Cg runtime compiler, staged privately for the profile only.
    shacccg_source = CLOUDPUNK_STACK / "vita3k/data/Vita3K/Vita3K/ur0/data/libshacccg.suprx"
    if not shacccg_source.is_file():
        raise SystemExit(f"missing libshacccg.suprx at {shacccg_source}")
    ur0_data = root / FIRMWARE / "ur0/data"
    ur0_data.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(shacccg_source, ur0_data / "libshacccg.suprx")
    (root / UX0_DATA).mkdir(parents=True, exist_ok=True)
    shutil.copytree(stack / "vita3k", root / "runtime")
    shutil.copyfile(vpk, root / "runtime" / vpk.name)
    config_dir = root / "config/Vita3K"
    config_dir.mkdir(parents=True, exist_ok=True)
    (config_dir / "config.yml").write_text(
        "---\ninitial-setup: false\nshow-welcome: false\n"
        "check-for-updates-mode: 0\nbackend-renderer: OpenGL\n"
        "pref-path: /run/ww03/data/Vita3K/Vita3K\nuser-id: 00\n"
        "user-name: BlueWake\ncpu-opt: true\ndiscord-rich-presence: false\n")

    phases = [run_phase(root, stack, "install",
                        ["--console", "--backend-renderer", "OpenGL", vpk.name],
                        args.timeout, args.wayland_socket),
              run_phase(root, stack, "run",
                        ["--backend-renderer", "OpenGL", "--installed-path", TITLE_ID],
                        args.timeout, args.wayland_socket)]

    report_path = root / UX0_DATA / "report.txt"
    capture_path = root / UX0_DATA / "capture.png"
    report_lines = []
    if report_path.is_file():
        report_lines = [line for line in report_path.read_text(errors="replace").splitlines()
                        if line.startswith(REPORT_PREFIXES)]
    ok = any(line == "demo done 0" for line in report_lines)
    evidence = {"report": None, "capture": None}
    if report_path.is_file():
        shutil.copyfile(report_path, root / "evidence/report.txt")
        evidence["report"] = "evidence/report.txt"
    if capture_path.is_file():
        shutil.copyfile(capture_path, root / "evidence/capture.png")
        evidence["capture"] = "evidence/capture.png"

    result = {"ticket": "VITA-WW-02", "run": root.name.removeprefix("vita3k-run-"),
              "title": TITLE_ID, "ok": ok,
              "vpk": {"path": str(vpk), "sha256": sha256(vpk)},
              "phases": phases, "evidence": evidence,
              "report_lines": report_lines}
    (root / "evidence" / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"run": result["run"], "ok": ok, "evidence": evidence,
                      "report_lines": report_lines}, indent=2))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
