"""Byte and symlink identities for exposed public runtime trees."""

from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import stat


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


def file_identity(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    with os.fdopen(descriptor, "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError(f"library entry is not a regular file: {path}")
        digest = hashlib.sha256()
        while block := stream.read(1024 * 1024):
            digest.update(block)
        after = os.fstat(stream.fileno())
        fields = ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
        if any(getattr(before, field) != getattr(after, field) for field in fields):
            raise ValueError(f"library changed while hashing: {path}")
        return {"type": "file", "bytes": before.st_size, "mode": stat.S_IMODE(before.st_mode),
                "sha256": digest.hexdigest()}


def library_identity(root, allow_symlinks=True):
    root = Path(root).absolute()
    if root.resolve(strict=True) != root or not root.is_dir():
        raise ValueError("library root must be a real directory without symlink ancestors")
    entries = {}
    for path in [root, *sorted(root.rglob("*"))]:
        name = str(path.relative_to(root))
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode):
            if not allow_symlinks:
                raise ValueError(f"unexpected symlink: {path}")
            target = path.resolve(strict=True)
            if not target.is_relative_to(root):
                raise ValueError(f"library symlink escapes exposed tree: {path}")
            entries[name] = {"type": "symlink", "link": os.readlink(path), "resolved": str(target)}
        elif stat.S_ISREG(info.st_mode):
            entries[name] = file_identity(path)
        elif stat.S_ISDIR(info.st_mode):
            entries[name] = {"type": "directory", "mode": stat.S_IMODE(info.st_mode)}
        else:
            raise ValueError(f"unsupported library entry: {path}")
    for entry in entries.values():
        if entry["type"] == "symlink":
            target = entries[str(Path(entry["resolved"]).relative_to(root))]
            entry["resolved_type"] = target["type"]
            if target["type"] == "file":
                entry["sha256"] = target["sha256"]
    encoded = json.dumps(entries, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return {"root": str(root), "sha256": hashlib.sha256(encoded).hexdigest(), "entries": entries}
