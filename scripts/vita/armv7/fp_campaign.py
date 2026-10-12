"""Sequential installation and installed-title execution in one owned private profile."""

import hashlib
import json
import os
from pathlib import Path
import signal
import uuid
import zipfile

from fp_libraries import directory_fd, file_identity, library_identity
from fp_profile import (CONFIG_ALT_BYTES, CONFIG_PATH, CONFIG_TEMPLATE, alt_config, checked_alt_config,
                        checked_config, config_identity, read_profile_file, verify_config_log, verify_firmware)

RESERVATION = Path("build-ww09") / "campaign-reservation.json"


def reservation_path(root):
    return Path(root) / RESERVATION


def reserve_campaign(root, plan):
    path = reservation_path(root)
    with directory_fd(path.parent) as parent:
        descriptor = os.open(path.name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                             0o600, dir_fd=parent)
    with os.fdopen(descriptor, "w") as stream:
        json.dump({"authorization": plan["authorization"], "source": plan["source"], "run": plan["run"],
                   "reserved_process_numbers": [1, 2], "retry": "FORBIDDEN",
                   "conditional_pair_3_4": "NOT AUTHORIZED; VITA-WW-09 defines no conditional pair"}, stream)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def verify_campaign_reservation(root, plan):
    record = json.loads(read_profile_file(reservation_path(root), 65536, owned=True))
    if (record.get("authorization"), record.get("source"), record.get("run"),
            record.get("reserved_process_numbers"), record.get("retry")) != (
            plan["authorization"], plan["source"], plan["run"], [1, 2], "FORBIDDEN"):
        raise ValueError("permanent campaign reservation does not match the saved plan")
    return record


def stage_campaign(root, plan, args, vpk):
    from fp_launch import evidence_file, sandbox_command

    phases = root / "phases"
    phases.mkdir(mode=0o700)
    title = plan.get("title", "BWWF00003")
    commands = {
        "install": plan["command"],
        "guest": sandbox_command(root, "Vita3K", ["--backend-renderer", "OpenGL", "--installed-path", title],
                                 args.libraries, wayland_socket=args.wayland_socket),
    }
    plan.update(process_budget=2, request_policy="fresh guest token only after verified preparation",
                phases={}, source_vpk=str(vpk),
                runtime_input=str(args.runtime), runtime_inputs=runtime_inputs(args.runtime),
                staged_runtime=library_identity(root / "runtime", allow_symlinks=False))
    if plan["runtime_inputs"]["executable"]["sha256"] != plan["runtime_sha256"]:
        raise ValueError("runtime source changed while staging campaign")
    for name, identity in plan["runtime_inputs"]["assets"].items():
        if library_identity(root / "runtime" / name, allow_symlinks=False)["entries"] != identity["entries"]:
            raise ValueError("runtime assets changed while staging campaign")
    for name, command in commands.items():
        phase = phases / name
        phase.mkdir(mode=0o700)
        (phase / "evidence").mkdir(mode=0o700)
        entry = {"command": command, "timeout_seconds": args.timeout, "evidence_root": str(phase),
                 "process_number": 1 if name == "install" else 2, "sandbox_cwd": "/stack/vita3k",
                 "source": plan["source"], "vpk_sha256": plan["vpk_sha256"],
                 "config_sha256": plan["private_config"]["sha256"], "runtime_sha256": plan["runtime_sha256"],
                 "requires": "fresh profile" if name == "install" else "installer reaped and verified preparation"}
        plan["phases"][name] = entry
        with evidence_file(phase, "plan.json") as stream:
            stream.write(json.dumps(entry, indent=2) + "\n")


def runtime_inputs(executable):
    from fp_launch import reject_symlinks

    return {"executable": file_identity(reject_symlinks(executable)),
            "assets": {name: library_identity(executable.parent / name, allow_symlinks=False)
                       for name in ("data", "icons", "shaders-builtin", "translations")}}


