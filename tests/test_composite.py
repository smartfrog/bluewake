#!/usr/bin/env python3
"""Synthetic fixture tests for generate_composite.py (P3 Route A)."""
import hashlib
import importlib.util
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent / "scripts"
sys.path.insert(0, str(SCRIPTS_DIR))

import generate_composite as gc

DOL_TEXT_ADDR = 0x80003100
REL_BASE = 0xC0400000
SHIP_START = 0xC1B100F4
SHIP_SIZE = 55804


def build_dol(text_data):
    hdr = bytearray(0x100)
    struct.pack_into(">I", hdr, 0x00, 0x100)
    struct.pack_into(">I", hdr, 0x48, DOL_TEXT_ADDR)
    struct.pack_into(">I", hdr, 0x90, len(text_data))
    return bytes(hdr) + text_data


def make_generated_h(chunks, entry_point=None):
    lines = ["// synthetic"]
    for start, end in chunks:
        lines.append(f"if (address >= 0x{start:08X}u && address < 0x{end:08X}u)")
        lines.append("    return true;")
    if entry_point is None:
        ep = chunks[0][0] + 0x40 if chunks else DOL_TEXT_ADDR
    else:
        ep = entry_point
    lines.append(f"#define DOLRECOMP_ENTRY_POINT 0x{ep:08X}u")
    for start, _ in chunks:
        lines.append(f"void func_{start:08X}(CPUState* ctx);")
    return "\n".join(lines) + "\n"


def write_dol_dir(d, chunks, native=False):
    d.mkdir(parents=True, exist_ok=True)
    (d / "generated.h").write_text(make_generated_h(chunks))
    (d / "generated_smc.txt").write_text("")
    cd = d / "chunks"
    cd.mkdir(exist_ok=True)
    for start, _ in chunks:
        suffix = ".o" if native else ".c"
        path = cd / f"chunk_{start:08X}{suffix}"
        if native:
            path.write_bytes(b"synthetic native object")
        else:
            path.write_text('#include "../generated.h"\n')


def write_dol_binary(root, text_bytes):
    root.mkdir(parents=True, exist_ok=True)
    (root / "main.dol").write_bytes(build_dol(text_bytes))


def build_rel(module_id, sections_data, imports=None):
    sec_count = len(sections_data)
    fixed_hdr_size = 0x48
    header_size = fixed_hdr_size + sec_count * 8
    imp_offset = header_size
    imp_size = sum(8 + len(v) * 4 for v in (imports or {}).values())
    hdr = bytearray(fixed_hdr_size)
    struct.pack_into(">II", hdr, 0x00, module_id, 1)
    struct.pack_into(">I", hdr, 0x0C, sec_count)
    struct.pack_into(">I", hdr, 0x10, fixed_hdr_size)
    struct.pack_into(">I", hdr, 0x14, imp_size)
    sec_table = bytearray()
    body = bytearray()
    for exec_flag, _, data in sections_data:
        off = header_size + imp_size + len(body)
        body += data
        while len(body) % 4:
            body += b"\x00"
        raw = off | (1 if exec_flag else 0)
        sec_table += struct.pack(">II", raw, len(data))
    imp_table = bytearray()
    for mid in sorted(imports or {}):
        entries = imports[mid]
        imp_table += struct.pack(">II", mid, len(entries) + 1)
        imp_table += struct.pack(">I", 0)
        for e in entries:
            imp_table += e
    result = bytes(hdr[:header_size]) + bytes(sec_table)
    while len(result) < imp_offset:
        result += b"\x00"
    return result + bytes(imp_table) + bytes(body)


def write_rel_module(root, mid, chunks):
    mod_dir = root / f"d_a_test_{mid}"
    mod_dir.mkdir(parents=True)
    ep_rel = min(c[0] for c in chunks) if chunks else REL_BASE
    (mod_dir / "generated.h").write_text(make_generated_h(chunks, entry_point=ep_rel))
    cd = mod_dir / "chunks"
    cd.mkdir(exist_ok=True)
    for start, _ in chunks:
        (cd / f"chunk_{start:08X}.c").write_text('#include "../generated.h"\n')


