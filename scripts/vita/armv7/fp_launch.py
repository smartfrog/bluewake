#!/usr/bin/env python3
"""Isolated VITA-WW-03 launcher. Execution requires the external pre-launch review gate."""

import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import uuid
import zipfile

from build import ROOT, build_directory
from fp_build import sha256
from fp_report import validate
from fp_vectors import cases

REPORT_DIRECTORY = Path("data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture")


def reject_symlinks(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink():
            raise ValueError(f"unexpected symlink: {part}")
    return path


def create_run(root):
    reject_symlinks(root)
    root.mkdir(mode=0o700)
    for child in ("home", "config", "cache", "data", "state", "work", "evidence", "runtime",
                  "xdg-runtime", "config-system", "data-system"):
        (root / child).mkdir(mode=0o700)


def sandbox_command(root, executable, arguments, libraries=None):
    root = reject_symlinks(root)
    if Path(executable).name != executable:
        raise ValueError("executable must be a basename")
    command = ["/usr/bin/bwrap", "--unshare-all", "--die-with-parent", "--new-session",
               "--ro-bind", "/usr", "/usr", "--ro-bind", "/etc", "/etc",
               "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib64", "/lib64",
               "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
               "--bind", str(root), "/run/ww03",
               "--ro-bind", str(root / "runtime"), "/run/ww03/runtime",
               "--ro-bind", str(root / "runtime"), "/runtime",
               "--chdir", "/run/ww03/work", "--clearenv"]
    environment = {"PATH": "/usr/bin", "LANG": "C.UTF-8", "HOME": "/run/ww03/home",
                   "XDG_CONFIG_HOME": "/run/ww03/config", "XDG_CACHE_HOME": "/run/ww03/cache",
                   "XDG_DATA_HOME": "/run/ww03/data", "XDG_STATE_HOME": "/run/ww03/state",
                   "XDG_RUNTIME_DIR": "/run/ww03/xdg-runtime",
                   "XDG_CONFIG_DIRS": "/run/ww03/config-system",
                   "XDG_DATA_DIRS": "/run/ww03/data-system",
                   "QT_QPA_PLATFORM": "offscreen", "SDL_VIDEODRIVER": "offscreen",
                   "SDL_AUDIODRIVER": "dummy", "LIBGL_ALWAYS_SOFTWARE": "1"}
    if libraries is not None:
        libraries = reject_symlinks(libraries)
        command.extend(["--ro-bind", str(libraries), "/runtime-libs"])
        environment["LD_LIBRARY_PATH"] = "/runtime-libs/lib"
        environment["QT_PLUGIN_PATH"] = "/runtime-libs/lib/qt6/plugins"
    for name, value in environment.items():
        command.extend(["--setenv", name, value])
    return [*command, "/runtime/" + executable, *arguments]


def run_owned(command, root, timeout):
    if timeout <= 0:
        raise ValueError("timeout must be positive")
    with (root / "evidence/process.log").open("x") as log:
        with subprocess.Popen(command, cwd=root / "work", stdout=log, stderr=log,
                              env={"PATH": "/usr/bin", "LANG": "C.UTF-8"},
                              start_new_session=True) as process:
            try:
                return process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                return 124
            except BaseException:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                raise


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


def collect_result(root, token, source, package, process_status):
    report = root / REPORT_DIRECTORY / "fp.report"
    record = {"process_exit": process_status, "semantic": "UNAVAILABLE"}
    try:
        reject_symlinks(report)
        semantic = validate(report.read_text(), token, source, package, cases())
        record["semantic"] = "FAIL" if semantic else "PASS"
        record["report_sha256"] = sha256(report)
        status = process_status or semantic
    except (OSError, ValueError) as error:
        record["report_error"] = str(error)
        status = process_status or 2
    record["status"] = status
    (root / "evidence/result.json").write_text(json.dumps(record, indent=2) + "\n")
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True, help="installed ELF; never a wrapper")
    parser.add_argument("--runtime-sha256", required=True)
    parser.add_argument("--runtime-source-revision", required=True,
                        help="reviewed source mapping; a version banner alone is insufficient")
    parser.add_argument("--libraries", type=Path, required=True, help="read-only public runtime libraries")
    parser.add_argument("--build-evidence", type=Path, default=ROOT / "build-vita-ww-03/fp-evidence.json")
    parser.add_argument("--out", type=Path, default=ROOT / "build-vita-ww-03")
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--execute-reviewed", action="store_true",
                        help="ONLY after independent review and coordinator authorization; default stages only")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    parent = build_directory(reject_symlinks(args.out))
    evidence = json.loads(args.build_evidence.read_text())
    source = evidence["source"]
    artifacts = [(Path(name), entry["sha256"]) for name, entry in evidence["artifacts"].items()
                 if name.endswith("/vita-fp/ww_fp.vpk")]
    if len(artifacts) != 1:
        raise ValueError("expected exactly one FP VPK in build evidence")
    vpk, package = artifacts[0]
    reject_symlinks(vpk)
    if sha256(vpk) != package:
        raise ValueError("VPK no longer matches build evidence")
    with zipfile.ZipFile(vpk) as archive:
        if sorted(archive.namelist()) != ["eboot.bin", "sce_sys/param.sfo"] or archive.testzip():
            raise ValueError("unexpected VPK content")
        if b"BWWF00003\0" not in archive.read("sce_sys/param.sfo"):
            raise ValueError("not the FP diagnostic title")
    token = uuid.uuid4().hex
    root = parent / ("vita3k-run-" + token)
    create_run(root)
    assets = stage_runtime(root, args.runtime, args.runtime_sha256)
    shutil.copyfile(vpk, root / "runtime/fp.vpk")
    directory = root / REPORT_DIRECTORY
    directory.mkdir(parents=True)
    (directory / "fp.request").write_text(f"{token} {source} {package}\n")
    command = sandbox_command(root, "Vita3K", ["--console", "--backend-renderer", "OpenGL",
                                               "/runtime/fp.vpk"], args.libraries)
    plan = {"run": token, "source": source, "vpk_sha256": package,
            "runtime_sha256": args.runtime_sha256, "runtime_source": args.runtime_source_revision,
            "runtime_assets": assets, "command": command, "cwd": str(root / "work"),
            "timeout_seconds": args.timeout, "real_execution": args.execute_reviewed}
    (root / "evidence/plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    print(root, flush=True)
    if not args.execute_reviewed:
        print("STAGED ONLY: real Vita3K NOT RUN; review gate still required")
        return 0
    status = run_owned(command, root, args.timeout)
    return collect_result(root, token, source, package, status)


if __name__ == "__main__":
    raise SystemExit(main())
