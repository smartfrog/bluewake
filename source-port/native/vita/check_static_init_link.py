#!/usr/bin/env python3
"""P-A blocker 2 check: the covered game units and the Vita static-init main link with no PC
harness, no Aurora DVD and no PC feature module.

    check_static_init_link.py --nm <nm> --ld <ld> --output out.o \
        --stubs libcos_vita_pc_stubs.a --stubs libcos_vita_gx_stub.a \
        --sdk libcos_sdk.a OBJ...

Two questions, both about the objects this build actually compiles (the 23 GXBegin callers and the
static-init main's four objects), never about what cos_sdk or Aurora need for themselves:

1. every symbol the objects leave undefined that belongs to the PC harness, to Aurora (DVD included,
   which the Vita source build does not have), to GX/GD or to the PC feature hooks is defined by the
   stub archives or by cos_sdk. `_ZN2pc...` catches the harness's C++ entry points, so a harness
   include creeping back into the split unit fails here rather than at the final link;
2. ld -r of the objects, cos_sdk (whole) and the stubs fails on a symbol defined twice, so a stub
   that starts colliding with cos_sdk or with the game is caught.

Everything else is reported as the remaining work: g_profile_* are the 502 REL actor units of the
profile list (the compile sweep), the rest are game and libc symbols the other 891 units and newlib
bring. Exit code 0 when both questions hold, 1 with the names printed otherwise.
"""
import argparse
import re
import subprocess
import sys

FORBIDDEN = re.compile(r"^(GX|GD|DVD|__DVD|pc_)[A-Za-z0-9_]*$|^aurora_|^_ZN2pc")
PROFILE = re.compile(r"^g_profile_[A-Za-z0-9_]*$")


def run(command):
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def merge(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        raise SystemExit(f"static-init-link: FAIL ld -r ({result.returncode}): a symbol is defined twice")


def undefined(nm, path):
    names = []
    for line in run([nm, "-u", path]).splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] == "U":
            names.append(parts[-1])
    return names


def defined(nm, archive):
    names = set()
    for line in run([nm, "-g", "--defined-only", archive]).splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] in ("T", "W", "D", "B", "R", "V"):
            names.add(parts[-1])
    return names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("--ld", required=True)
    parser.add_argument("--output", required=True, help="relocatable file ld -r writes")
    parser.add_argument("--stubs", action="append", default=[],
                        help="stub archive, merged whole (repeatable)")
    parser.add_argument("--sdk", help="cos_sdk, merged whole: part of the real link")
    parser.add_argument("objects", nargs="+")
    args = parser.parse_args()

    required = {}
    internal = set()
    for obj in args.objects:
        for line in run([args.nm, "-g", "--defined-only", obj]).splitlines():
            parts = line.split()
            if len(parts) >= 2 and parts[-2] in ("T", "W", "D", "B", "R", "V"):
                internal.add(parts[-1])
        for name in undefined(args.nm, obj):
            required.setdefault(name, set()).add(obj)

    available = set(internal)
    for archive in args.stubs:
        available |= defined(args.nm, archive)
    if args.sdk:
        available |= defined(args.nm, args.sdk)

    missing = sorted(name for name in required if name not in available)
    forbidden = [name for name in missing if FORBIDDEN.match(name)]

    archives = list(args.stubs)
    command = [args.ld, "-r", "-o", args.output] + args.objects + ["--whole-archive"] + archives
    if args.sdk:
        command += [args.sdk]
    command += ["--no-whole-archive"]
    merge(command)

    census = sorted(set(undefined(args.nm, args.output)))
    profiles = [name for name in census if PROFILE.match(name)]
    other = [name for name in census if name not in profiles]

    print(f"static-init-link: {len(args.objects)} objects reference {len(required)} symbols, "
          f"{len(missing)} not in the stubs or cos_sdk")
    print(f"static-init-link: PC harness / Aurora / GX / DVD / feature hooks: {len(forbidden)} (must be 0)")
    print(f"static-init-link: g_profile_* left for the REL units: {len(profiles)}")
    print(f"static-init-link: other symbols the final link brings: {len(other)}")
    for name in forbidden:
        print(f"static-init-link: FORBIDDEN {name}")
    if forbidden:
        print(f"static-init-link: FAIL ({len(forbidden)} harness/Aurora/feature symbols unresolved)")
        return 1
    print("static-init-link: ok, the covered units need no PC harness, no Aurora DVD and no PC feature")
    return 0


if __name__ == "__main__":
    sys.exit(main())
