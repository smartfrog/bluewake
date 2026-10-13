#!/usr/bin/env python3
"""Isolated VITA-WW-03 launcher. Execution requires the external pre-launch review gate."""

import argparse
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path
import re
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
from fp_libraries import directory_fd, library_identity
from fp_profile import FIRMWARE_DIRECTORY, config_identity, stage_firmware, verify_config_log, verify_firmware
from fp_report import validate
from fp_vectors import cases
from render_node_guard import renderer_guard_command, renderer_guard_plan, renderer_guard_status

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
def evidence_file(root, name):
    with directory_fd(root / "evidence") as parent:
        descriptor = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                             0o600, dir_fd=parent)
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        yield stream


def add_render_node_argument(parser):
    parser.add_argument("--render-node", type=Path,
                        help="opt in to hardware-requested rendering with one explicit /dev/dri/renderD* character device")


def render_node_path(render_node):
    path = Path(render_node)
    if not path.is_absolute():
        raise ValueError("render node must be an absolute /dev/dri/renderD* path")
    reject_symlinks(path)
    if path.parent != Path("/dev/dri") or re.fullmatch(r"renderD[0-9]+", path.name) is None:
        raise ValueError("render node must be an explicit /dev/dri/renderD* path")
    return path


def render_node_identity(path, info):
    major, minor = os.major(info.st_rdev), os.minor(info.st_rdev)
    if not stat.S_ISCHR(info.st_mode) or major != 226 or not 128 <= minor <= 255 or path.name != f"renderD{minor}":
        raise ValueError(f"render node must be a DRM render character device matching its name: {path}")
    return {"mode": "hardware-requested", "render_node": {
        "path": str(path), "major": major, "minor": minor, "device": info.st_dev, "inode": info.st_ino}}


def renderer_identity(render_node=None):
    if render_node is None:
        return {"mode": "software", "render_node": None}
    path = render_node_path(render_node)
    try:
        info = path.lstat()
    except OSError as error:
        raise ValueError(f"render node must be an existing character device: {path}") from error
    return render_node_identity(path, info)


def verify_renderer_identity(renderer):
    if renderer is None:
        return
    node = renderer["render_node"]
    if renderer_identity(node["path"] if node is not None else None) != renderer:
        raise ValueError("render node identity or requested mode changed since staging")


def sandbox_command(root, executable, arguments, libraries=None, wayland_socket=None, render_node=None):
    renderer = renderer_identity(render_node)
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
    if renderer["render_node"] is not None:
        node = renderer["render_node"]["path"]
        command.extend(["--dir", "/dev/dri", "--dev-bind", node, node])
        del environment["LIBGL_ALWAYS_SOFTWARE"]
    application = ["/stack/vita3k/" + executable, *arguments]
    if renderer["render_node"] is not None:
        application = renderer_guard_command(application, renderer, environment.pop("LD_LIBRARY_PATH", ""))
    for name, value in environment.items():
        command.extend(["--setenv", name, value])
    return [*command, *application]


@contextmanager
def guarded_renderer_command(command, renderer):
    if renderer is None or renderer == renderer_identity():
        if "--dev-bind" in command:
            raise ValueError("render node bind requires its planned identity")
        yield command, ()
        return
    if renderer["mode"] != "hardware-requested":
        raise ValueError("render node bind requires a hardware-requested identity")
    path = render_node_path(renderer["render_node"]["path"])
    if command[0] != "/usr/bin/bwrap" or command.count("--dev-bind") != 1:
        raise ValueError("render node command must contain exactly one bwrap device bind")
    index = command.index("--dev-bind")
    if command[index + 1:index + 3] != [str(path), str(path)]:
        raise ValueError("render node command must bind the authorized canonical path only")
    guard = command.index("/usr/bin/python3") if "/usr/bin/python3" in command else len(command)
    if command[guard:guard + 10] != renderer_guard_command([], renderer)[:10] or len(command[guard:]) < 12:
        raise ValueError("render node command must include the authorized mounted-node guard before exec")
    with directory_fd(path.parent) as parent:
        descriptor = os.open(path.name, os.O_PATH | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent)
        try:
            if render_node_identity(path, os.fstat(descriptor)) != renderer:
                raise ValueError("render node identity changed since staging")
            execution = list(command)
            execution[index + 1] = f"/proc/self/fd/{descriptor}"
            yield execution, (descriptor,)
        finally:
            os.close(descriptor)


