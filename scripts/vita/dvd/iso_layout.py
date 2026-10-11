#!/usr/bin/env python3
"""VITA-WW-13 packaging: install the private disc image for a Vita or Vita3K.

The VPK ships no game data. The image stays a separate private file, by
reference install path ux0:data/bluewake/GZLE01.iso, installed here with a
symlink by default (no 1.36 GiB copy) or a real copy with --copy. The
destination must not live inside the git worktree: game data is never
committed.
"""

import argparse
import shutil
import struct
import subprocess
from pathlib import Path

MAGIC = 0xC2339F3D
TARGET_RELATIVE = Path("data/bluewake/GZLE01.iso")


def validate_image(path):
    path = Path(path)
    if not path.is_file():
        raise SystemExit(f"not a file: {path}")
    with path.open("rb") as stream:
        header = stream.read(0x440)
    if len(header) < 0x440 or struct.unpack_from(">I", header, 0x1C)[0] != MAGIC:
        raise SystemExit(f"not a GameCube disc image: {path}")
    return path.stat().st_size


def inside_git_worktree(path):
    probe = Path(path)
    while not probe.exists() and probe != probe.parent:
        probe = probe.parent
    result = subprocess.run(["git", "-C", str(probe), "rev-parse", "--is-inside-work-tree"],
                            capture_output=True, text=True)
    return result.returncode == 0 and result.stdout.strip() == "true"


def git_ignores(path):
    probe = Path(path)
    while not probe.exists() and probe != probe.parent:
        probe = probe.parent
    result = subprocess.run(["git", "-C", str(probe), "check-ignore", "-q", str(path)],
                            capture_output=True)
    return result.returncode == 0


def ensure_not_committed(path):
    if inside_git_worktree(path) and not git_ignores(path):
        raise SystemExit(f"refusing to install game data at a tracked path: {path}")


def install(iso, ux0, copy=False):
    iso = Path(iso)
    size = validate_image(iso)
    ux0 = Path(ux0)
    ensure_not_committed(ux0)
    target = ux0 / TARGET_RELATIVE
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() or target.is_symlink():
        if not copy and target.is_symlink() and Path(target.resolve()) == iso.resolve():
            return target, size, "already installed"
        raise SystemExit(f"target already exists: {target}")
    if copy:
        shutil.copyfile(iso, target)
        return target, size, "copied"
    target.symlink_to(iso.resolve())
    return target, size, "symlinked"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True, help="private GameCube image")
    parser.add_argument("--ux0", type=Path, required=True, help="Vita3K ux0 directory (or a Vita card root)")
    parser.add_argument("--copy", action="store_true", help="copy instead of symlink")
    args = parser.parse_args()
    target, size, action = install(args.iso, args.ux0, args.copy)
    print(f"{action}: {target} -> ux0:{TARGET_RELATIVE.as_posix()} ({size} bytes)")


if __name__ == "__main__":
    raise SystemExit(main())
