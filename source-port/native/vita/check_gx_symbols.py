#!/usr/bin/env python3
"""E3 POC: prove the Vita GX/GD stub defines every GX/GD symbol the representative game objects
reference.

    check_gx_symbols.py --nm <nm> --ld <ld> --stub libcos_vita_gx_stub.a --output out.o OBJ...

Two checks:
1. every GX/GD symbol an object leaves undefined (nm -u) is defined by the stub archive or by
   cos_sdk (which defines seventeen GX names itself and is part of the game link);
2. ld -r of the objects, cos_sdk (whole) and the stub leaves no GX/GD symbol undefined, so a
   future game link has nothing left to resolve and a stub/SDK duplicate fails the merge.

Exit code 0 when both hold, 1 with the missing names printed otherwise.
"""
import argparse
import re
import subprocess
import sys

GX_SYMBOL = re.compile(r"^(GX|GD)[A-Za-z0-9_]*$|^__GDCurrentDL$")


def run(command):
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def undefined(nm, path):
    names = []
    for line in run([nm, "-u", path]).splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] == "U":
            names.append(parts[-1])
    return [name for name in names if GX_SYMBOL.match(name)]


def defined(nm, archive):
    names = set()
    for line in run([nm, "-g", "--defined-only", archive]).splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] in ("T", "W", "D", "B", "R", "V"):
            names.add(parts[-1])
    return names


def link_sdk(args):
    # cos_sdk is part of the game link and defines seventeen GX names itself (GXExtras.cpp,
    # GXFifo.cpp); it is merged whole so a stub definition that collided with it would fail here.
    if args.sdk:
        return ["--whole-archive", args.sdk, "--no-whole-archive"]
    return []


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("--ld", required=True)
    parser.add_argument("--stub", required=True)
    parser.add_argument("--sdk", help="static cos_sdk archive linked first, whole-archive")
    parser.add_argument("--output", required=True, help="relocatable file ld -r writes")
    parser.add_argument("objects", nargs="+")
    args = parser.parse_args()

    referenced = {}
    for obj in args.objects:
        for name in undefined(args.nm, obj):
            referenced.setdefault(name, set()).add(obj)

    available = defined(args.nm, args.stub)
    if args.sdk:
        available |= defined(args.nm, args.sdk)
    missing = sorted(name for name in referenced if name not in available)
    print(f"gx-stub-check: {len(args.objects)} objects reference {len(referenced)} GX/GD symbols")
    gx_available = [name for name in available if GX_SYMBOL.match(name)]
    print(f"gx-stub-check: the stub and cos_sdk together define {len(gx_available)} GX/GD symbols")

    run([args.ld, "-r", "-o", args.output] + args.objects + link_sdk(args) + [args.stub])
    unresolved = undefined(args.nm, args.output)

    for name in missing:
        print(f"gx-stub-check: MISSING {name}")
    for name in unresolved:
        print(f"gx-stub-check: UNRESOLVED after ld -r: {name}")
    if missing or unresolved:
        print(f"gx-stub-check: FAIL ({len(missing)} missing, {len(unresolved)} unresolved)")
        return 1
    print(f"gx-stub-check: ok, all {len(referenced)} GX/GD references resolve")
    return 0


if __name__ == "__main__":
    sys.exit(main())
