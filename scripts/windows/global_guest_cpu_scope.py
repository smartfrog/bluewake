#!/usr/bin/env python3
"""Point selected translated chunks at the guest CPU the host adopted.

  global_guest_cpu_scope.py COMPOSITE_SRC --chunk 0040 [--chunk 0145 ...] [--check]
  global_guest_cpu_scope.py COMPOSITE_SRC --dol [--check]

scripts/windows/global_guest_cpu.py rewrites every chunk for the Windows
fixed-CPU module, whose guest CPU is a by-value object at a fixed address. Trial
#25 showed that the combined Vita package cannot wait for that module-wide
preparation: the dispatcher-level canon emitted nothing, and the first fault
was still PC 0, so the block's own ctx (host r4) was clobbered inside the block.

Trial #26 faulted in DOL chunk_0173 (host PC 0x94150000, inside its linked
range 0x940D9932..0x9416FDE6) after the normal title path had presented 297
frames, so the three-chunk scope was not enough. `--dol` rewrites every DOL
chunk and leaves the REL chunks byte-identical: the DOL text is the game's own
code and this path does not leave it, and an unscoped DOL block is the failure
being fixed.

Each function takes the guest CPU state as `CPUState* ctx` and reaches every
register through it; the transform renames the parameter to `ctx_param` and
defines `ctx` as the fixed slot the host adopts (`bw_guest_cpu`), so a clobbered
r4 cannot move the body to a guest address.
`bw_guest_global_ctx_entry(ctx_param)` runs first and fails closed if the
adopted slot is not the RAM guard's CPU; an entry pointer that differs from the
adopted state is the inside-block corruption being survived, so it is counted
and reported, not followed.

The change is repeatable and keeps LF line ends. A selection is converted in
memory first and written only when every selected chunk is prepared, so a bad
chunk leaves the tree untouched. The marker is the CMake verification line
(cmake/composite/CMakeLists.txt, BLUEWAKE_GLOBAL_CTX), so a tree prepared by
the module-wide script is rejected rather than half-converted.
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


def select(root, names, dol):
    chunks = sorted(root.glob("chunks_*/*.c"))
    if not chunks:
        sys.exit(f"no chunks under {root}")
    selected = []
    if dol:
        dol_chunks = sorted((root / "chunks_dol").glob("*.c"))
        if not dol_chunks:
            sys.exit(f"no DOL chunks under {root}")
        selected.extend(dol_chunks)
    selectors = [name if name.startswith("chunk_") else f"chunk_{name}" for name in names]
    selected.extend(path for path in chunks
                    if any(selector in path.name for selector in selectors))
    missing = [selector for selector in selectors
               if not any(selector in path.name for path in chunks)]
    if missing:
        sys.exit(f"no chunk matches: {', '.join(missing)}")
    return list(dict.fromkeys(selected))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("composite")
    parser.add_argument("--chunk", action="append", default=[],
                        help="chunk number (0040) or file prefix (chunk_0040); repeatable")
    parser.add_argument("--dol", action="store_true",
                        help="rewrite every DOL chunk, including the trial #26 high block")
    parser.add_argument("--check", action="store_true",
                        help="report unprepared selected chunks and do not write")
    args = parser.parse_args()
    if not args.chunk and not args.dol:
        parser.error("select chunks with --chunk or --dol")
    root = Path(args.composite)
    selected = select(root, args.chunk, args.dol)
    changed = []
    for path in selected:
        with open(path, encoding="utf-8", newline="") as file:
            original = file.read()
        try:
            converted = transform(original, path)
        except ValueError as error:
            sys.exit(str(error))
        if converted != original:
            changed.append((path, converted))
    if args.check:
        for path, _ in changed:
            print(f"needs global ctx: {path}")
        print(f"global ctx: {len(changed)} of {len(selected)} selected chunks need the transform")
        return 1 if changed else 0
    for path, converted in changed:
        temporary = path.with_suffix(".c.tmp")
        with open(temporary, "w", encoding="utf-8", newline="") as file:
            file.write(converted)
        temporary.replace(path)
    print(f"global ctx: {len(changed)} of {len(selected)} selected chunks rewritten")
    return 0


if __name__ == "__main__":
    sys.exit(main())