def write_ship_fixture(td):
    raw = bytearray(56200)
    struct.pack_into('>I', raw, 0, 168)
    struct.pack_into('>II', raw, 0xC, 4, 0x4C)
    struct.pack_into('>I', raw, 0x1C, 3)
    struct.pack_into('>III', raw, 0x24, 56100, 56064, 16)
    raw[0x30:0x33] = bytes([1, 1, 1])
    struct.pack_into('>III', raw, 0x34, 0, 44, 88)
    struct.pack_into('>I', raw, 0x40, 4)
    for index, (offset, size) in enumerate(((0, 0), (245, SHIP_SIZE),
                                           (56048, 8), (56056, 8))):
        struct.pack_into('>II', raw, 0x4C + index * 8, offset, size)
    raw[244:56048] = bytes((i * 13 + 7) & 255 for i in range(SHIP_SIZE))
    struct.pack_into('>I', raw, 252, 0x48000001)
    struct.pack_into('>IIII', raw, 56064, 168, 56100, 0, 56164)
    records = [(0, 202, 1, 0), (2, 6, 2, 0), (4, 4, 2, 0),
               (0, 202, 2, 0), (0, 1, 1, 0xD320),
               (0, 202, 3, 0), (0, 1, 1, 0x94), (0, 203, 0, 0)]
    for index, record in enumerate(records):
        struct.pack_into('>HBBI', raw, 56100 + index * 8, *record)
    for index, record in enumerate(((0, 202, 1, 0), (8, 10, 0, DOL_TEXT_ADDR), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 56164 + index * 8, *record)
    (td / 'rels_bin' / '0168.rel').write_bytes(raw)
    struct.pack_into('>I', raw, 0x40, 8)
    (td / 'input' / 'ship-disc.rel').write_bytes(raw)
    write_rel_module(td / 'rels_out', 168, [(SHIP_START, SHIP_START + SHIP_SIZE)])


def setup_fixture(td, dol_chunks=None, rel_specs=None):
    td = Path(td)
    if dol_chunks is None:
        dol_chunks = [(0x80003100, 0x80003140), (0x80003140, 0x80003180)]
    max_end = max(e for _, e in dol_chunks)
    text = bytes((i * 7 + 0x90) & 0xFF for i in range(max_end - DOL_TEXT_ADDR))
    write_dol_binary(td / "input", text)
    write_dol_dir(td / "dol_out", dol_chunks)
    rb = td / "rels_bin"; rb.mkdir(exist_ok=True)
    ro = td / "rels_out"; ro.mkdir(exist_ok=True)
    write_ship_fixture(td)
    for spec in (rel_specs or []):
        mid = spec["module_id"]
        chunks = spec["chunks"]
        lo = min(c[0] for c in chunks); hi = max(c[1] for c in chunks)
        exec_data = bytes(((i * 13 + 0x42) & 0xFF) for i in range(hi - lo))
        (rb / f"{mid:04d}.rel").write_bytes(build_rel(mid, [(True, None, exec_data)]))
        write_rel_module(ro, mid, chunks)
    return td


def write_ship_disc(td, extra_members=()):
    raw = (td / 'input' / 'ship-disc.rel').read_bytes()
    names = b'ship\0d_a_ship.rel\0'
    archive = bytearray(128 + len(raw))
    archive[:4] = b'RARC'
    struct.pack_into('>III', archive, 4, len(archive), 32, 96)
    struct.pack_into('>6I', archive, 32, 1, 32, 1, 48, len(names), 68)
    struct.pack_into('>HI', archive, 74, 1, 0)
    struct.pack_into('>III', archive, 84, 0x11000005, 0, len(raw))
    archive[100:100 + len(names)] = names
    archive[128:] = raw
    if extra_members:
        members = [('d_a_ship.rel', raw), *extra_members]
        names = bytearray()
        strings_offset = 80 + len(members) * 20
        data_offset = gc.align_up(strings_offset + sum(len(name.encode()) + 1 for name, _ in members), 32)
        archive = bytearray(data_offset)
        archive[:4] = b'RARC'
        struct.pack_into('>II', archive, 8, 32, data_offset - 32)
        struct.pack_into('>6I', archive, 32, 1, 32, len(members), 48, 0, strings_offset - 32)
        struct.pack_into('>HI', archive, 74, len(members), 0)
        for index, (name, content) in enumerate(members):
            struct.pack_into('>III', archive, 84 + index * 20,
                             0x11000000 | len(names), len(archive) - data_offset, len(content))
            names.extend(name.encode() + b'\0')
            archive.extend(content)
        archive[strings_offset:strings_offset + len(names)] = names
        struct.pack_into('>I', archive, 48, len(names))
        struct.pack_into('>I', archive, 4, len(archive))
    dol = (td / 'input' / 'main.dol').read_bytes()
    fst_offset = gc.align_up(0x500 + len(dol), 32)
    fst = struct.pack('>6I', 0x01000000, 0, 2, 0, 0, len(archive)) + b'RELS.arc\0'
    archive_offset = gc.align_up(fst_offset + len(fst), 32)
    fst = bytearray(fst)
    struct.pack_into('>I', fst, 16, archive_offset)
    disc = bytearray(archive_offset + len(archive))
    disc[:6] = b'GZLE01'
    struct.pack_into('>I', disc, 0x1C, 0xC2339F3D)
    struct.pack_into('>III', disc, 0x420, 0x500, fst_offset, len(fst))
    disc[0x500:0x500 + len(dol)] = dol
    disc[fst_offset:fst_offset + len(fst)] = fst
    disc[archive_offset:] = archive
    path = td / 'input' / 'game.iso'
    path.write_bytes(disc)
    return path


def run_generator(td, expect_fail=False, disc_reference=True, identity_sha256=None):
    cmd = [sys.executable, str(SCRIPTS_DIR / "generate_composite.py"),
           "--dol-dir", str(Path(td)/"dol_out"), "--rels-dir", str(Path(td)/"rels_out"),
           "--rels-bin-dir", str(Path(td)/"rels_bin"), "--main-dol", str(Path(td)/"input"/"main.dol"),
           "--output-dir", str(Path(td)/"composite")]
    if disc_reference:
        cmd += ['--ship-disc', str(write_ship_disc(Path(td)))]
    if identity_sha256 is not None:
        cmd += ['--ship-rel-sha256', identity_sha256]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if expect_fail:
        assert r.returncode != 0, f"expected failure:\nstdout={r.stdout}\nstderr={r.stderr}"
        return r
    assert r.returncode == 0, f"generator failed:\nstdout={r.stdout}\nstderr={r.stderr}"
    return r


def test_single_dol_no_rels():
    """The ordinary pipeline cannot silently omit the ship contract."""
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        import shutil
        shutil.rmtree(td / 'rels_out' / 'd_a_test_168')
        result = run_generator(td, expect_fail=True)
        assert 'ship contract requires translated module 168' in result.stderr
        assert not (td / 'composite' / 'rel_data.inc').exists()


def test_dol_dispatch_with_ship_contract():
    chunks = [(0x80003100, 0x80003140), (0x80003140, 0x80003200)]
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td, dol_chunks=chunks)
        run_generator(td)
        out = Path(td) / "composite"
        h = (out / "generated_composite.h").read_text()
        for start, _ in chunks:
            assert f"func_{start:08X}" in h
        assert "dolrecomp_block_can_precharge" in h
        assert "ctx->cycle_deadline_budget + ctx->downcount" in h
        assert "dolrecomp_refund_cycle_suffix" in h
        assert "ctx->downcount += (s64)ctx->cycle_observation_suffix" in h
        # The dispatch cache is pc-keyed and exact, and the dispatcher is split
        # so the path every block boundary takes carries no cold code. Both are
        # measured properties of the emitted text (docs/status/CURRENT.md,
        # 2026-09-22); the reason they are checked here is that a cache which
        # answers for a pc it never resolved is a boot failure, not a slowdown,
        # and a dispatcher that lost its alias fallbacks is a wrong answer.
        assert "DOLRECOMP_PC_CACHE_SIZE = 4096u" in h
        assert "((address >> 2) ^ (address >> 14))" in h
        assert "if (s_cached_pc[cache_index] == address)" in h
        assert "return s_cached_pc_fn[cache_index];" in h
        assert "s_cached_pc[cache_index] = address;" in h
        assert "s_cached_pc_fn[cache_index] = s_fns[idx];" in h
        assert "s_cached_starts" not in h and "s_cached_ends" not in h
        assert "dolrecomp_call_slow" in h
        assert "return dolrecomp_call_slow(ctx, address);" in h
        assert "dolrecomp_physical_pc_alias(ctx, address, &alias)" in h
        tbl = (out / "module_tables.inc").read_text()
        for s, e in chunks:
            assert f"{{0x{s:08X}u, 0x{e:08X}u}}" in tbl
        assert "MODULE_CHUNK_RANGE_COUNT 3u" in tbl
        rm = (out / "rel_modules.inc").read_text()
        assert "MODULE_REL_MODULE_COUNT 1u" in rm
        data = (out / "rel_data.inc").read_text()
        assert "MODULE_REL_LIFECYCLE_V2_COUNT 1u" in data


