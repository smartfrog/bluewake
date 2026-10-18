#!/usr/bin/env python3
"""Rewrite the translated logo-scene request from OPENING_SCENE (8) to NAME_SCENE (12).

Only the generated composite instruction at 0x8022D16C is touched:
the `li r4, 8` that feeds dComIfG_changeOpeningScene at 0x8022D170. The
composite is otherwise unchanged. The marker is the inserted fprintf line.
"""
import argparse
import sys
from pathlib import Path

MARK = '    fprintf(stderr, "[composite-skip] pc=8022D16C scene=8->12\\n");\n'
BEFORE = (
    "    // 8022D16C: li      r4, 8\n"
    "    ctx->gpr[4] = (u32)(s32)(8);\n"
)
AFTER = (
    "    // 8022D16C: li      r4, 12 (bluewake scene skip)\n"
    "    ctx->gpr[4] = (u32)(s32)(12);\n"
    + MARK
)
INCLUDE = '#include "../generated.h"\n'
STDIO = "#include <stdio.h>\n"


def transform(text, path):
    if MARK in text:
        return text
    count = text.count(BEFORE)
    if count != 1:
        raise ValueError(f"{path}: expected one 8022D16C li r4,8 block, found {count}")
    if text.count(INCLUDE) != 1:
        raise ValueError(f"{path}: expected one generated.h include")
    text = text.replace(INCLUDE, INCLUDE + STDIO, 1)
    return text.replace(BEFORE, AFTER, 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("chunk", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    original = args.chunk.read_text(encoding="utf-8", newline="")
    converted = transform(original, args.chunk)
    if converted == original:
        print("composite scene skip: already prepared")
        return 0
    if args.check:
        print("composite scene skip: needs the rewrite")
        return 1
    temporary = args.chunk.with_suffix(".c.tmp")
    temporary.write_text(converted, encoding="utf-8", newline="")
    temporary.replace(args.chunk)
    print("composite scene skip: rewrote 8022D16C 8->12")
    return 0


if __name__ == "__main__":
    sys.exit(main())