def verify_inputs(root, plan):
    from fp_launch import verify_package

    memory = plan.get("diagnostic") == "mem"
    control = plan.get("diagnostic") == "arm"
    options = {"control": True} if control else {"memory": True} if memory else {}
    verify_package(Path(plan["source_vpk"]), plan["vpk_sha256"], **options)
    verify_package(root / ("runtime/arm.vpk" if control else "runtime/mem.vpk" if memory else "runtime/fp.vpk"), plan["vpk_sha256"], **options)
    if runtime_inputs(Path(plan["runtime_input"])) != plan["runtime_inputs"]:
        raise ValueError("runtime source/assets changed")
    if library_identity(root / "runtime", allow_symlinks=False) != plan["staged_runtime"]:
        raise ValueError("staged runtime/assets/package changed")
    before = json.loads((root / "evidence/libraries-before.json").read_text())
    if library_identity(Path(plan["runtime_libraries"]["root"])) != before:
        raise ValueError("public library tree changed")
    verify_firmware(plan["firmware"])


def verify_stopped(process):
    if not process.get("waited") or Path("/proc", str(process["pid"])).exists():
        raise ValueError("owned process has not been reaped")
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            fields = (entry / "stat").read_text().rsplit(") ", 1)[1].split()
        except FileNotFoundError:
            continue
        if int(fields[2]) == process["pid"]:
            raise ValueError("owned process group still populated")