def test_native_dol_chunks_are_preserved():
    """LLVM DOL objects pass through while REL inputs remain C sources."""
    chunks = [(0x80003100, 0x80003140), (0x80003140, 0x80003200)]
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td, dol_chunks=chunks)
        dol_chunks = td / "dol_out" / "chunks"
        for source in dol_chunks.glob("*.c"):
            source.unlink()
        for start, _ in chunks:
            (dol_chunks / f"chunk_{start:08X}.o").write_bytes(
                b"synthetic native object")
        result = run_generator(td)
        out = td / "composite" / "chunks_dol"
        assert sorted(path.suffix for path in out.iterdir()) == [".o", ".o"]
        source = dol_chunks / "chunk_80003100.o"
        copied = out / "chunk_80003100.o"
        assert source.stat().st_ino == copied.stat().st_ino
        assert "DOL backend: native objects (2 chunks)" in result.stdout
    print("PASS: test_native_dol_chunks_are_preserved")


def test_mixed_dol_backends_are_rejected():
    """A stale output tree cannot silently link duplicate guest symbols."""
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        chunks = td / "dol_out" / "chunks"
        (chunks / "stale.o").write_bytes(b"synthetic native object")
        result = run_generator(td, expect_fail=True)
        assert "exactly one of C or native object output" in result.stderr
    print("PASS: test_mixed_dol_backends_are_rejected")


def test_multi_section_rel():
    """REL module with exec section produces correct section table."""
    mid = 1
    b = REL_BASE + mid * 0x10000
    rc = [(b + 0x100, b + 0x200)]
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td, rel_specs=[{"module_id": mid, "chunks": rc}])
        run_generator(td)
        rm = (Path(td) / "composite" / "rel_modules.inc").read_text()
        assert f"s_rel_sec_{mid}[]" in rm
        assert "MODULE_REL_MODULE_COUNT 2u" in rm
    print("PASS: test_multi_section_rel")


def test_bss_section_gets_linked_address():
    """Zero-offset REL BSS follows its header alignment, not a fixed value."""
    rel = bytearray(0x48)
    struct.pack_into(">IIII", rel, 0, 7, 1, 0, 2)
    struct.pack_into(">I", rel, 0x10, 0x20)
    struct.pack_into(">II", rel, 0x20, 0x29, 4)  # executable, file-backed
    struct.pack_into(">II", rel, 0x28, 0, 12)  # zero-offset BSS
    struct.pack_into(">I", rel, 0x44, 8)  # BSS alignment
    rel.extend(b"ABCD")  # file size 0x4c, so BSS starts at 0x50
    sections = gc.parse_rel_sections(bytes(rel), 2, 0x20, REL_BASE)
    assert sections[0] == (0, 0xC0400028, 4)
    assert sections[1] == (1, 0xC0400050, 12)

    linked = gc.rel_linked_sections(bytes(rel), REL_BASE)
    assert linked[1]["linked_start"] == 0xC0400050
    print("PASS: test_bss_section_gets_linked_address")


def test_bss_zero_alignment_uses_four_byte_fallback():
    rel = bytearray(0x49)
    struct.pack_into(">IIII", rel, 0, 8, 1, 0, 1)
    struct.pack_into(">I", rel, 0x10, 0x20)
    struct.pack_into(">II", rel, 0x20, 0, 4)
    struct.pack_into(">I", rel, 0x44, 0)
    sections = gc.parse_rel_sections(bytes(rel), 1, 0x20, REL_BASE)
    assert sections[0] == (0, 0xC040004C, 4)
    print("PASS: test_bss_zero_alignment_uses_four_byte_fallback")


def test_rel_namespace_rejects_retail_memory():
    sections = {
        60: {
            "base": 0x81550000,
            "sections": [{
                "index": 6,
                "linked_start": 0x815581A0,
                "size": 0x214,
            }],
        }
    }
    try:
        gc.validate_rel_linked_namespace(sections)
    except ValueError as exc:
        assert "retail MEM1" in str(exc)
    else:
        raise AssertionError("retail-memory REL namespace was accepted")
    print("PASS: test_rel_namespace_rejects_retail_memory")


def test_tagged_rel_namespace_is_disjoint():
    sections = {
        60: {
            "base": 0xC1550000,
            "sections": [{
                "index": 6,
                "linked_start": 0xC15581A0,
                "size": 0x214,
            }],
        }
    }
    gc.validate_rel_linked_namespace(sections)
    print("PASS: test_tagged_rel_namespace_is_disjoint")


