"""Private diagnostic profile setup from installed system firmware only."""

import hashlib
import os
from pathlib import Path
import shutil
import stat

from fp_libraries import directory_fd, library_identity

FIRMWARE_DIRECTORY = Path("data/Vita3K/Vita3K")
CONFIG_PATH = Path("config/Vita3K/config.yml")
CONFIG_TEMPLATE = Path(__file__).with_name("fp_config.yml")
CONFIG_SHA256 = "c11fa8feaa4240186f652672643ee1c8d4cbdecbc09b62ac841a936272b67f25"
CONFIG_ALT_SHA256 = "b9cc0c63d791d0add83db669a6d340b46b839be7ee23c31285e270993364a079"
CONFIG_ALT_BYTES = 4285
CPU_OPT_LINES = (b"cpu-opt: true\n", b"cpu-opt: false\n")
SOURCE_CONFIG_SHA256 = "9e64e723c8286e77e7cd57ffb6049d3cdcf202615062a503348c3170a18edff7"
SOURCE_CONFIG_RECORD = "local-adoption-fbfae8841e53439db2d5d034b00a4c89/evidence/config-after.yml"
CONFIG_CHANGES = {"backend-renderer": {"from": "Vulkan", "to": "OpenGL"},
                  "user-id": {"from": "00", "to": ""}}


def read_profile_file(path, limit, owned=False):
    with directory_fd(path.parent) as parent:
        descriptor = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                             dir_fd=parent)
    with os.fdopen(descriptor, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise ValueError("profile input must be a bounded regular file")
        if owned and (info.st_uid != os.getuid() or info.st_nlink != 1):
            raise ValueError("private profile file must be owned and unshared")
        content = stream.read(limit + 1)
        after = os.fstat(stream.fileno())
        if (len(content) > limit or len(content) != info.st_size or
                any(getattr(info, field) != getattr(after, field)
                    for field in ("st_size", "st_mtime_ns", "st_ctime_ns"))):
            raise ValueError("profile input size changed during read")
    return content


def checked_config(path):
    content = read_profile_file(path, 4284)
    if hashlib.sha256(content).hexdigest() != CONFIG_SHA256:
        raise ValueError("configuration differs from the approved neutral schema")
    return content


def alt_config(baseline):
    if baseline.count(CPU_OPT_LINES[0]) != 1:
        raise ValueError("baseline configuration does not contain exactly one cpu-opt line")
    alt = baseline.replace(CPU_OPT_LINES[0], CPU_OPT_LINES[1], 1)
    keys = [line.split(b":", 1)[0] for line in alt.splitlines() if b":" in line and not line.startswith(b" ")]
    if len(keys) != 154 or len(set(keys)) != 154:
        raise ValueError("cpu-opt-off configuration must keep 154 unique keys")
    if alt.replace(CPU_OPT_LINES[1], CPU_OPT_LINES[0], 1) != baseline:
        raise ValueError("cpu-opt-off configuration changed bytes outside the cpu-opt line")
    if hashlib.sha256(alt).hexdigest() != CONFIG_ALT_SHA256:
        raise ValueError("cpu-opt-off configuration differs from the approved alternate schema")
    return alt


def checked_alt_config(path):
    content = read_profile_file(path, CONFIG_ALT_BYTES)
    if hashlib.sha256(content).hexdigest() != CONFIG_ALT_SHA256:
        raise ValueError("configuration differs from the approved cpu-opt-off schema")
    if content != alt_config(checked_config(CONFIG_TEMPLATE)):
        raise ValueError("cpu-opt-off configuration is not the single approved baseline mutation")
    return content


def verify_config_log(root, limit=1024 * 1024):
    content = read_profile_file(root / "evidence/process.log", limit)
    if b"Config file can't be loaded:" in content:
        raise ValueError("runtime reported configuration parse failure; see evidence/process.log")


def config_identity(root, alt=False):
    content = checked_alt_config(root / CONFIG_PATH) if alt else checked_config(root / CONFIG_PATH)
    identity = {"path": str(CONFIG_PATH), "bytes": len(content),
                "sha256": CONFIG_ALT_SHA256 if alt else CONFIG_SHA256,
                "template": CONFIG_TEMPLATE.name, "source_record": SOURCE_CONFIG_RECORD,
                "source_sha256": SOURCE_CONFIG_SHA256, "sanitization": CONFIG_CHANGES}
    if alt:
        identity["cpu_opt_off"] = {"baseline_sha256": CONFIG_SHA256, "alternate_sha256": CONFIG_ALT_SHA256,
                                   "change": "cpu-opt: true -> cpu-opt: false"}
    return identity


def stage_firmware(root, source, alt=False):
    config = alt_config(checked_config(CONFIG_TEMPLATE)) if alt else checked_config(CONFIG_TEMPLATE)
    sources = {name: library_identity(source / name, allow_symlinks=False) for name in ("vs0", "sa0")}
    for name, identity in sources.items():
        if not any(entry["type"] == "file" for entry in identity["entries"].values()):
            raise ValueError(f"empty system firmware tree: {name}")
        if (root / FIRMWARE_DIRECTORY / name).exists():
            raise FileExistsError(root / FIRMWARE_DIRECTORY / name)
    records = {}
    for name, identity in sources.items():
        target = root / FIRMWARE_DIRECTORY / name
        shutil.copytree(source / name, target, symlinks=True)
        staged = library_identity(target, allow_symlinks=False)
        if staged["entries"] != identity["entries"]:
            raise ValueError(f"staged firmware mismatch: {name}")
        records[name] = {"source": identity, "staged": staged}
    verify_firmware(records)
    with directory_fd(root / "config") as parent:
        os.mkdir("Vita3K", mode=0o700, dir_fd=parent)
    with directory_fd((root / CONFIG_PATH).parent) as parent:
        descriptor = os.open("config.yml", os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                             0o600, dir_fd=parent)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(config)
    return records


def verify_firmware(records):
    for name, trees in records.items():
        for location, expected in trees.items():
            actual = library_identity(Path(expected["root"]), allow_symlinks=False)
            if actual != expected:
                raise ValueError(f"{location} firmware changed: {name}")