def verify_installed(root, package, memory=False, control=False):
    from fp_launch import verify_package

    filename = "arm.vpk" if control else "mem.vpk" if memory else "fp.vpk"
    title_id = "BWWA00008" if control else "BWWM00004" if memory else "BWWF00003"
    verify_package(root / "runtime" / filename, package, **({"control": True} if control else {"memory": True} if memory else {}))
    apps = root / "data/Vita3K/Vita3K/ux0/app"
    title = apps / title_id
    for directory, names in ((apps, {title_id}), (title, {"eboot.bin", "sce_sys"}),
                             (title / "sce_sys", {"param.sfo"})):
        with directory_fd(directory) as descriptor:
            if os.fstat(descriptor).st_uid != os.getuid() or set(os.listdir(descriptor)) != names:
                raise ValueError("unexpected installed title entries or owner")
    payload = {}
    with zipfile.ZipFile(root / "runtime" / filename) as archive:
        for name, limit in (("eboot.bin", 16 * 1024 * 1024), ("sce_sys/param.sfo", 65536)):
            if archive.getinfo(name).file_size > limit:
                raise ValueError("installed diagnostic exceeds payload size limit")
            expected = archive.read(name)
            content = read_profile_file(title / name, len(expected), owned=True)
            if content != expected:
                raise ValueError(f"installed payload mismatch: {name}")
            payload[name] = {"bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}
    return payload


def emitted_config(root, phase, alt=False):
    from fp_launch import evidence_file

    content = read_profile_file(root / CONFIG_PATH, CONFIG_ALT_BYTES if alt else 4284, owned=True)
    with evidence_file(phase, "config-emitted.yml") as stream:
        stream.write(content.decode("utf-8"))
    canonical = alt_config(checked_config(CONFIG_TEMPLATE)) if alt else checked_config(CONFIG_TEMPLATE)
    if content not in (canonical, canonical.replace(b'user-id: ""\n', b'user-id: 00\n')):
        raise ValueError("unexpected runtime configuration mutation")
    return canonical


def verify_phase_logs(root, phase, log_limit=1024 * 1024):
    from fp_launch import evidence_file

    verify_config_log(phase, log_limit)
    try:
        content = read_profile_file(root / "cache/Vita3K/vita3k.log", log_limit, owned=True)
    except FileNotFoundError:
        return
    with evidence_file(phase, "runtime-cache.log") as stream:
        stream.write(content.decode("utf-8"))
    if b"Config file can't be loaded:" in content:
        raise ValueError("runtime cache reported configuration parse failure")


def restore_config(root, canonical, alt=False):
    with directory_fd((root / CONFIG_PATH).parent) as parent:
        descriptor = os.open("config.restore.yml", os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                             0o600, dir_fd=parent)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(canonical)
        os.replace("config.restore.yml", "config.yml", src_dir_fd=parent, dst_dir_fd=parent)
    if alt:
        checked_alt_config(root / CONFIG_PATH)
    else:
        checked_config(root / CONFIG_PATH)


def fresh_request(root, source, package, memory=False, control=False):
    from fp_launch import REPORT_DIRECTORY

    directory = root
    for part in REPORT_DIRECTORY.parts:
        with directory_fd(directory) as parent:
            try:
                os.mkdir(part, mode=0o700, dir_fd=parent)
            except FileExistsError:
                pass
        directory = directory / part
        with directory_fd(directory) as descriptor:
            if os.fstat(descriptor).st_uid != os.getuid():
                raise ValueError("report directory must be privately owned")
    token = uuid.uuid4().hex
    prefix = "arm" if control else "mem" if memory else "fp"
    with directory_fd(directory) as parent:
        if set(os.listdir(parent)) & {prefix + suffix for suffix in (".partial", ".report", ".request")}:
            raise ValueError("preexisting diagnostic output/request; refusing to delete or reuse it")
        descriptor = os.open(prefix + ".request", os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                             0o600, dir_fd=parent)
        with os.fdopen(descriptor, "w") as stream:
            stream.write(f"{token} {source} {package}\n")
    return token


def execute_campaign(root, plan):
    from fp_launch import collect_result, evidence_file, run_owned

    install = plan["phases"]["install"]
    control = plan.get("diagnostic") == "arm"
    alt = plan.get("comparison") == "cpu-opt-off"
    options = {"control": True} if control else {"memory": True} if plan.get("diagnostic") == "mem" else {}
    guest = plan["phases"]["guest"]
    install_root, guest_root = Path(install["evidence_root"]), Path(guest["evidence_root"])
    verify_inputs(root, plan)
    if config_identity(root, alt=alt) != plan["private_config"]:
        raise ValueError("configuration changed before installation")
    status = run_owned(install["command"], root, install["timeout_seconds"], evidence_root=install_root)
    process = json.loads((install_root / "evidence/process.json").read_text())
    preparation = {"readiness": "REJECTED", "process_status": status, "process": process}
    try:
        verify_stopped(process)
        if alt:
            canonical = alt_config(checked_config(CONFIG_TEMPLATE))
            try:
                emitted_config(root, install_root, alt=True)
            except (OSError, ValueError) as rejection:
                try:
                    restore_config(root, canonical, alt=True)
                except (OSError, ValueError) as failure:
                    raise ValueError(f"emitted configuration rejected: {rejection}; "
                                     f"canonical restoration failed: {failure}") from rejection
                raise
            restore_config(root, canonical, alt=True)
        else:
            canonical = emitted_config(root, install_root)
            if control:
                restore_config(root, canonical)
        if not ((status == 0 and process["stop_reason"] == "exit" and process["exit"] == 0) or
                (status == 124 and process["stop_reason"] == "timeout" and
                 process["exit"] in (0, -signal.SIGTERM, -signal.SIGKILL))):
            raise ValueError("unexpected installer disposition")
        verify_phase_logs(root, install_root)
        verify_inputs(root, plan)
        preparation["payload"] = verify_installed(root, plan["vpk_sha256"], **options)
        if not control:
            restore_config(root, canonical)
        token = fresh_request(root, plan["source"], plan["vpk_sha256"], **options)
        preparation.update(readiness="VERIFIED", guest_token=token, config=config_identity(root, alt=alt))
    except (OSError, ValueError) as error:
        preparation["error"] = str(error)
    with evidence_file(install_root, "preparation.json") as stream:
        stream.write(json.dumps(preparation, indent=2) + "\n")
    if preparation["readiness"] != "VERIFIED":
        return status or 2
    with evidence_file(guest_root, "request.json") as stream:
        stream.write(json.dumps({"token": token, "source": plan["source"], "vpk_sha256": plan["vpk_sha256"]}) + "\n")
    identity = (token, plan["source"], plan["vpk_sha256"])
    status = run_owned(guest["command"], root, guest["timeout_seconds"], report_identity=identity, evidence_root=guest_root, **options)
    process = json.loads((guest_root / "evidence/process.json").read_text())
    error = None
    canonical = alt_config(checked_config(CONFIG_TEMPLATE)) if alt else None
    stopped = False
    try:
        verify_stopped(process)
        stopped = True
        canonical = emitted_config(root, guest_root, alt=alt)
        verify_phase_logs(root, guest_root)
        verify_inputs(root, plan)
        verify_installed(root, plan["vpk_sha256"], **options)
    except (OSError, ValueError) as failure:
        error = str(failure)
    finally:
        restore = stopped if alt else control and canonical is not None
        if restore:
            try:
                restore_config(root, canonical, alt=alt)
            except (OSError, ValueError) as failure:
                error = f"canonical restoration failed: {failure}; preceding error: {error}"
    return collect_result(root, *identity, status, error, process, evidence_root=guest_root, **options)