def test_chunk_hash_correctness():
    """FNV-1a hashes match original DOL/REL bytes for each chunk."""
    dc = [(0x80003100, 0x80003140)]
    mid = 5
    b5 = REL_BASE + mid * 0x10000
    rc = [(b5 + 0x100, b5 + 0x200)]
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td, dol_chunks=dc, rel_specs=[{"module_id": mid, "chunks": rc}])
        run_generator(td)
        gen_h_text = (Path(td) / "rels_out" / f"d_a_test_{mid}" / "generated.h").read_text()
        ep_rel = int(re.search(r"DOLRECOMP_ENTRY_POINT 0x([0-9A-F]{8})u", gen_h_text).group(1), 16)
        tbl = (Path(td) / "composite" / "module_tables.inc").read_text()
        hashes = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{16})ull,", tbl)]
        assert len(hashes) == 3, f"expected 3 hashes, got {len(hashes)}"
        # DOL chunk hash
        dol_bin = (Path(td) / "input" / "main.dol").read_bytes()
        s, e = dc[0]
        lo = 0x100 + (s - DOL_TEXT_ADDR)
        assert hashes[0] == gc.fnv1a64(dol_bin[lo : lo + (e - s)]), "DOL hash mismatch"
        # REL chunk hash
        rel_bin = (Path(td) / "rels_bin" / f"{mid:04d}.rel").read_bytes()
        nsec = struct.unpack_from(">I", rel_bin, 0xC)[0]
        sio = struct.unpack_from(">I", rel_bin, 0x10)[0]
        for i in range(nsec):
            raw_off, sz = struct.unpack_from(">II", rel_bin, sio + i * 8)
            if raw_off & 1 and sz > 0:
                off = raw_off & ~1
                lo2 = off + (rc[0][0] - ep_rel)
                data = rel_bin[lo2 : lo2 + (rc[0][1] - rc[0][0])]
                assert hashes[1] == gc.fnv1a64(data), "REL hash mismatch"
                break
    print("PASS: test_chunk_hash_correctness")


def test_dispatch_sorted_no_overlap():
    """Chunk starts are sorted and non-overlapping across DOL and RELs."""
    dc = [(0x80003100, 0x80003140), (0x80003140, 0x80003200)]
    specs = []
    for mid in (3, 7):
        b = REL_BASE + mid * 0x10000
        specs.append({"module_id": mid,
                      "chunks": [(b + 0x100, b + 0x200), (b + 0x200, b + 0x300)]})
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td, dol_chunks=dc, rel_specs=specs)
        run_generator(td)
        h = (Path(td) / "composite" / "generated_composite.h").read_text()
        # The probe is pc-keyed, so the range test it used to carry lives in the
        # miss path; what this asserts is that the emitted probe still tests the
        # entry it is about to use, and that the tables it searches are sorted and
        # disjoint - which is the invariant the chunk dispatch depends on.
        assert "s_cached_pc[cache_index] == address" in h
        assert "s_cached_pc_fn[cache_index]" in h
        starts_m = re.search(r"static const u32 s_starts\[\] = \{(.*?)\};", h, re.S)
        ends_m = re.search(r"static const u32 s_ends\[\] = \{(.*?)\};", h, re.S)
        assert starts_m and ends_m
        starts = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{8})u", starts_m.group(1))]
        ends = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{8})u", ends_m.group(1))]
        assert starts == sorted(starts)
        for i in range(len(starts) - 1):
            assert starts[i + 1] >= ends[i], f"overlap at {starts[i]:#010x}"
    print("PASS: test_dispatch_sorted_no_overlap")


def test_overlapping_rejected():
    """Two RELs claiming the same address cause generator failure."""
    shared_lo = REL_BASE + 0x10000 + 0x100
    specs_a = [{"module_id": 1, "chunks": [(shared_lo, shared_lo + 0x100)]}]
    specs_b = [{"module_id": 2, "chunks": [(shared_lo, shared_lo + 0x80)]}]
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td, rel_specs=specs_a + specs_b)
        r = run_generator(td, expect_fail=True)
        assert "overlap" in r.stderr.lower(), f"got: {r.stderr}"
    print("PASS: test_overlapping_rejected")


def test_missing_rel_binary_fails():
    """Missing .rel binary when a REL module dir exists causes failure."""
    mid = 99
    b = REL_BASE + mid * 0x10000
    rc = [(b + 0x100, b + 0x200)]
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td, rel_specs=[{"module_id": mid, "chunks": rc}])
        (Path(td) / "rels_bin" / f"{mid:04d}.rel").unlink()
        r = run_generator(td, expect_fail=True)
        assert ".rel" in r.stderr.lower(), f"got: {r.stderr}"
    print("PASS: test_missing_rel_binary_fails")


def test_address_reuse():
    """Two different RELs can occupy the same address slot in separate builds."""
    shared = REL_BASE + 0x10000
    for mid in (1, 2):
        with tempfile.TemporaryDirectory() as td:
            td = setup_fixture(td, rel_specs=[{"module_id": mid,
                             "chunks": [(shared + 0x100, shared + 0x200)]}])
            run_generator(td)
            tbl = (Path(td) / "composite" / "module_tables.inc").read_text()
            assert f"0x{shared + 0x100:08X}u" in tbl
    print("PASS: test_address_reuse")


def test_determinism():
    """Two runs on identical input produce byte-identical output."""
    dc = [(0x80003100, 0x80003140)]
    mid = 4; b = REL_BASE + mid * 0x10000
    rc = [(b + 0x100, b + 0x200)]
    d1, d2 = {}, {}
    for store in (d1, d2):
        with tempfile.TemporaryDirectory() as td:
            td = setup_fixture(td, dol_chunks=dc, rel_specs=[{"module_id": mid, "chunks": rc}])
            run_generator(td)
            out = Path(td) / "composite"
            for f in ["generated.h", "generated_composite.h", "module_tables.inc", "rel_modules.inc", "rel_data.inc", "generation_receipt.json"]:
                store[f] = hashlib.sha256((out / f).read_bytes()).hexdigest()
    assert d1 == d2, f"non-deterministic"
    print("PASS: test_determinism")

