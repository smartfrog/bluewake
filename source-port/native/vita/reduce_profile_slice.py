#!/usr/bin/env python3
"""Reduce compiled actor units to their g_profile_* data for the Vita boot path.

    reduce_profile_slice.py --nm <nm> --objcopy <objcopy> --objdump <objdump> \
        --readelf <readelf> --ld <ld> --output <slice.o> OBJ...

Why: the boot path needs the real profile identities (f_pc_profile_lst's g_profile_* objects),
but linking a unit's code drags in the game API it calls, which the bounded slice does not have.
With -ffunction-sections -fdata-sections each profile is its own section, so for every unit:

1. remove the relocations of the profile section - its method-table pointers become NULL, and
   pc::staticInitCheck never reads them (it reads mProcName and the list identity);
2. localize the unit's other globals so they are not garbage-collection roots;
3. ld -r --gc-sections -u <g_profile> keeps only the profile data and drops the code.

The final object must have exactly the profiles, no other global definitions and no code
relocations (debug relocations are ignored); anything else fails the build rather than turning
into a silent stub. This is profile metadata, not the linked actor: the code arrives with the
rest of the REL sweep.
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys


def run(command):
    env = dict(os.environ, LC_ALL="C")
    result = subprocess.run(command, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        raise SystemExit(f"profile-slice: FAIL {' '.join(command)}")
    return result.stdout


def globals_defined(nm, path):
    output = run([nm, "-g", "--defined-only", str(path)])
    return [line.split()[-1] for line in output.splitlines() if line.strip()]


def relocations(objdump, path):
    sections = []
    for line in run([objdump, "-r", str(path)]).splitlines():
        match = re.match(r"RELOCATION RECORDS FOR \[(.*)\]:", line)
        if match and not match.group(1).startswith(".debug"):
            sections.append(match.group(1))
    return sections


def profile_section(readelf, path, profile):
    symbol = None
    for line in run([readelf, "-sW", str(path)]).splitlines():
        parts = line.split()
        if parts and parts[-1] == profile:
            symbol = parts
            break
    if symbol is None:
        raise SystemExit(f"profile-slice: FAIL {path}: no symbol table entry for {profile}")
    index = symbol[6]
    for line in run([readelf, "-SW", str(path)]).splitlines():
        match = re.match(r"\s*\[\s*(\d+)\]\s+(\S+)", line)
        if match and match.group(1) == index:
            return match.group(2)
    raise SystemExit(f"profile-slice: FAIL {path}: no section for {profile} (index {index})")


def reduce_unit(args, obj, index, out):
    names = globals_defined(args.nm, obj)
    profiles = [name for name in names if name.startswith("g_profile_")]
    if len(profiles) != 1:
        raise SystemExit(f"profile-slice: FAIL {obj}: {len(profiles)} g_profile_* definitions, "
                         f"expected 1")
    profile = profiles[0]
    # -fdata-sections names the profile's section after it, but the toolchain picks the variant:
    # .data.g_profile_X on ARM/ILP32, .data.rel.g_profile_X on AArch64 LP64 (it holds pointers).
    section = profile_section(args.readelf, obj, profile)
    stripped = out.with_name(f"{out.stem}.{index}.stripped.o")
    localized = out.with_name(f"{out.stem}.{index}.localized.o")
    reduced = out.with_name(f"{out.stem}.{index}.o")
    localize_list = out.with_name(f"{out.stem}.{index}.locals")
    run([args.objcopy, "--remove-section=.init_array", "--remove-section=.fini_array",
         f"--remove-relocations={section}", str(obj), str(stripped)])
    localize_list.write_text("\n".join(name for name in names if name != profile) + "\n")
    run([args.objcopy, f"--localize-symbols={localize_list}", str(stripped), str(localized)])
    run([args.ld, "-r", "--gc-sections", "-u", profile, "-o", str(reduced), str(localized)])
    remaining = relocations(args.objdump, reduced)
    if remaining:
        raise SystemExit(f"profile-slice: FAIL {obj}: {profile} kept relocations in {remaining}")
    defined = globals_defined(args.nm, reduced)
    if defined != [profile]:
        raise SystemExit(f"profile-slice: FAIL {obj}: reduced globals {defined}, expected "
                         f"[{profile}]")
    return reduced, profile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--ld", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("objects", nargs="+")
    args = parser.parse_args()

    out = pathlib.Path(args.output)
    reduced = []
    profiles = []
    for index, obj in enumerate(pathlib.Path(path) for path in args.objects):
        part, profile = reduce_unit(args, obj, index, out)
        reduced.append(part)
        profiles.append(profile)
    run([args.ld, "-r", "-o", str(out)] + [str(part) for part in reduced])
    remaining = relocations(args.objdump, out)
    if remaining:
        raise SystemExit(f"profile-slice: FAIL {out}: relocations remain in {remaining}")
    defined = globals_defined(args.nm, out)
    if sorted(defined) != sorted(profiles):
        raise SystemExit(f"profile-slice: FAIL {out}: globals {defined}, expected the {len(profiles)} "
                         f"profiles")
    print(f"profile-slice: {len(reduced)} units, {len(profiles)} g_profile_* definitions, "
          f"{out.stat().st_size} bytes, 0 relocations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
