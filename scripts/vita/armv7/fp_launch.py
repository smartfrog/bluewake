#!/usr/bin/env python3
"""Isolated VITA-WW-03 launcher. Execution requires the external pre-launch review gate."""

import argparse
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import shutil
import signal
import stat
import subprocess
import time
import uuid
import zipfile

from build import ROOT, build_directory
from fp_build import sha256
from fp_libraries import library_identity
from fp_profile import FIRMWARE_DIRECTORY, stage_firmware, verify_firmware
from fp_report import validate
from fp_vectors import cases

REPORT_DIRECTORY = Path("data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture")
WRITABLE_DIRECTORIES = ("home", "config", "cache", "data", "state", "work", "xdg-runtime")
MAX_REPORT_BYTES = 65536


def reject_symlinks(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink():
            raise ValueError(f"unexpected symlink: {part}")
    return path


def create_run(root):
    reject_symlinks(root)
    root.mkdir(mode=0o700)
    for child in (*WRITABLE_DIRECTORIES, "evidence", "runtime", "config-system", "data-system"):
        (root / child).mkdir(mode=0o700)


@contextmanager
def directory_fd(path):
    descriptor = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for component in Path(path).absolute().parts[1:]:
            child = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                            dir_fd=descriptor)
            os.close(descriptor)
            descriptor = child
        yield descriptor
    finally:
        os.close(descriptor)


@contextmanager
def evidence_file(root, name):
    with directory_fd(root / "evidence") as parent:
        descriptor = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                             0o600, dir_fd=parent)
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        yield stream


def sandbox_command(root, executable, arguments, libraries=None, wayland_socket=None):
    root = reject_symlinks(root)
    if Path(executable).name != executable:
        raise ValueError("executable must be a basename")
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
        firmware = root / FIRMWARE_DIRECTORY / name
        if firmware.exists():
            reject_symlinks(firmware)
            command.extend(["--ro-bind", str(firmware), "/run/ww03/" + str(FIRMWARE_DIRECTORY / name)])
    environment = {"PATH": "/usr/bin", "LANG": "C.UTF-8", "HOME": "/run/ww03/home",
                   "XDG_CONFIG_HOME": "/run/ww03/config", "XDG_CACHE_HOME": "/run/ww03/cache",
                   "XDG_DATA_HOME": "/run/ww03/data", "XDG_STATE_HOME": "/run/ww03/state",
                   "XDG_RUNTIME_DIR": "/run/ww03/xdg-runtime",
                   "XDG_CONFIG_DIRS": "/run/ww03/config-system",
                   "XDG_DATA_DIRS": "/run/ww03/data-system",
                   "QT_QPA_PLATFORM": "offscreen", "SDL_VIDEODRIVER": "offscreen",
                   "SDL_AUDIODRIVER": "dummy", "LIBGL_ALWAYS_SOFTWARE": "1",
                   "FONTCONFIG_FILE": "/etc/fonts/fonts.conf"}
    if libraries is not None:
        libraries = reject_symlinks(libraries)
        command.extend(["--ro-bind", str(libraries), "/stack/vita3k-env"])
        environment["LD_LIBRARY_PATH"] = "/stack/vita3k-env/lib"
        environment["QT_PLUGIN_PATH"] = "/stack/vita3k-env/lib/qt6/plugins"
    if wayland_socket is not None:
        wayland_socket = reject_symlinks(wayland_socket)
        info = wayland_socket.stat()
        if not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid():
            raise ValueError("display must be an existing Unix socket owned by the current user")
        command.extend(["--ro-bind", str(wayland_socket), "/run/ww03/xdg-runtime/wayland-0"])
        environment["QT_QPA_PLATFORM"] = "wayland"
        environment["WAYLAND_DISPLAY"] = "wayland-0"
        del environment["SDL_VIDEODRIVER"]
    for name, value in environment.items():
        command.extend(["--setenv", name, value])
    return [*command, "/stack/vita3k/" + executable, *arguments]