def test_indirect_call_lookup():
    """Each aligned address within a chunk maps to at most one entry."""
    dc = [(0x80003100, 0x80003140), (0x80003140, 0x80003200)]
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td, dol_chunks=dc)
        run_generator(td)
        h = (Path(td) / "composite" / "generated_composite.h").read_text()
        starts_m = re.search(r"static const u32 s_starts\[\] = \{(.*?)\};", h, re.S)
        ends_m = re.search(r"static const u32 s_ends\[\] = \{(.*?)\};", h, re.S)
        starts = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{8})u", starts_m.group(1))]
        ends = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{8})u", ends_m.group(1))]
        for probe in [0x80003100, 0x80003104, 0x8000313C, 0x80003140, 0x800031FC]:
            m = [i for i in range(len(starts)) if starts[i] <= probe < ends[i]]
            assert len(m) <= 1
            if m:
                assert (probe - starts[m[0]]) % 4 == 0
    print("PASS: test_indirect_call_lookup")


def test_lifecycle_metadata_complete():
    """All REL modules appear in rel_modules.inc with section tables."""
    specs = []
    for mid in (1, 2, 3):
        b = REL_BASE + mid * 0x10000
        specs.append({"module_id": mid,
                      "chunks": [(b + 0x100, b + 0x200), (b + 0x200, b + 0x280)]})
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td, rel_specs=specs)
        run_generator(td)
        rm = (Path(td) / "composite" / "rel_modules.inc").read_text()
        assert "MODULE_REL_MODULE_COUNT 4u" in rm
        for mid in (1, 2, 3):
            assert f"s_rel_sec_{mid}[]" in rm
    print("PASS: test_lifecycle_metadata_complete")


def lifecycle_fixture():
    rel = bytearray(build_rel(37, [(False, None, b""),
                                  (True, None, bytes(32))]))
    rel[0x30:0x33] = bytes([1, 1, 1])
    struct.pack_into(">III", rel, 0x34, 0, 12, 28)
    sections = gc.rel_linked_sections(rel, REL_BASE)
    start = sections[1]["linked_start"]
    chunks = [(start, start + 16), (start + 16, start + 32)]
    return rel, sections, chunks


def test_lifecycle_all_entries_across_chunks():
    rel, sections, chunks = lifecycle_fixture()
    start = sections[1]["linked_start"]
    assert gc.rel_lifecycle_addresses(rel, sections, chunks) == (start, start + 12, start + 28)


def test_lifecycle_absent_entries():
    rel, sections, chunks = lifecycle_fixture()
    rel[0x30:0x33] = bytes(3)
    struct.pack_into(">III", rel, 0x34, 0, 0, 0)
    assert gc.rel_lifecycle_addresses(rel, sections, chunks) == (0, 0, 0)


def test_lifecycle_invalid_headers():
    cases = [("section", 0x31, 9), ("offset", 0x38, 32),
             ("alignment", 0x3C, 3), ("absent", 0x31, 0)]
    for label, offset, value in cases:
        rel, sections, chunks = lifecycle_fixture()
        if offset < 0x34:
            rel[offset] = value
        else:
            struct.pack_into(">I", rel, offset, value)
        try:
            gc.rel_lifecycle_addresses(rel, sections, chunks)
        except ValueError as exc:
            assert "lifecycle" in str(exc), (label, str(exc))
        else:
            raise AssertionError(f"accepted invalid lifecycle {label}")


def test_lifecycle_requires_translated_coverage():
    rel, sections, chunks = lifecycle_fixture()
    try:
        gc.rel_lifecycle_addresses(rel, sections, chunks[:1])
    except ValueError as exc:
        assert "unresolved" in str(exc) and "translated" in str(exc)
    else:
        raise AssertionError("accepted untranslated lifecycle entry")


def test_lifecycle_export_preserves_legacy_stride():
    rel, sections, chunks = lifecycle_fixture()
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        (td / "rels_bin" / "0037.rel").write_bytes(rel)
        write_rel_module(td / "rels_out", 37, chunks)
        run_generator(td)
        text = (td / "composite" / "rel_data.inc").read_text()
        start = sections[1]["linked_start"]
        assert f"{{37u, 0x{start:08X}u}}," in text
        assert "static const BlueWakeRelLifecycleV2 s_rel_lifecycle_v2[]" in text
        assert f"{{37u, 0x{start:08X}u, 0x{start + 12:08X}u, 0x{start + 28:08X}u}}," in text
        assert "#define MODULE_REL_LIFECYCLE_V2_COUNT 2u" in text


def test_lifecycle_invalid_executable_sections():
    cases = [("executable", False), ("offset", 0), ("size", 0),
             ("size", 31), ("offset", 0xFFFF), ("linked_start", 0),
             ("linked_start", 0x100000000), ("linked_start", REL_BASE + 1)]
    for key, value in cases:
        rel, sections, chunks = lifecycle_fixture()
        sections[1][key] = value
        try:
            gc.rel_lifecycle_addresses(rel, sections, chunks)
        except ValueError as exc:
            assert "invalid executable offset" in str(exc)
        else:
            raise AssertionError(f"accepted invalid lifecycle section {key}={value}")


def test_lifecycle_truncated_header():
    rel, sections, chunks = lifecycle_fixture()
    try:
        gc.rel_lifecycle_addresses(rel[:0x3F], sections, chunks)
    except ValueError as exc:
        assert "truncated" in str(exc)
    else:
        raise AssertionError("accepted truncated lifecycle header")


def test_generator_rejects_invalid_lifecycle():
    rel, sections, chunks = lifecycle_fixture()
    struct.pack_into(">I", rel, 0x38, 32)
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        (td / "rels_bin" / "0037.rel").write_bytes(rel)
        write_rel_module(td / "rels_out", 37, chunks)
        result = run_generator(td, expect_fail=True)
        assert "REL module 37: REL lifecycle epilog" in result.stderr
        assert not (td / "composite" / "rel_data.inc").exists()


