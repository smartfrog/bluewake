#!/usr/bin/env python3
"""E2 isolated Vita3K run for the E1 skeleton smoke (cos_sdk_smoke, BWWG00031).

Fresh private profile per run (never shared, never reused), no network, bwrap
containment, firmware vs0/sa0 staged from the local stack, and libshacccg.suprx
staged privately from the Cloudpunk stack (a system file owned locally, never
committed or published). Both phases run in console mode: Vita3K relays a guest
tty0 write to its own stdout there (io.redirect_stdio = cfg.console, upstream
bbd5c36) and the smoke's stdio is tty0-backed (newlib _init_vita_io), so the
guest's verdict lines land in the phase log. The run phase stops at the smoke's
verdict (an exact "ok" line, or its FAIL lines), at a fault marker, at silence
with near-zero CPU for 30 s, or at the phase ceiling.

The run must live inside a fresh transient systemd unit (the run ritual). This
launcher verifies MemoryMax, MemorySwapMax, OOMPolicy and CPU accounting from
inside that unit, and takes the one-shot MemAvailable admission reading, before
any emulator starts. Outside such a unit it refuses to run.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_STACK = Path("/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack")
DEFAULT_OUTPUT = Path("/home/fred/projects/zeldaWWHDRecomp/build-f2/e2-skeleton")
CLOUDPUNK_STACK = Path("/home/fred/cloudpunk-vita/tools")
TITLE_ID = "BWWG00031"
FIRMWARE = Path("data/Vita3K/Vita3K")
UX0_APP = FIRMWARE / "ux0/app" / TITLE_ID
WRITABLE_DIRECTORIES = ("home", "config", "cache", "data", "state", "work", "xdg-runtime")
PHASE_TIMEOUT = 90.0
UNIT_MEMORY_MAX = 6442450944  # 6 GiB: the run ritual's cap for the unit
ADMISSION_BYTES = 6442450944  # the run ritual's one-shot MemAvailable floor
HANG_SECONDS = 30.0
FAULT_MARKERS = ("PC is 0x0", "|E| [MemoryWrite]: Invalid write", "Unhandled SIGSEGV")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_timeout(timeout):
    if not math.isfinite(timeout) or not 0 < timeout <= 600:
        raise ValueError("each phase deadline must be finite, positive and at most 600 seconds")


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
        command.extend(["--ro-bind", str(wayland_socket), "/run/ww03/xdg-runtime/wayland-0"])
        environment["QT_QPA_PLATFORM"] = "wayland"
        environment["WAYLAND_DISPLAY"] = "wayland-0"
    else:
        environment["QT_QPA_PLATFORM"] = "offscreen"
        environment["SDL_VIDEODRIVER"] = "offscreen"
    for name, value in environment.items():
        command.extend(["--setenv", name, value])
    return [*command, "/stack/vita3k/Vita3K", *arguments]


def descendants(parent):
    entries = {}
    for path in Path("/proc").glob("[0-9]*/stat"):
        try:
            text = path.read_text()
            rest = text[text.rfind(")") + 2:].split()
            entries[int(path.parent.name)] = (int(rest[1]), int(rest[11]) + int(rest[12]), text)
        except (FileNotFoundError, ProcessLookupError):
            continue
    owned = {parent}
    while True:
        added = {pid for pid, entry in entries.items() if entry[0] in owned} - owned
        if not added:
            return {pid: entries[pid] for pid in owned if pid in entries}
        owned.update(added)


def stop_owned(process):
    for pid in descendants(process.pid):
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    try:
        return process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        for pid in descendants(process.pid):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        return process.wait(timeout=5)


def verdict_lines(log):
    """The smoke's own lines in the phase log: the raw tty0 relay writes them
    without the emulator's timestamp prefix, so a whole line is the guest's."""
    ok, failures, journal, skips = None, [], [], []
    text = log.read_text(errors="replace")
    for line in text.splitlines():
        stripped = line.strip()
        if stripped == "ok":
            ok = line
        elif stripped.startswith("cos_sdk_smoke:") or "check failed:" in stripped:
            failures.append(stripped)
        elif stripped.startswith("[cos_sdk]"):
            journal.append(stripped)
        elif "skipped" in stripped:
            skips.append(stripped)
    return ok, failures, journal, skips


def run_phase(root, stack, name, arguments, timeout, wayland_socket, verdict_on_ok):
    log = root / "phases" / f"{name}.log"
    report_app = root / UX0_APP
    started = time.monotonic()
    previous, previous_size, idle_since = {}, 0, None
    reason = "exited"
    with log.open("wb") as output, (root / "evidence" / f"{name}-watch.jsonl").open("w") as watch:
        process = subprocess.Popen(sandbox_command(root, stack, arguments, wayland_socket),
                                   cwd=root / "work", stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        while process.poll() is None:
            now = time.monotonic()
            emulators = {pid: entry[1] for pid, entry in descendants(process.pid).items()
                         if "(Vita3K)" in entry[2]}
            delta = sum(ticks - previous.get(pid, ticks) for pid, ticks in emulators.items())
            cpu = delta / os.sysconf("SC_CLK_TCK") * 100 / 0.5
            previous = emulators
            size = log.stat().st_size
            watch.write(json.dumps(dict(seconds=round(now - started, 2), cpu_percent=round(cpu, 2),
                                        pids=list(emulators), log_bytes=size)) + "\n")
            watch.flush()
            with log.open("rb") as recent_log:
                recent_log.seek(max(0, size - 65536))
                recent = recent_log.read().decode(errors="replace")
            if any(marker in recent for marker in FAULT_MARKERS):
                reason = "emulator-fault"
                status = stop_owned(process)
                break
            if verdict_on_ok:
                ok, failures, _, _ = verdict_lines(log)
                if ok is not None or failures:
                    reason = "verdict-ok" if ok is not None else "verdict-fail"
                    time.sleep(1.0)
                    status = stop_owned(process)
                    break
                if "Game closed:" in recent:
                    reason = "game-closed-without-verdict"
                    status = stop_owned(process)
                    break
            elif name == "install":
                with zipfile.ZipFile(root / "runtime" / arguments[-1]) as archive:
                    installed = all((report_app / entry.filename).is_file() and
                                    (report_app / entry.filename).stat().st_size == entry.file_size
                                    for entry in archive.infolist() if not entry.is_dir())
                if installed:
                    reason = "vpk-files-installed"
                    status = stop_owned(process)
                    break
            if emulators and cpu < 5 and size == previous_size:
                idle_since = idle_since if idle_since is not None else now
                if now - idle_since > HANG_SECONDS:
                    reason = "hung-low-cpu-silent"
                    status = stop_owned(process)
                    break
            else:
                idle_since = None
            previous_size = size
            if now - started > timeout:
                reason = "healthy-run-ceiling" if cpu >= 5 else "ceiling"
                status = stop_owned(process)
                break
            time.sleep(0.5)
        else:
            status = process.returncode
    return dict(phase=name, seconds=round(time.monotonic() - started, 3), status=status,
                reason=reason, log=str(log))


def unit_gate(root):
    """Verify the transient unit's envelope from inside, before any emulator."""
    cgroup = Path("/proc/self/cgroup").read_text().strip()
    unit = cgroup.rsplit("/", 1)[-1]
    record = {"cgroup": cgroup, "unit": unit}
    if not unit.endswith(".service"):
        raise SystemExit(f"blocked: not inside a transient .service unit (cgroup {cgroup})")
    properties = {}
    for name in ("MemoryMax", "EffectiveMemoryMax", "MemorySwapMax", "OOMPolicy",
                 "CPUAccounting", "MemoryAccounting", "MemoryCurrent", "CPUUsageNSec"):
        result = subprocess.run(["systemctl", "--user", "show", unit, "-p", name, "--value"],
                                capture_output=True, text=True, check=True)
        properties[name] = result.stdout.strip()
    record["systemd"] = properties
    memory_max = Path("/sys/fs/cgroup" + cgroup + "/memory.max").read_text().strip()
    swap_max = Path("/sys/fs/cgroup" + cgroup + "/memory.swap.max").read_text().strip()
    record["kernel"] = {"memory.max": memory_max, "memory.swap.max": swap_max}
    checks = {
        "MemoryMax": properties["MemoryMax"] == str(UNIT_MEMORY_MAX),
        "EffectiveMemoryMax": properties["EffectiveMemoryMax"] == str(UNIT_MEMORY_MAX),
        "MemorySwapMax": properties["MemorySwapMax"] == "0",
        "OOMPolicy": properties["OOMPolicy"] == "stop",
        "CPUAccounting": properties["CPUAccounting"] == "yes",
        "kernel memory.max": memory_max == str(UNIT_MEMORY_MAX),
        "kernel memory.swap.max": swap_max == "0",
    }
    record["checks"] = checks
    (root / "evidence" / "unit-properties.json").write_text(json.dumps(record, indent=2) + "\n")
    if not all(checks.values()):
        raise SystemExit(f"blocked: unit envelope check failed: {checks}")
    return record


