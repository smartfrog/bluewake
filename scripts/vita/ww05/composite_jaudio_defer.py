#!/usr/bin/env python3
"""Defer JAIZelBasic::demoBgmStop while JAInter::SequenceMgr::seqTrackInfo is null."""
import argparse
import sys
from pathlib import Path

MARK = '    fprintf(stderr, "[jaudio-defer] seqTrackInfo=0\\n");\n'
BEFORE = 'label_802AC6C0:\n    ctx->pc = 0x802AC6C0u;\n'
AFTER = (
    'label_802AC6C0:\n'
    '    ctx->pc = 0x802AC6C0u;\n'
    '    if (mem_read32(ctx, ctx->gpr[13] - 27328u) == 0u) {\n'
    '        ctx->pc = ctx->lr & ~3u;\n'
    + MARK +
    '        return;\n'
    '    }\n'
)
INCLUDE = '#include "../generated.h"\n'
STDIO = "#include <stdio.h>\n"


def transform(text, path):
    if MARK in text:
        return text
    count = text.count(BEFORE)
    if count != 1:
        raise ValueError(f"{path}: expected one demoBgmStop entry, found {count}")
    if text.count(INCLUDE) != 1:
        raise ValueError(f"{path}: expected one generated.h include")
    text = text.replace(INCLUDE, INCLUDE + STDIO, 1)
    return text.replace(BEFORE, AFTER, 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("chunk", type=Path)
    args = parser.parse_args()
    original = args.chunk.read_text(encoding="utf-8", newline="")
    converted = transform(original, args.chunk)
    if converted == original:
        print("jaudio defer: already prepared")
        return 0
    temporary = args.chunk.with_suffix(".c.tmp")
    temporary.write_text(converted, encoding="utf-8", newline="")
    temporary.replace(args.chunk)
    print("jaudio defer: inserted seqTrackInfo null guard")
    return 0


if __name__ == "__main__":
    sys.exit(main())