def test_import_across_modules():
    """A REL with imports still generates correctly alongside its dependency."""
    imp_mid, dep_mid = 10, 11
    bi = REL_BASE + imp_mid * 0x10000
    bd = REL_BASE + dep_mid * 0x10000
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        write_dol_binary(td / "input", b"\x90" * 0x40)
        write_dol_dir(td / "dol_out", [(0x80003100, 0x80003140)])
        rb = td / "rels_bin"; rb.mkdir(exist_ok=True)
        ro = td / "rels_out"; ro.mkdir(exist_ok=True)
        dep_exec = b"\xAB" * 0x100
        dep_chunks = [(bd + 0x100, bd + 0x200)]
        (rb / f"{dep_mid:04d}.rel").write_bytes(build_rel(dep_mid, [(True, None, dep_exec)]))
        write_rel_module(ro, dep_mid, dep_chunks)
        entries = [struct.pack(">I", (6 << 26) | (0 << 16) | 0x100),
                   struct.pack(">I", (5 << 26) | (0 << 16) | 0x104),
                   struct.pack(">I", (4 << 26) | (0 << 16) | 0x108)]
        imp_exec = b"\xCD" * 0x100
        imp_chunks = [(bi + 0x100, bi + 0x200)]
        (rb / f"{imp_mid:04d}.rel").write_bytes(
            build_rel(imp_mid, [(True, None, imp_exec)], imports={dep_mid: entries}))
        write_rel_module(ro, imp_mid, imp_chunks)
        run_generator(td)
        rm = (td / "composite" / "rel_modules.inc").read_text()
        assert "MODULE_REL_MODULE_COUNT 3u" in rm
    print("PASS: test_import_across_modules")


def test_zero_length_section():
    """A REL section with size 0 is handled without error."""
    mid = 20; b = REL_BASE + mid * 0x10000
    chunks = [(b + 0x100, b + 0x200)]
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        write_dol_binary(td / "input", b"\x90" * 0x40)
        write_dol_dir(td / "dol_out", [(0x80003100, 0x80003140)])
        rb = td / "rels_bin"; rb.mkdir(exist_ok=True)
        ro = td / "rels_out"; ro.mkdir(exist_ok=True)
        exec_data = b"\xEF" * 0x100
        (rb / f"{mid:04d}.rel").write_bytes(
            build_rel(mid, [(False, 0, b""), (True, None, exec_data)]))
        write_rel_module(ro, mid, chunks)
        run_generator(td)
        rm = (td / "composite" / "rel_modules.inc").read_text()
        assert f"s_rel_sec_{mid}[]" in rm
    print("PASS: test_zero_length_section")


def read_c_array(text, name):
    match = re.search(rf'static const u8 {name}\[\] = \{{(.*?)\}};', text, re.S)
    assert match, name
    return bytes(int(value, 0) for value in re.findall(r'\b(?:0x[0-9A-Fa-f]+|[0-9]+)\b', match[1]))


def test_ship_contract_emitted_from_normal_inputs():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        run_generator(td)
        text = (td / 'composite' / 'rel_data.inc').read_text()
        assert '#define MODULE_SHIP_CONTRACT_PRESENT 1' in text
        assert 'static const BlueWakeShipContractV1 s_ship_contract' in text
        assert read_c_array(text, 's_ship_raw_dol') == (td / 'input' / 'main.dol').read_bytes()
        assert read_c_array(text, 's_ship_raw_rel') == (td / 'input' / 'ship-disc.rel').read_bytes()
        canonical = read_c_array(text, 's_ship_canonical_1')
        assert len(canonical) == SHIP_SIZE
        assert struct.unpack_from('>H', canonical, 2)[0] == 0xC1B2
        assert struct.unpack_from('>H', canonical, 6)[0] == 0xDAF0
        assert read_c_array(text, 's_ship_canonical_2') == struct.pack('>II', 0xC1B1D414, 0)
        assert read_c_array(text, 's_ship_canonical_3') == struct.pack('>II', 0xC1B10188, 0)
        receipt = json.loads((td / 'composite' / 'generation_receipt.json').read_text())
        contract = receipt['ship_contract']
        assert contract['version'] == 1 and contract['module_id'] == 168
        assert contract['exec'] == {'index': 1, 'offset': 244, 'linked_start': SHIP_START, 'size': SHIP_SIZE}
        assert contract['lifecycle'] == {'prolog': SHIP_START, 'epilog': 0xC1B10120, 'unresolved': 0xC1B1014C}
        assert contract['ctors'] == 0xC1B1DAF0 and contract['dtors'] == 0xC1B1DAF8
        assert contract['section_count'] == 3
        for name, path in [('dol', td / 'input' / 'main.dol'), ('rel', td / 'input' / 'ship-disc.rel')]:
            raw = path.read_bytes()
            assert contract[f'{name}_size'] == len(raw)
            assert contract[f'{name}_sha256'] == hashlib.sha256(raw).hexdigest()
        for name, digest in receipt['output_sha256'].items():
            assert hashlib.sha256((td / 'composite' / name).read_bytes()).hexdigest() == digest
        fields = dict(contract)
        identity = fields.pop('build_identity')
        fields.pop('sha256')
        assert hashlib.sha256(json.dumps(fields, sort_keys=True, separators=(',', ':')).encode()).hexdigest() == identity
        assert f'1u,384u,s_ship_raw_dol,56200u,s_ship_raw_rel,s_ship_canonical,3u,"{contract["build_identity"]}"' in text
        contract_text = text[text.index('#define MODULE_SHIP_CONTRACT_PRESENT'):]
        assert contract['sha256'] == hashlib.sha256(contract_text.encode()).hexdigest()


def test_ship_chunk_hashes_use_canonical_executable_bytes():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        run_generator(td)
        out = td / 'composite'
        canonical = read_c_array((out / 'rel_data.inc').read_text(), 's_ship_canonical_1')
        hashes = re.findall(r'0x([0-9A-F]{16})ull,', (out / 'module_tables.inc').read_text())
        assert int(hashes[-1], 16) == gc.fnv1a64(canonical)


def test_ship_contract_rejects_changed_layout():
    cases = [('exec offset', 0x54, 249), ('exec size', 0x58, SHIP_SIZE - 4),
             ('ctors address', 0x5C, 56052), ('dtors address', 0x64, 56060)]
    for label, offset, value in cases:
        assert_ship_change_rejected(offset, struct.pack('>I', value), label=label)


def test_ship_contract_rejects_changed_lifecycle():
    for index, name in enumerate(('prolog', 'epilog', 'unresolved')):
        assert_ship_change_rejected(0x34 + index * 4, struct.pack('>I', index * 44 + 4),
                                    diagnostic=f'ship contract {name}')