def stop_owned(process):
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def run_owned(command, root, timeout, report_identity=None, evidence_root=None, memory=False, control=False,
              phase_observer=None, renderer=None, environment=None, cwd=None):
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
    with evidence_file(evidence_root or root, "process.log") as log:
        process = None
        try:
            with guarded_renderer_command(command, renderer) as (execution, descriptors):
                options = {"pass_fds": descriptors} if descriptors else {}
                if descriptors:
                    record["renderer_launch"] = {"renderer": renderer, "command": execution,
                        "pass_fds": list(descriptors), "preflight_validated": True,
                        "guard": renderer_guard_plan(renderer), "postmount_validation": "UNOBSERVED",
                        "source_transport": "preflight O_PATH via /proc/self/fd; bwrap may recanonicalize; fd numbers are per-spawn"}
                process = subprocess.Popen(execution, cwd=cwd if cwd is not None else root / "work", stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                                           env=environment if environment is not None else {"PATH": "/usr/bin", "LANG": "C.UTF-8"},
                                           start_new_session=True, **options)
        except BaseException:
            if process is not None:
                stop_owned(process)
            raise
        with process:
            try:
                if report_identity is None and phase_observer is None:
                    status = process.wait(timeout=remaining_time())
                else:
                    while True:
                        remaining_time()
                        status = process.poll()
                        remaining_time()
                        if status is not None:
                            break
                        if phase_observer is not None:
                            observation = phase_observer(process.pid)
                            if observation is not None:
                                record.update(observation)
                                status = 125
                                break
                        if report_identity is None:
                            time.sleep(min(.05, remaining_time()))
                            continue
                        try:
                            content = read_report(root, control=True) if control else read_report(root, memory=True) if memory else read_report(root)
                        except FileNotFoundError:
                            pass
                        except (OSError, ValueError) as error:
                            record.update(stop_reason="invalid_report", report_error=str(error))
                            status = 2
                            break
                        else:
                            try:
                                if control:
                                    from arm_report import validate as validate_control
                                    validate_control(content.decode("utf-8"), *report_identity)
                                elif memory:
                                    from mem_report import validate as validate_memory
                                    validate_memory(content.decode("utf-8"), *report_identity, expected_platform="VITA")
                                else:
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
                stop_owned(process)
                record.update(pid=process.pid, exit=process.returncode, waited=True,
                              duration_seconds=time.monotonic() - started)
                if descriptors:
                    log.flush()
                    with directory_fd((evidence_root or root) / "evidence") as parent:
                        descriptor = os.open("process.log", os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                                             dir_fd=parent)
                    with os.fdopen(descriptor, "rb") as stream:
                        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                            raise ValueError("guard evidence must be a regular process log")
                        line = stream.readline(512)
                    record["renderer_launch"]["postmount_validation"] = renderer_guard_status(line, renderer)
                with evidence_file(evidence_root or root, "process.json") as stream:
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


