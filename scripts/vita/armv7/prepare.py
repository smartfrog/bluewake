#!/usr/bin/env python3
"""Build-only seams for the pinned, game-free DolRecomp corpus."""

import re
import struct
import subprocess
import sys


def split_corpus(source):
    functions = list(re.finditer(r"^void func_([0-9A-F]{8})\(CPUState\* ctx\) \{", source, re.M))
    dispatch = source.index("#define DOLRECOMP_ENTRY_POINT")
    if not functions or functions[-1].start() >= dispatch:
        raise ValueError("pinned corpus function layout changed")
    starts = []
    for function in functions:
        helper = re.search(r"^static void loop_" + function.group(1) + r"\(CPUState\* ctx\) \{",
                           source[:function.start()], re.M)
        starts.append(helper.start() if helper else function.start())
    ends = starts[1:] + [dispatch]
    chunks = [(int(function.group(1), 16), source[start:end])
              for function, start, end in zip(functions, starts, ends)]
    prototypes = "".join(f"void func_{address:08X}(CPUState* ctx);\n" for address, _ in chunks)
    return source[:starts[0]] + prototypes + source[dispatch:], chunks


def execution_source(source):
    include = '#include "../src/cpu/cpu.h"'
    if source.count(include) != 1:
        raise ValueError("pinned execution corpus CPU include changed")
    return source.replace(include, '#include "core/cpu.h"', 1)


def corpus_inline_header(source):
    helpers = []
    for name in ("ppc_fp_available_inline", "ppc_psq_load_inline", "ppc_psq_store_inline"):
        matches = re.findall(r"^static inline bool " + name + r"\([^}]+?^\}", source, re.M)
        if len(matches) != 1:
            raise ValueError(f"pinned corpus helper changed: {name}")
        helpers.append(matches[0])
    return '#include "core/cpu.h"\n' + "\n\n".join(helpers) + "\n"


def prepare(root, dolrecomp, corpus, extra, output):
    header, chunks = split_corpus(corpus.read_text())
    extra_source = extra.read_text()
    extra_address = 0x80006000
    if not extra_source.startswith(f"void func_{extra_address:08X}(CPUState* ctx) {{"):
        raise ValueError("stack fixture function layout changed")
    chunks.append((extra_address, extra_source))
    header = header.replace("#define DOLRECOMP_ENTRY_POINT",
                            f"void func_{extra_address:08X}(CPUState* ctx);\n"
                            f"static inline int fixture_extra_range(u32 address) {{\n"
                            f"    return address >= 0x{extra_address:08X}u && address < 0x80006030u;\n"
                            "}\n#define DOLRECOMP_ENTRY_POINT", 1)
    dol = output / "dol"
    (dol / "chunks").mkdir(parents=True, exist_ok=True)
    (dol / "generated.h").write_text(header)
    (dol / "generated_smc.txt").write_text("")
    for address, body in chunks:
        (dol / "chunks" / f"chunk_{address:08X}.c").write_text('#include "../generated.h"\n' + body)

    synthetic = bytearray(0x100 + 0x3030)
    struct.pack_into(">I", synthetic, 0, 0x100)
    struct.pack_into(">I", synthetic, 0x48, 0x80003000)
    struct.pack_into(">I", synthetic, 0x90, 0x3030)
    main_dol = output / "synthetic.dol"
    main_dol.write_bytes(synthetic)
    for directory in ("rels", "rels-bin"):
        (output / directory).mkdir(exist_ok=True)
    composite = output / "composite"
    subprocess.run([sys.executable, str(root / "scripts/generate_composite.py"),
                    "--dol-dir", str(dol), "--rels-dir", str(output / "rels"),
                    "--rels-bin-dir", str(output / "rels-bin"), "--main-dol", str(main_dol),
                    "--game-id", "SYNTH", "--output-dir", str(composite)], check=True)
    subprocess.run([sys.executable, str(root / "scripts/windows/direct_calls.py"), str(composite)],
                   check=True)
    (output / "test_c_execute.c").write_text(execution_source((dolrecomp / "tests/test_c_execute.c").read_text()))
    (output / "corpus_inline.h").write_text(corpus_inline_header((dolrecomp / "src/cpu/cpu.h").read_text()))
