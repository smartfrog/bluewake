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

With --symbols, the same reduction applies to a given data symbol per object instead of finding
one g_profile_*: the boot path uses it for the data-only base method tables (g_fopAc_Method,
g_fopMsg_Method, g_fopOvlp_Method), whose function pointers are zeroed the same way while their
identities and sizes stay real.

With --callable, one symbol keeps its relocations and the code they reach: the group is merged,
every other global is localized and ld -r --gc-sections -u <symbol> keeps exactly the function
chain the table calls. The boot path uses it for g_fpcLf_Method (fpcLf_* -> fpcMtd_* /
fpcDwPi_* / fpcBs_MakeOfType). The result must have the symbol as its only global and no
relocation to an undefined symbol.
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


def symbol_section(readelf, path, symbol):
    entry = None
    for line in run([readelf, "-sW", str(path)]).splitlines():
        parts = line.split()
        if parts and parts[-1] == symbol:
            entry = parts
            break
    if entry is None:
        raise SystemExit(f"profile-slice: FAIL {path}: no symbol table entry for {symbol}")
    index = entry[6]
    for line in run([readelf, "-SW", str(path)]).splitlines():
        match = re.match(r"\s*\[\s*(\d+)\]\s+(\S+)", line)
        if match and match.group(1) == index:
            return match.group(2)
    raise SystemExit(f"profile-slice: FAIL {path}: no section for {symbol} (index {index})")


def reduce_unit(args, obj, index, out, symbol=None):
    names = globals_defined(args.nm, obj)
    if symbol is None:
        profiles = [name for name in names if name.startswith("g_profile_")]
        if len(profiles) != 1:
            raise SystemExit(f"profile-slice: FAIL {obj}: {len(profiles)} g_profile_* definitions, "
                             f"expected 1")
        symbol = profiles[0]
    elif symbol not in names:
        raise SystemExit(f"profile-slice: FAIL {obj}: {symbol} is not a global definition")
    # -fdata-sections names the symbol's section after it, but the toolchain picks the variant:
    # .data.<symbol> on ARM/ILP32, .data.rel.<symbol> on AArch64 LP64 (it holds pointers).
    section = symbol_section(args.readelf, obj, symbol)
    stripped = out.with_name(f"{out.stem}.{index}.stripped.o")
    localized = out.with_name(f"{out.stem}.{index}.localized.o")
    reduced = out.with_name(f"{out.stem}.{index}.o")
    localize_list = out.with_name(f"{out.stem}.{index}.locals")
    run([args.objcopy, "--remove-section=.init_array", "--remove-section=.fini_array",
         f"--remove-relocations={section}", str(obj), str(stripped)])
    localize_list.write_text("\n".join(name for name in names if name != symbol) + "\n")
    run([args.objcopy, f"--localize-symbols={localize_list}", str(stripped), str(localized)])
    run([args.ld, "-r", "--gc-sections", "-u", symbol, "-o", str(reduced), str(localized)])
    remaining = relocations(args.objdump, reduced)
    if remaining:
        raise SystemExit(f"profile-slice: FAIL {obj}: {symbol} kept relocations in {remaining}")
    defined = globals_defined(args.nm, reduced)
    if defined != [symbol]:
        raise SystemExit(f"profile-slice: FAIL {obj}: reduced globals {defined}, expected "
                         f"[{symbol}]")
    return reduced, symbol


def reduce_callable(args, objects, out):
    combined = out.with_name(f"{out.stem}.combined.o")
    stripped = out.with_name(f"{out.stem}.stripped.o")
    localized = out.with_name(f"{out.stem}.localized.o")
    localize_list = out.with_name(f"{out.stem}.locals")
    run([args.ld, "-r", "-o", str(combined)] + [str(obj) for obj in objects])
    run([args.objcopy, "--remove-section=.init_array", "--remove-section=.fini_array",
         str(combined), str(stripped)])
    names = globals_defined(args.nm, stripped)
    if args.callable not in names:
        raise SystemExit(f"profile-slice: FAIL {out}: {args.callable} is not a global definition")
    localize_list.write_text("\n".join(name for name in names if name != args.callable) + "\n")
    run([args.objcopy, f"--localize-symbols={localize_list}", str(stripped), str(localized)])
    run([args.ld, "-r", "--gc-sections", "-u", args.callable, "-o", str(out), str(localized)])
    defined = globals_defined(args.nm, out)
    if defined != [args.callable]:
        raise SystemExit(f"profile-slice: FAIL {out}: globals {defined}, expected [{args.callable}]")
    undefined = set(run([args.nm, "-u", str(out)]).split())
    referenced = set()
    for line in run([args.objdump, "-r", str(out)]).splitlines():
        parts = line.split()
        if not parts or parts[0][0] not in "0123456789abcdef":
            continue
        for index, part in enumerate(parts):
            if part.startswith("R_") and index + 1 < len(parts):
                referenced.add(parts[index + 1])
                break
    missing = sorted(name for name in referenced & undefined if not name.startswith("."))
    if missing:
        raise SystemExit(f"profile-slice: FAIL {out}: callable chain still needs {missing}")
    return f"{out.stat().st_size} bytes, callable, {len(referenced)} local relocations"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--ld", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--symbols", help="comma list, one symbol per object (method tables); "
                        "without it each object must define exactly one g_profile_*")
    parser.add_argument("--callable", help="keep one symbol callable: its own relocations and the "
                        "code they reach stay real, everything else is localized and dropped")
    parser.add_argument("objects", nargs="+")
    args = parser.parse_args()

    out = pathlib.Path(args.output)
    if args.callable:
        summary = reduce_callable(args, [pathlib.Path(path) for path in args.objects], out)
        print(f"profile-slice: {len(args.objects)} units, {args.callable} {summary}")
        return 0

    symbols = args.symbols.split(",") if args.symbols else [None] * len(args.objects)
    if len(symbols) != len(args.objects):
        raise SystemExit(f"profile-slice: FAIL {len(symbols)} symbols for {len(args.objects)} objects")
    reduced = []
    kept = []
    for index, (obj, symbol) in enumerate(zip((pathlib.Path(path) for path in args.objects), symbols)):
        part, name = reduce_unit(args, obj, index, out, symbol)
        reduced.append(part)
        kept.append(name)
    run([args.ld, "-r", "-o", str(out)] + [str(part) for part in reduced])
    remaining = relocations(args.objdump, out)
    if remaining:
        raise SystemExit(f"profile-slice: FAIL {out}: relocations remain in {remaining}")
    defined = globals_defined(args.nm, out)
    if sorted(defined) != sorted(kept):
        raise SystemExit(f"profile-slice: FAIL {out}: globals {defined}, expected the {len(kept)} "
                         f"symbols")
    kind = "method tables" if args.symbols else "g_profile_* definitions"
    print(f"profile-slice: {len(reduced)} units, {len(kept)} {kind}, "
          f"{out.stat().st_size} bytes, 0 relocations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