def read_report(root, memory=False, control=False):
    with directory_fd(root / REPORT_DIRECTORY) as parent:
        descriptor = os.open("arm.report" if control else "mem.report" if memory else "fp.report", os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW | os.O_CLOEXEC,
                             dir_fd=parent)
    with os.fdopen(descriptor, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise ValueError("report must be a regular file")
        if (memory or control) and (info.st_uid != os.getuid() or info.st_nlink != 1):
            raise ValueError("ARM08 report must have one private owner/link" if control else
                             "memory report must have one private owner/link")
        if info.st_size > MAX_REPORT_BYTES:
            raise ValueError("report exceeds size limit")
        content = stream.read(MAX_REPORT_BYTES + 1)
        if len(content) > MAX_REPORT_BYTES:
            raise ValueError("report exceeds size limit")
        if len(content) != info.st_size:
            raise ValueError("report size changed while reading")
        return content


def collect_result(root, token, source, package, process_status, identity_error=None, process=None, evidence_root=None, memory=False, control=False, renderer=None):
    record = {"process_exit": process["exit"] if process else process_status,
              "completion": "INCOMPLETE", "semantic": "UNAVAILABLE"}
    if renderer is not None:
        record["renderer"] = renderer
    if process:
        record["process"] = process
    if identity_error:
        record["identity_error"] = identity_error
    try:
        content = read_report(root, control=True) if control else read_report(root, memory=True) if memory else read_report(root)
        if control:
            from arm_report import analyze
            analysis = analyze(content.decode("utf-8"), token, source, package)
            semantic = analysis["status"]
            record.update(reported_controls=analysis["controls"], reported_runtime=analysis["runtime"],
                          reported_restore=analysis["restore"], attribution=analysis["attribution"])
        elif memory:
            from mem_report import validate as validate_memory
            semantic = validate_memory(content.decode("utf-8"), token, source, package, expected_platform="VITA")
        else:
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
    with evidence_file(evidence_root or root, "result.json") as stream:
        stream.write(json.dumps(record, indent=2) + "\n")
    return status


def verify_package(path, expected_hash, memory=False, control=False):
    reject_symlinks(path)
    if sha256(path) != expected_hash:
        raise ValueError("VPK no longer matches build evidence")
    with zipfile.ZipFile(path) as archive:
        if sorted(archive.namelist()) != ["eboot.bin", "sce_sys/param.sfo"] or archive.testzip():
            raise ValueError("unexpected VPK content")
        if control:
            from mem_package import verify_contents
            verify_contents(archive, title=b"BWWA00008\0")
        elif memory:
            from mem_package import verify_contents
            verify_contents(archive)
        elif b"BWWF00003\0" not in archive.read("sce_sys/param.sfo"):
            raise ValueError("not the FP diagnostic title")


def main(memory=False, control=False, cpu_opt_off=False):
    description = ("Stage VITA-WW-09 cpu-opt-off pair only; exact saved-plan authorization required later."
                   if cpu_opt_off else
                   "Stage VITA-WW-08 baseline pair only; exact saved-plan authorization required later." if control
                   else "Isolated VITA-WW-04 memory launcher; defaults to stage only." if memory else __doc__)
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--runtime", type=Path, required=True, help="installed ELF; never a wrapper")
    parser.add_argument("--runtime-sha256", required=True)
    parser.add_argument("--runtime-source-revision", required=True,
                        help="observed source label only; executable source mapping remains UNVERIFIED")
    parser.add_argument("--libraries", type=Path, required=True, help="read-only public runtime libraries")
    parser.add_argument("--firmware", type=Path, help="copy only installed public system vs0/sa0 into the new profile")
    parser.add_argument("--wayland-socket", type=Path, help="explicitly authorized display socket only; no session bus")
    if not control:
        add_render_node_argument(parser)
    parser.add_argument("--build-evidence", type=Path, default=ROOT / ("build-ww08/arm-evidence.json" if control else "build-vita-ww-04/mem-evidence.json" if memory else "build-vita-ww-03/fp-evidence.json"))
    parser.add_argument("--out", type=Path, default=ROOT / ("build-ww08" if control else "build-vita-ww-04" if memory else "build-vita-ww-03"))
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--two-phase", action="store_true", help="install, verify, then run the installed title")
    parser.add_argument("--execute-reviewed", action="store_true",
                        help="ONLY after independent review and coordinator authorization; default stages only")
    args = parser.parse_args()
    if cpu_opt_off and not control:
        parser.error("cpu-opt-off staging is the ARM09 control profile only")
    if control and args.execute_reviewed:
        parser.error(("ARM09" if cpu_opt_off else "ARM08") +
                     " execution requires the exact saved plan and coordinator pair authorization")
    if memory or control:
        args.two_phase = True
    if args.two_phase and not args.firmware:
        parser.error("two-phase execution requires verified private firmware and configuration")
    if not math.isfinite(args.timeout) or not 0 < args.timeout <= 30:
        parser.error("timeout must be finite, positive and at most 30 seconds")
    render_node = getattr(args, "render_node", None)
    renderer = renderer_identity(render_node)
    if cpu_opt_off:
        from fp_campaign import reservation_path
        reserved = reservation_path(ROOT)
        if args.out.resolve() != reserved.parent.resolve():
            parser.error(f"cpu-opt-off staging must use the ignored output directory {reserved.parent}")
        if reserved.exists() or reserved.is_symlink():
            raise ValueError("VITA-WW-09 campaign is already reserved; a second run must not be staged")
    parent = build_directory(reject_symlinks(args.out))
    evidence = json.loads(args.build_evidence.read_text())
    source = evidence["source"]
    diagnostic = "arm" if control else "mem" if memory else "fp"
    if control and (evidence.get("method"), evidence.get("title")) != ("scalar-vfp-v1", "BWWA00008"):
        raise ValueError("unexpected ARM08 build method/title")
    if memory and (evidence.get("method"), evidence.get("title")) != ("bounded-v2", "BWWM00004"):
        raise ValueError("unexpected memory build method/title")
    artifacts = [(Path(name), entry["sha256"]) for name, entry in evidence["artifacts"].items()
                 if name.endswith(f"/vita-{diagnostic}/ww_{diagnostic}.vpk")]
    if len(artifacts) != 1:
        raise ValueError("expected exactly one ARM08 VPK in build evidence" if control else
                         "expected exactly one memory VPK in build evidence" if memory else
                         "expected exactly one FP VPK in build evidence")
    vpk, package = artifacts[0]
    package_options = {"control": True} if control else {"memory": True} if memory else {}
    verify_package(vpk, package, **package_options)
    token = uuid.uuid4().hex
    root = parent / ("vita3k-run-" + token)
    create_run(root)
    assets = stage_runtime(root, args.runtime, args.runtime_sha256)
    staged_vpk = root / f"runtime/{diagnostic}.vpk"
    with vpk.open("rb") as source_file, staged_vpk.open("xb") as destination:
        shutil.copyfileobj(source_file, destination)
    verify_package(staged_vpk, package, **package_options)
    firmware = stage_firmware(root, reject_symlinks(args.firmware), alt=cpu_opt_off) if args.firmware else {}
    private_config = config_identity(root, alt=cpu_opt_off) if firmware else None
    if not args.two_phase:
        directory = root / REPORT_DIRECTORY
        directory.mkdir(parents=True)
        (directory / "fp.request").write_text(f"{token} {source} {package}\n")
    display = {"wayland_socket": args.wayland_socket} if args.wayland_socket else {}
    if render_node is not None:
        display["render_node"] = render_node
    command = sandbox_command(root, "Vita3K", ["--console", "--backend-renderer", "OpenGL",
                                                f"{diagnostic}.vpk"], args.libraries, **display)
    libraries_before = library_identity(args.libraries)
    with evidence_file(root, "libraries-before.json") as stream:
        stream.write(json.dumps(libraries_before, indent=2) + "\n")
    plan = {"run": token, "source": source, "vpk_sha256": package,
            "runtime_sha256": args.runtime_sha256, "runtime_source": args.runtime_source_revision,
            "runtime_source_mapping": "UNVERIFIED",
            "renderer": renderer,
            "wayland_socket": str(args.wayland_socket) if args.wayland_socket else None,
            "firmware": firmware, "firmware_provenance": "installed system trees; version/PUP UNVERIFIED",
            "private_config": private_config,
            "runtime_assets": assets,
            "runtime_libraries": {"root": libraries_before["root"], "sha256": libraries_before["sha256"],
                                  "manifest": "libraries-before.json"},
            "command": command, "cwd": str(root / "work"),
            "sandbox_cwd": "/stack/vita3k", "completion_policy": "forced shutdown remains INCOMPLETE/status 125",
            "report_poll_seconds": .05, "report_exit_grace_seconds": .25,
            "RLIMIT_FSIZE": resource.getrlimit(resource.RLIMIT_FSIZE),
            "timeout_seconds": args.timeout, "real_execution": args.execute_reviewed}
    guard = renderer_guard_plan(renderer)
    if guard is not None:
        plan["renderer_guard"] = guard
    if memory:
        plan.update(diagnostic="mem", method="bounded-v2", title="BWWM00004", process_budget_used=0)
    if control:
        plan.update(diagnostic="arm", method="scalar-vfp-v1", title="BWWA00008", process_budget_used=0,
                    ticket_process_budget=2 if cpu_opt_off else 4, real_execution=False,
                    comparison="cpu-opt-off" if cpu_opt_off else "pristine-baseline",
                    authorization=(f"ARM09:{source}:{token}:cpu-opt-off:1,2" if cpu_opt_off else
                                   f"ARM08:{source}:{token}:baseline:1,2"))
    if args.two_phase:
        from fp_campaign import stage_campaign
        stage_campaign(root, plan, args, vpk)
    with evidence_file(root, "plan.json") as stream:
        stream.write(json.dumps(plan, indent=2) + "\n")
    if cpu_opt_off:
        from fp_campaign import reserve_campaign
        reserve_campaign(ROOT, plan)
    print(root, flush=True)
    if not args.execute_reviewed:
        print("STAGED ONLY: real Vita3K NOT RUN; review gate still required")
        return 0
    if args.two_phase:
        from fp_campaign import execute_campaign
        return execute_campaign(root, plan)
    verify_renderer_identity(renderer)
    verify_firmware(firmware)
    if private_config and config_identity(root, alt=cpu_opt_off) != private_config:
        raise ValueError("staged configuration changed before execution")
    status = run_owned(command, root, args.timeout, report_identity=(token, source, package), renderer=renderer)
    process = json.loads((root / "evidence/process.json").read_text())
    identity_error = None
    try:
        libraries_after = library_identity(args.libraries)
        with evidence_file(root, "libraries-after.json") as stream:
            stream.write(json.dumps(libraries_after, indent=2) + "\n")
        if libraries_after != libraries_before:
            identity_error = "public library tree changed during execution"
        verify_firmware(firmware)
        if private_config:
            verify_config_log(root)
    except (OSError, ValueError) as error:
        identity_error = f"cannot verify runtime inputs after execution: {error}"
    return collect_result(root, token, source, package, status, identity_error, process, renderer=renderer)


if __name__ == "__main__":
    raise SystemExit(main())