def admission(root):
    for line in Path("/proc/meminfo").read_text().splitlines():
        if line.startswith("MemAvailable:"):
            available = int(line.split()[1]) * 1024
            break
    record = {"mem_available_bytes": available, "required_bytes": ADMISSION_BYTES,
              "ok": available >= ADMISSION_BYTES, "source": "/proc/meminfo (one reading)"}
    (root / "evidence" / "admission.json").write_text(json.dumps(record, indent=2) + "\n")
    if not record["ok"]:
        raise SystemExit(f"blocked: MemAvailable {available} B below {ADMISSION_BYTES} B")
    return record


def verify_staging(root, vpk):
    required = [
        root / FIRMWARE / "vs0" / "vsh",
        root / FIRMWARE / "sa0",
        root / FIRMWARE / "ur0/data/libshacccg.suprx",
        root / "runtime/Vita3K",
        root / "runtime" / vpk.name,
        root / UX0_APP.parent,
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
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    parser.add_argument("--stack", default=str(DEFAULT_STACK))
    parser.add_argument("--vpk", required=True)
    parser.add_argument("--vpk-sha256", required=True)
    parser.add_argument("--timeout", type=float, default=PHASE_TIMEOUT)
    parser.add_argument("--wayland-socket", type=Path,
                        default=Path(f"/run/user/{os.getuid()}/wayland-0"))
    args = parser.parse_args()
    try:
        validate_timeout(args.timeout)
    except ValueError as error:
        parser.error(str(error))

    build = Path(args.output)
    stack = Path(args.stack)
    vpk = Path(args.vpk)
    if not vpk.is_file():
        raise SystemExit(f"missing VPK: {vpk}")
    if sha256(vpk) != args.vpk_sha256:
        raise SystemExit(f"VPK SHA-256 mismatch: {vpk}")
    wayland_socket = args.wayland_socket if args.wayland_socket.exists() else None

    root = build / f"vita3k-run-{uuid.uuid4().hex}"
    root.mkdir(parents=True, mode=0o700)
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
    (root / UX0_APP.parent).mkdir(parents=True, exist_ok=True)
    shutil.copytree(stack / "vita3k", root / "runtime")
    shutil.copyfile(vpk, root / "runtime" / vpk.name)
    config_dir = root / "config/Vita3K"
    config_dir.mkdir(parents=True, exist_ok=True)
    (config_dir / "config.yml").write_text(
        "---\ninitial-setup: false\nshow-welcome: false\n"
        "check-for-updates-mode: 0\nwarn-missing-firmware: false\n"
        "backend-renderer: OpenGL\n"
        "pref-path: /run/ww03/data/Vita3K/Vita3K\nuser-id: 00\n"
        "user-name: BlueWake\ncpu-opt: true\ndiscord-rich-presence: false\n")
    verify_staging(root, vpk)

    unit = unit_gate(root)
    admitted = admission(root)

    phases = [run_phase(root, stack, "install",
                        ["--console", "--backend-renderer", "OpenGL", vpk.name],
                        args.timeout, wayland_socket, verdict_on_ok=False)]
    # The install phase rewrites the config with the stock welcome/update
    # defaults, which stop the CLI from starting the content: restore them.
    config_path = root / "config/Vita3K/config.yml"
    text = config_path.read_text()
    text = text.replace("show-welcome: true", "show-welcome: false")
    text = text.replace("check-for-updates-mode: 1", "check-for-updates-mode: 0")
    text = text.replace("warn-missing-firmware: true", "warn-missing-firmware: false")
    if "warn-missing-firmware" not in text:
        text += "warn-missing-firmware: false\n"
    config_path.write_text(text)
    verify_staging(root, vpk)
    phases.append(run_phase(root, stack, "run",
                            ["--console", "--backend-renderer", "OpenGL",
                             "--installed-path", TITLE_ID],
                            args.timeout, wayland_socket, verdict_on_ok=True))

    install_log = Path(phases[0]["log"])
    installed = (root / UX0_APP / "eboot.bin").is_file()
    run_log = Path(phases[1]["log"])
    ok_line, failures, journal, skips = verdict_lines(run_log)
    if ok_line is not None:
        verdict = {"status": "ok", "line": ok_line.strip()}
    elif failures:
        verdict = {"status": "fail", "lines": failures[:20]}
    elif phases[1]["reason"] == "hung-low-cpu-silent":
        verdict = {"status": "hang"}
    elif phases[1]["reason"] == "emulator-fault":
        verdict = {"status": "fault", "lines": [m for m in FAULT_MARKERS
                                                if m in run_log.read_text(errors="replace")]}
    else:
        verdict = {"status": "no-verdict"}
    result = {"ticket": "E1-E2", "run": root.name.removeprefix("vita3k-run-"),
              "title": TITLE_ID, "vpk": {"path": str(vpk), "sha256": args.vpk_sha256},
              "unit": {"name": unit["unit"], "memory_max": unit["systemd"]["MemoryMax"],
                       "memory_swap_max": unit["systemd"]["MemorySwapMax"],
                       "oom_policy": unit["systemd"]["OOMPolicy"],
                       "cpu_accounting": unit["systemd"]["CPUAccounting"]},
              "admission": admitted, "phases": phases,
              "install": {"installed": installed, "log": str(install_log)},
              "verdict": verdict, "guest_journal": journal[:20], "guest_skips": skips[:20],
              "wayland_socket": str(wayland_socket) if wayland_socket else None}
    (root / "evidence" / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"run": result["run"], "verdict": verdict, "phases": phases,
                      "admission_bytes": admitted["mem_available_bytes"]}, indent=2))
    return 0 if verdict["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