def test_ship_contract_requires_raw_module_168():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        (td / 'rels_bin' / '0168.rel').unlink()
        result = run_generator(td, expect_fail=True)
        assert 'ship contract requires raw module 168' in result.stderr


def test_ship_contract_rejects_ambiguous_raw_input():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        (td / 'rels_bin' / 'duplicate.rel').write_bytes((td / 'rels_bin' / '0168.rel').read_bytes())
        result = run_generator(td, expect_fail=True)
        assert 'ambiguous .rel binaries for mid=168' in result.stderr
        assert not (td / 'composite' / 'generation_receipt.json').exists()


def test_ship_contract_rejects_invalid_relocations():
    cases = [('type', 56110, b'\x05'), ('patch', 56108, struct.pack('>H', 55804)),
             ('target', 56112, struct.pack('>I', 8)),
             ('end', 56158, b'\xC9'), ('import', 56064, struct.pack('>I', 999)),
             ('branch coverage', 56176, struct.pack('>I', 0x80005000)),
             ('branch opcode', 252, struct.pack('>I', 0x60000000)),
             ('ctor target', 56136, struct.pack('>I', SHIP_SIZE)),
             ('ctor terminator', 56052, struct.pack('>I', 1))]
    for label, offset, value in cases:
        assert_ship_change_rejected(offset, value, label=label)


def assert_ship_change_rejected(offset, value, diagnostic='ship contract', label=''):
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        path = td / 'rels_bin' / '0168.rel'
        raw = bytearray(path.read_bytes())
        raw[offset:offset + len(value)] = value
        path.write_bytes(raw)
        identity_path = td / 'input' / 'ship-disc.rel'
        identity_raw = bytearray(identity_path.read_bytes())
        identity_raw[offset:offset + len(value)] = value
        identity_path.write_bytes(identity_raw)
        result = run_generator(td, expect_fail=True)
        assert diagnostic in result.stderr, (label, result.stderr)
        assert not (td / 'composite' / 'rel_data.inc').exists()


def test_ship_dol_branch_relocation_retains_linked_tag():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        run_generator(td)
        text = (td / 'composite' / 'rel_data.inc').read_text()
        canonical = read_c_array(text, 's_ship_canonical_1')
        word = struct.unpack_from('>I', canonical, 8)[0]
        displacement = word & 0x03FFFFFC
        if displacement & 0x02000000:
            displacement -= 0x04000000
        assert SHIP_START + 8 + displacement == DOL_TEXT_ADDR | 0x40000000
        assert word & 3 == 1


def test_ship_contract_additive_abi_and_chunk_include_set():
    root = SCRIPTS_DIR.parent
    header = (root / 'cmake/composite/module_exports.h').read_text()
    assert 'typedef struct BlueWakeShipContractV1' in header
    assert 'staticrecomp_get_ship_contract_v1(void)' in header
    export = (root / 'cmake/composite/module_export.c').read_text()
    assert '#ifdef MODULE_SHIP_CONTRACT_PRESENT\n    return &s_ship_contract;' in export
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        run_generator(td)
        out = td / 'composite'
        for source in out.glob('chunks_*/*.c'):
            assert source.read_text() == '#include "../generated.h"\n'
        translated_header = (out / 'generated.h').read_text()
        assert re.findall(r'^#include (.*)$', translated_header, re.M) == [
            '<string.h>', '<math.h>', 'DOLRECOMP_CPU_HEADER']
        assert 'module_exports.h' not in translated_header
        assert 'BlueWakeShipContract' not in translated_header


def test_failed_regeneration_invalidates_success_receipt():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        run_generator(td)
        path = td / 'input' / 'ship-disc.rel'
        raw = bytearray(path.read_bytes())
        raw[8] ^= 1
        path.write_bytes(raw)
        result = run_generator(td, expect_fail=True)
        assert 'ship identity and processed inputs differ outside header alignment word 0x40' in result.stderr
        assert not (td / 'composite' / 'generation_receipt.json').exists()


def test_generated_contract_compiles_with_existing_export_abi():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        run_generator(td)
        (td / 'core').mkdir()
        (td / 'core' / 'cpu.h').write_text('typedef struct CPUState CPUState;\n')
        (td / 'StaticRecompABI.h').write_text(
            '#include <stdint.h>\ntypedef uint8_t u8;\ntypedef uint32_t u32;\n'
            'typedef struct StaticRecompModuleDesc StaticRecompModuleDesc;\n')
        glue = (SCRIPTS_DIR.parent / 'cmake/composite/module_export.c').read_text()
        getter = re.search(r'RECOMP_MODULE_EXPORT const BlueWakeShipContractV1\* '
                           r'staticrecomp_get_ship_contract_v1\(void\)\n\{.*?\n\}', glue, re.S)[0]
        source = td / 'contract_probe.c'
        source.write_text('#include <assert.h>\n#include <stddef.h>\n#include "module_exports.h"\n'
                          '#include "rel_data.inc"\n#define RECOMP_MODULE_EXPORT\n' + getter + '\n'
                          'int main(void) {\n'
                          'const BlueWakeShipContractV1* c = staticrecomp_get_ship_contract_v1();\n'
                          'assert(c != NULL && c->version == 1u && c->dol_size == 384u);\n'
                          'assert(c->rel_size == 56200u && c->section_count == 3u);\n'
                          'assert(c->canonical_sections[0].linked_start == 0xC1B100F4u);\n'
                          'assert(c->canonical_sections[0].size == 55804u);\n'
                          'assert(c->canonical_sections[1].linked_start == 0xC1B1DAF0u);\n'
                          'assert(c->canonical_sections[2].linked_start == 0xC1B1DAF8u);\n'
                          'return 0;\n}\n')
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-const-variable', '-I', str(td), '-I', str(td / 'composite'),
                        '-I', str(SCRIPTS_DIR.parent / 'cmake/composite'), str(source),
                        '-o', str(td / 'contract_probe')], check=True, capture_output=True, text=True)
        subprocess.run([str(td / 'contract_probe')], check=True, capture_output=True, text=True)


