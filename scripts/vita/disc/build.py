#!/usr/bin/env python3
"""VITA-WW-13 build: the pinned-runtime disc probe. Pins stay read-only.

The probe compiles the pristine RecompCore checkout (never edited) and links
it against vita/feasibility/disc/disc_probe.c. Heavy output goes to the
maintainer's ignored build-f2 tree, never into the worktree.
"""

import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
PINS = {"recompcore": "ddd031b2a7f45fcbda37a49daac9b308b8d95da1"}
DEFAULT_BUILD = Path("/home/fred/projects/zeldaWWHDRecomp/build-f2/ww13")


def verify_source(path, sha):
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(path), "status", "--porcelain"], text=True)
    if actual != sha or dirty:
        raise ValueError(f"source must be pristine at {sha}: {path}")


def find_recompcore():
    candidate = ROOT / "ref/vita-ww-01/RecompCore"
    if (candidate / "GXRuntime").is_dir():
        return candidate
    return Path("/home/fred/projects/zeldaWWHDRecomp/ref/vita-ww-01/RecompCore")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=os.environ.get("WW13_BUILD", str(DEFAULT_BUILD)),
                        help="build directory (default: %(default)s)")
    args = parser.parse_args()

    recompcore = find_recompcore()
    verify_source(recompcore, PINS["recompcore"])
    build = Path(args.output) / "disc-host"
    build.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cmake", "-G", "Ninja", "-S", str(ROOT / "vita/feasibility/disc"),
                    "-B", str(build), f"-DRECOMPCORE_DIR={recompcore}"], check=True)
    subprocess.run(["cmake", "--build", str(build)], check=True)
    print(f"disc_probe: {build / 'disc_probe'}")


if __name__ == "__main__":
    raise SystemExit(main())