def run_owned(command, root, timeout, report_identity=None):
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout must be finite and positive")
    if resource.getrlimit(resource.RLIMIT_FSIZE) != (resource.RLIM_INFINITY, resource.RLIM_INFINITY):
        raise ValueError("RLIMIT_FSIZE must be unlimited; inherited limits can break runtime memfd allocation")
    started = time.monotonic()
    deadline = started + timeout

    def remaining_time():
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(command, timeout)
        return remaining

    record = {"stop_reason": "exit"}
    with evidence_file(root, "process.log") as log:
        with subprocess.Popen(command, cwd=root / "work", stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                              env={"PATH": "/usr/bin", "LANG": "C.UTF-8"},
                              start_new_session=True) as process:
            try:
                if report_identity is None:
                    status = process.wait(timeout=remaining_time())
                else:
                    while True:
                        remaining_time()
                        status = process.poll()
                        remaining_time()
                        if status is not None:
                            break
                        try:
                            content = read_report(root)
                        except FileNotFoundError:
                            pass
                        except (OSError, ValueError) as error:
                            record.update(stop_reason="invalid_report", report_error=str(error))
                            status = 2
                            break
                        else:
                            try:
                                validate(content.decode("utf-8"), *report_identity, cases())
                            except ValueError as error:
                                record.update(stop_reason="invalid_report", report_error=str(error))
                                status = 2
                            else:
                                record["report_sha256_at_stop"] = hashlib.sha256(content).hexdigest()
                                grace = min(.25, remaining_time())
                                try:
                                    status = process.wait(timeout=grace)
                                except subprocess.TimeoutExpired:
                                    record["stop_reason"] = "validated_report"
                                    status = 125
                            break
                        time.sleep(min(.05, remaining_time()))
                remaining_time()
            except subprocess.TimeoutExpired:
                record["stop_reason"] = "timeout"
                status = 124
            except BaseException:
                record["stop_reason"] = "observer_error"
                raise
            finally:
                if process.poll() is None:
                    try:
                        os.killpg(process.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
                record.update(pid=process.pid, exit=process.returncode, waited=True,
                              duration_seconds=time.monotonic() - started)
                with evidence_file(root, "process.json") as stream:
                    stream.write(json.dumps(record, indent=2) + "\n")
            return status


def stage_runtime(root, executable, expected_hash):
    executable = reject_symlinks(executable)
    with executable.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            raise ValueError("runtime must be the installed ELF, not a wrapper")
    if sha256(executable) != expected_hash:
        raise ValueError("runtime hash mismatch")
    destination = root / "runtime"
    if any(destination.iterdir()):
        raise FileExistsError("runtime stage is not empty")
    shutil.copyfile(executable, destination / "Vita3K")
    if sha256(destination / "Vita3K") != expected_hash:
        raise ValueError("runtime changed while staging")
    (destination / "Vita3K").chmod(0o500)
    for name in ("data", "icons", "shaders-builtin", "translations"):
        source = executable.parent / name
        reject_symlinks(source)
        if not source.is_dir():
            raise ValueError(f"missing public runtime asset directory: {source}")
        for entry in source.rglob("*"):
            if entry.is_symlink() or not (entry.is_file() or entry.is_dir()):
                raise ValueError(f"unexpected runtime asset: {entry}")
        shutil.copytree(source, destination / name)
    return {str(path.relative_to(destination)): sha256(path)
            for path in sorted(destination.rglob("*")) if path.is_file()}


def read_report(root):
    with directory_fd(root / REPORT_DIRECTORY) as parent:
        descriptor = os.open("fp.report", os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW | os.O_CLOEXEC,
                             dir_fd=parent)
    with os.fdopen(descriptor, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise ValueError("report must be a regular file")
        if info.st_size > MAX_REPORT_BYTES:
            raise ValueError("report exceeds size limit")
        content = stream.read(MAX_REPORT_BYTES + 1)
        if len(content) > MAX_REPORT_BYTES:
            raise ValueError("report exceeds size limit")
        if len(content) != info.st_size:
            raise ValueError("report size changed while reading")
        return content


def collect_result(root, token, source, package, process_status, identity_error=None, process=None):
    record = {"process_exit": process["exit"] if process else process_status,
              "completion": "INCOMPLETE", "semantic": "UNAVAILABLE"}
    if process:
        record["process"] = process
    if identity_error:
        record["identity_error"] = identity_error
    try:
        content = read_report(root)
        semantic = validate(content.decode("utf-8"), token, source, package, cases())
        report_hash = hashlib.sha256(content).hexdigest()
        if process and process.get("report_sha256_at_stop", report_hash) != report_hash:
            raise ValueError("report changed after completion observation")
        record["reported_semantic"] = "FAIL" if semantic else "PASS"
        record["report_sha256"] = report_hash
        if process_status == 0 and not identity_error:
            record["completion"] = "COMPLETE"
            record["semantic"] = record["reported_semantic"]
        status = process_status or (2 if identity_error else semantic)
    except (OSError, ValueError) as error:
        record["report_error"] = str(error)
        status = process_status or 2
    record["status"] = status
    with evidence_file(root, "result.json") as stream:
        stream.write(json.dumps(record, indent=2) + "\n")
    return status


def verify_package(path, expected_hash):
    reject_symlinks(path)
    if sha256(path) != expected_hash:
        raise ValueError("VPK no longer matches build evidence")
    with zipfile.ZipFile(path) as archive:
        if sorted(archive.namelist()) != ["eboot.bin", "sce_sys/param.sfo"] or archive.testzip():
            raise ValueError("unexpected VPK content")
        if b"BWWF00003\0" not in archive.read("sce_sys/param.sfo"):
            raise ValueError("not the FP diagnostic title")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True, help="installed ELF; never a wrapper")
    parser.add_argument("--runtime-sha256", required=True)
    parser.add_argument("--runtime-source-revision", required=True,
                        help="observed source label only; executable source mapping remains UNVERIFIED")
    parser.add_argument("--libraries", type=Path, required=True, help="read-only public runtime libraries")
    parser.add_argument("--firmware", type=Path, help="copy only installed public system vs0/sa0 into the new profile")
    parser.add_argument("--wayland-socket", type=Path, help="explicitly authorized display socket only; no session bus")
    parser.add_argument("--build-evidence", type=Path, default=ROOT / "build-vita-ww-03/fp-evidence.json")
    parser.add_argument("--out", type=Path, default=ROOT / "build-vita-ww-03")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--execute-reviewed", action="store_true",
                        help="ONLY after independent review and coordinator authorization; default stages only")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or not 0 < args.timeout <= 30:
        parser.error("timeout must be finite, positive and at most 30 seconds")
    parent = build_directory(reject_symlinks(args.out))
    evidence = json.loads(args.build_evidence.read_text())
    source = evidence["source"]
    artifacts = [(Path(name), entry["sha256"]) for name, entry in evidence["artifacts"].items()
                 if name.endswith("/vita-fp/ww_fp.vpk")]
    if len(artifacts) != 1:
        raise ValueError("expected exactly one FP VPK in build evidence")
    vpk, package = artifacts[0]
    verify_package(vpk, package)
    token = uuid.uuid4().hex
    root = parent / ("vita3k-run-" + token)
    create_run(root)
    assets = stage_runtime(root, args.runtime, args.runtime_sha256)
    staged_vpk = root / "runtime/fp.vpk"
    with vpk.open("rb") as source_file, staged_vpk.open("xb") as destination:
        shutil.copyfileobj(source_file, destination)
    verify_package(staged_vpk, package)
    firmware = stage_firmware(root, reject_symlinks(args.firmware)) if args.firmware else {}
    directory = root / REPORT_DIRECTORY
    directory.mkdir(parents=True)
    (directory / "fp.request").write_text(f"{token} {source} {package}\n")
    display = {"wayland_socket": args.wayland_socket} if args.wayland_socket else {}
    command = sandbox_command(root, "Vita3K", ["--console", "--backend-renderer", "OpenGL",
                                                "fp.vpk"], args.libraries, **display)
    libraries_before = library_identity(args.libraries)
    with evidence_file(root, "libraries-before.json") as stream:
        stream.write(json.dumps(libraries_before, indent=2) + "\n")
    plan = {"run": token, "source": source, "vpk_sha256": package,
            "runtime_sha256": args.runtime_sha256, "runtime_source": args.runtime_source_revision,
            "runtime_source_mapping": "UNVERIFIED",
            "wayland_socket": str(args.wayland_socket) if args.wayland_socket else None,
            "firmware": firmware, "firmware_provenance": "installed system trees; version/PUP UNVERIFIED",
            "runtime_assets": assets,
            "runtime_libraries": {"root": libraries_before["root"], "sha256": libraries_before["sha256"],
                                  "manifest": "libraries-before.json"},
            "command": command, "cwd": str(root / "work"),
            "sandbox_cwd": "/stack/vita3k", "completion_policy": "forced shutdown remains INCOMPLETE/status 125",
            "report_poll_seconds": .05, "report_exit_grace_seconds": .25,
            "RLIMIT_FSIZE": resource.getrlimit(resource.RLIMIT_FSIZE),
            "timeout_seconds": args.timeout, "real_execution": args.execute_reviewed}
    with evidence_file(root, "plan.json") as stream:
        stream.write(json.dumps(plan, indent=2) + "\n")
    print(root, flush=True)
    if not args.execute_reviewed:
        print("STAGED ONLY: real Vita3K NOT RUN; review gate still required")
        return 0
    status = run_owned(command, root, args.timeout, report_identity=(token, source, package))
    process = json.loads((root / "evidence/process.json").read_text())
    identity_error = None
    try:
        libraries_after = library_identity(args.libraries)
        with evidence_file(root, "libraries-after.json") as stream:
            stream.write(json.dumps(libraries_after, indent=2) + "\n")
        if libraries_after != libraries_before:
            identity_error = "public library tree changed during execution"
        verify_firmware(firmware)
    except (OSError, ValueError) as error:
        identity_error = f"cannot verify runtime inputs after execution: {error}"
    return collect_result(root, token, source, package, status, identity_error, process)


if __name__ == "__main__":
    raise SystemExit(main())