def test_ship_identity_uses_disc_member_not_processed_member():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        disc_raw = (td / 'input' / 'ship-disc.rel').read_bytes()
        processed = (td / 'rels_bin' / '0168.rel').read_bytes()
        expected = hashlib.sha256(disc_raw).hexdigest()
        run_generator(td, disc_reference=True, identity_sha256=expected)
        out = td / 'composite'
        assert read_c_array((out / 'rel_data.inc').read_text(), 's_ship_raw_rel') == disc_raw
        receipt = json.loads((out / 'generation_receipt.json').read_text())['ship_contract']
        assert receipt['rel_sha256'] == expected
        assert receipt['processed_rel_sha256'] == hashlib.sha256(processed).hexdigest()
        assert receipt['section_alignment'] == {'identity': 8, 'processed': 4}
        for section in receipt['canonical_sections']:
            at, size = section['offset'], section['size']
            assert section['raw_sha256'] == hashlib.sha256(disc_raw[at:at + size]).hexdigest()
            emitted = read_c_array((out / 'rel_data.inc').read_text(), f's_ship_canonical_{section["index"]}')
            assert section['sha256'] == hashlib.sha256(emitted).hexdigest()


def test_ship_sources_reject_differences_outside_alignment_word():
    for offset in (0x08, 0x43 + 1, 244, 56048, 56112):
        with tempfile.TemporaryDirectory() as td:
            td = setup_fixture(td)
            path = td / 'input' / 'ship-disc.rel'
            raw = bytearray(path.read_bytes())
            raw[offset] ^= 1
            path.write_bytes(raw)
            result = run_generator(td, expect_fail=True, disc_reference=True)
            assert 'ship identity and processed inputs differ outside header alignment word 0x40' in result.stderr
            assert not (td / 'composite' / 'generation_receipt.json').exists()


def test_ship_disc_section_table_is_checked_directly():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        path = td / 'input' / 'ship-disc.rel'
        raw = bytearray(path.read_bytes())
        struct.pack_into('>I', raw, 0x58, SHIP_SIZE - 4)
        path.write_bytes(raw)
        result = run_generator(td, expect_fail=True, disc_reference=True)
        assert 'ship contract section metadata disagrees with raw REL bytes' in result.stderr


def test_ship_identity_hash_pin_rejects_processed_identity():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        expected = hashlib.sha256((td / 'input' / 'ship-disc.rel').read_bytes()).hexdigest()
        (td / 'input' / 'ship-disc.rel').write_bytes((td / 'rels_bin' / '0168.rel').read_bytes())
        result = run_generator(td, expect_fail=True, disc_reference=True, identity_sha256=expected)
        assert 'ship disc-member identity SHA-256 mismatch' in result.stderr


def test_ship_requires_disc_reference_without_stale_success_receipt():
    with tempfile.TemporaryDirectory() as td:
        td = setup_fixture(td)
        run_generator(td)
        result = run_generator(td, expect_fail=True, disc_reference=False)
        assert 'ship contract requires a disc-member identity reference' in result.stderr
        assert not (td / 'composite' / 'generation_receipt.json').exists()


def test_desktop_builders_forward_the_original_disc():
    for platform in ('linux', 'windows'):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            spec = importlib.util.spec_from_file_location(f'{platform}_builder', SCRIPTS_DIR / platform / 'build.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            builder = module.Builder.__new__(module.Builder)
            builder.iso = td / 'owned-disc.iso'
            commands = []
            builder.run = lambda name, command: commands.append(command)
            builder.composite(td / 'dol', td / 'rels', td / 'rels_bin', td / 'main.dol', td / 'out', 'generate')
            assert len(commands) == 1
            index = commands[0].index('--ship-disc')
            assert commands[0][index + 1] == builder.iso


def main():
    tests = [
        test_single_dol_no_rels,
        test_dol_dispatch_with_ship_contract,
        test_native_dol_chunks_are_preserved,
        test_mixed_dol_backends_are_rejected,
        test_multi_section_rel,
        test_bss_section_gets_linked_address,
        test_bss_zero_alignment_uses_four_byte_fallback,
        test_rel_namespace_rejects_retail_memory,
        test_tagged_rel_namespace_is_disjoint,
        test_chunk_hash_correctness,
        test_dispatch_sorted_no_overlap,
        test_overlapping_rejected,
        test_missing_rel_binary_fails,
        test_address_reuse,
        test_determinism,
        test_indirect_call_lookup,
        test_lifecycle_metadata_complete,
        test_lifecycle_all_entries_across_chunks,
        test_lifecycle_absent_entries,
        test_lifecycle_invalid_headers,
        test_lifecycle_requires_translated_coverage,
        test_lifecycle_export_preserves_legacy_stride,
        test_lifecycle_invalid_executable_sections,
        test_lifecycle_truncated_header,
        test_generator_rejects_invalid_lifecycle,
        test_import_across_modules,
        test_zero_length_section,
        test_ship_contract_emitted_from_normal_inputs,
        test_ship_chunk_hashes_use_canonical_executable_bytes,
        test_ship_contract_rejects_changed_layout,
        test_ship_contract_rejects_changed_lifecycle,
        test_ship_contract_requires_raw_module_168,
        test_ship_contract_rejects_ambiguous_raw_input,
        test_ship_contract_rejects_invalid_relocations,
        test_ship_dol_branch_relocation_retains_linked_tag,
        test_ship_contract_additive_abi_and_chunk_include_set,
        test_failed_regeneration_invalidates_success_receipt,
        test_generated_contract_compiles_with_existing_export_abi,
        test_ship_identity_uses_disc_member_not_processed_member,
        test_ship_sources_reject_differences_outside_alignment_word,
        test_ship_disc_section_table_is_checked_directly,
        test_ship_identity_hash_pin_rejects_processed_identity,
        test_ship_requires_disc_reference_without_stale_success_receipt,
        test_desktop_builders_forward_the_original_disc,
    ]
    failures = 0
    for t in tests:
        try:
            t()
        except Exception as e:
            import traceback; traceback.print_exc()
            print(f"FAIL: {t.__name__}: {e}")
            failures += 1
    print(f"\n{len(tests)-failures}/{len(tests)} PASS")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
