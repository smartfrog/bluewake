#!/usr/bin/env python3
"""Point selected translated chunks at the guest CPU the host adopted.

  global_guest_cpu_scope.py COMPOSITE_SRC --chunk 0040 [--chunk 0145 ...] [--check]

scripts/windows/global_guest_cpu.py rewrites every chunk for the Windows
fixed-CPU module, whose guest CPU is a by-value object at a fixed address. Trial
#25 showed that the combined Vita package cannot wait for that module-wide
preparation: the dispatcher-level canon emitted nothing, and the first fault
was still PC 0, so the block's own ctx (host r4) was clobbered inside the block.

This scoped form rewrites only the chunk files named on the command line. Each
function takes the guest CPU state as `CPUState* ctx` and reaches every register
through it; the transform renames the parameter to `ctx_param` and defines
`ctx` as the fixed slot the host adopts (`bw_guest_cpu`), so a clobbered r4
cannot move the body to a guest address. `bw_guest_global_ctx_entry(ctx_param)`
runs first and fails closed if the adopted slot is not the RAM guard's CPU; an
entry pointer that differs from the adopted state is the inside-block
corruption being survived, so it is counted and reported, not followed.

The change is repeatable and keeps LF line ends. The marker is the CMake
verification line (cmake/composite/CMakeLists.txt, BLUEWAKE_GLOBAL_CTX), so a
tree prepared by the module-wide script is rejected rather than half-converted.
"""
import argparse
import re
import sys
from pathlib import Path

MARK = "/* bluewake: the guest CPU through the module's adopted slot (BW_GUEST_GLOBAL_CTX) */\n"
DEFINE = (
    MARK
    + "extern CPUState* bw_guest_cpu_ptr;\n"
    + "#define bw_guest_cpu (*bw_guest_cpu_ptr)\n"
    + "#define ctx (&bw_guest_cpu)\n"
    + "void bw_guest_global_ctx_entry(CPUState* entry);\n"
)
INCLUDE = '#include "../generated.h"\n'
PARAM = re.compile(r"\bCPUState\* ctx\b(?=[,)])")
ENTRY = re.compile(
    r"(?m)^((?:static )?void [A-Za-z_][A-Za-z0-9_]*\(CPUState\* ctx_param\)) \{$")


def transform(text, path):
    if MARK in text:
        return text
    if INCLUDE not in text:
        raise ValueError(f"{path}: no generated.h include to place the definition after")
    if not PARAM.search(text):
        raise ValueError(f"{path}: no CPUState* ctx parameter")
    text = PARAM.sub("CPUState* ctx_param", text)
    text, entries = ENTRY.subn(
        r"\1 {\n    bw_guest_global_ctx_entry(ctx_param);", text)
    if entries == 0:
        raise ValueError(f"{path}: no translated function to guard")
    return text.replace(INCLUDE, INCLUDE + DEFINE, 1)


def select(root, names):
    chunks = sorted(root.glob("chunks_*/*.c"))
    if not chunks:
        sys.exit(f"no chunks under {root}")
    selectors = [name if name.startswith("chunk_") else f"chunk_{name}" for name in names]
    selected = [path for path in chunks
                if any(selector in path.name for selector in selectors)]
    missing = [selector for selector in selectors
               if not any(selector in path.name for path in chunks)]
    if missing:
        sys.exit(f"no chunk matches: {', '.join(missing)}")
    return selected


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("composite")
    parser.add_argument("--chunk", action="append", required=True,
                        help="chunk number (0040) or file prefix (chunk_0040); repeatable")
    parser.add_argument("--check", action="store_true",
                        help="report unprepared selected chunks and do not write")
    args = parser.parse_args()
    root = Path(args.composite)
    selected = select(root, args.chunk)
    changed = 0
    for path in selected:
        with open(path, encoding="utf-8", newline="") as file:
            original = file.read()
        converted = transform(original, path)
        if converted == original:
            continue
        changed += 1
        if args.check:
            print(f"needs global ctx: {path}")
            continue
        temporary = path.with_suffix(".c.tmp")
        with open(temporary, "w", encoding="utf-8", newline="") as file:
            file.write(converted)
        temporary.replace(path)
    if args.check:
        print(f"global ctx: {changed} of {len(selected)} selected chunks need the transform")
        return 1 if changed else 0
    print(f"global ctx: {changed} of {len(selected)} selected chunks rewritten")
    return 0


if __name__ == "__main__":
    sys.exit(main())
