# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic same-emission checks; never run this against private game inputs."""

import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(sys.argv.pop(1)).resolve()
if SOURCE != Path("/tmp/opencode/ww42-producer-copy"):
    raise SystemExit("Use only the authorized disposable /tmp/opencode/ww42-producer-copy")
PROBE = Path(__file__).with_name("ship_code_input_probe.c")
BUILD = SOURCE / "ww42-probe"


def fixture(module=168, executable=True, import_module=0):
    data = bytearray(0x120)

    def word(offset, value):
        struct.pack_into(">I", data, offset, value)

    for offset, value in [(0, module), (0x0C, 3), (0x10, 0x50), (0x1C, 3),
                          (0x24, 0x80), (0x28, 0xA0), (0x2C, 8),
                          (0x34, 4), (0x40, 4), (0x44, 4),
                          (0x58, 0x100 | int(executable)), (0x5C, 8),
                          (0x60, 0x108), (0x64, 4),
                          (0xA0, import_module), (0xA4, 0x80),
                          (0x100, 0x48000001), (0x104, 0x4E800020)]:
        word(offset, value)
    data[0x30] = 1
    for offset, values in [(0x80, (0, 202, 1, 0)),
                           (0x88, (0, 10, 0, 0x80400104)),
                           (0x90, (0, 203, 0, 0))]:
        struct.pack_into(">HBBI", data, offset, *values)
    return bytes(data)


class ShipCodeInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sources = ["app/pipeline.c", "app/paths.c", "frontend/decoder.c",
                   "frontend/container/rel.c", "platform/fs.c",
                   "platform/pathlist.c", "platform/strutil.c",
                   "analysis/embedded_data.c", "analysis/smc.c",
                   "analysis/symbol_map.c", "backend/c_cfg.c",
                   "backend/option_sites.c", "backend/emitter.c",
                   "backend/dispatch.c", "backend/codegen.c",
                   "backend/symbols.c", "backend/module_abi.c"]
        command = ["cc", "-std=c11", "-O0", "-g", "-Wall", "-Wextra",
                   "-Werror", "-Wno-unused-parameter", "-ffunction-sections",
                   "-fdata-sections", "-I", str(SOURCE / "src"), str(PROBE)]
        command += [str(SOURCE / "src" / item) for item in sources]
        command += ["-Wl,--gc-sections", "-Wl,--wrap=fopen",
                    "-Wl,--wrap=fwrite", "-Wl,--wrap=fclose",
                    "-Wl,--wrap=ppc_decode", "-Wl,--wrap=rel_apply_relocations",
                    "-pthread", "-lm", "-o", str(BUILD)]
        subprocess.run(command, check=True)
        print("probe_sha256=" + hashlib.sha256(BUILD.read_bytes()).hexdigest())
        identity = hashlib.sha256()
        for path in [Path(__file__), PROBE] + [SOURCE / "src" / item for item in sources]:
            identity.update(path.name.encode() + b"\0" + path.read_bytes() + b"\0")
        print("test_source_identity_sha256=" + identity.hexdigest())

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ww42-fixture-", dir=SOURCE)
        self.root = Path(self.temporary.name)
        self.inputs = self.root / "inputs"
        self.inputs.mkdir()
        self.output = self.root / "output"
        self.sidecar = self.root / "capture.ship-code-input"
        (self.inputs / "a_ship.rel").write_bytes(fixture())
        (self.inputs / "z_later.rel").write_bytes(fixture(module=169))

    def tearDown(self):
        self.temporary.cleanup()

    def invoke(self, requested=True, fault=None, single=False):
        environment = os.environ.copy()
        environment.pop("DOLRECOMP_SHIP_CODE_INPUT", None)
        environment.pop("SHIP_TEST_FAULT", None)
        if requested:
            environment["DOLRECOMP_SHIP_CODE_INPUT"] = str(self.sidecar)
        if fault:
            environment["SHIP_TEST_FAULT"] = fault
        return subprocess.run([str(BUILD), str(self.inputs / "a_ship.rel" if single else self.inputs),
                               str(self.output), "single" if single else "batch"],
                              env=environment, capture_output=True, text=True)

    def assert_rejected(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(self.sidecar.exists())

    def test_capture_matches_actual_emitter_decode_after_relocation(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        capture = self.sidecar.read_bytes()
        self.assertEqual(capture[:8], b"SHIP168\0")
        self.assertEqual(struct.unpack_from(">I", capture, 8), (1,))
        index, offset, address, size = struct.unpack_from(">4I", capture, 12)
        self.assertEqual((index, offset, address, size), (1, 0x100, 0xC0400100, 8))
        words = struct.unpack(">2I", capture[28:])
        observed = [(int(a, 16), int(w, 16)) for a, w in
                    re.findall(r"DECODE ([0-9A-F]+) ([0-9A-F]+)", result.stdout)]
        self.assertEqual(observed[:2], [(address + i * 4, w) for i, w in enumerate(words)])
        self.assertEqual(words, (0x48000005, 0x4E800020))
        self.assertNotEqual(capture[28:], fixture()[offset:offset + size])
        chunks = list((self.output / "generated/rels/a_ship_168/chunks").glob("*.c"))
        self.assertEqual(len(chunks), 1)
        self.assertIn("C0400104", chunks[0].read_text())

    def test_default_emission_has_no_sidecar(self):
        result = self.invoke(requested=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.sidecar.exists())

    def test_opt_in_does_not_change_emitted_sources(self):
        self.assertEqual(self.invoke(requested=False).returncode, 0)
        before = {p.relative_to(self.output): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        self.assertEqual(self.invoke().returncode, 0)
        after = {p.relative_to(self.output): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_missing_ship_rejected_before_emission(self):
        (self.inputs / "a_ship.rel").unlink()
        self.assert_rejected(self.invoke())
        self.assertFalse(self.output.exists())

    def test_wrong_module_rejected_before_emission(self):
        (self.inputs / "a_ship.rel").write_bytes(fixture(module=167))
        self.assert_rejected(self.invoke())
        self.assertFalse(self.output.exists())

    def test_missing_executable_section_rejected(self):
        self.assert_rejected(self.invoke(fault="no-code"))

    def test_missing_executable_data_rejected(self):
        self.assert_rejected(self.invoke(fault="missing-data"))

    def test_invalid_executable_index_rejected(self):
        self.assert_rejected(self.invoke(fault="invalid-index"))

    def test_all_executable_sections_match_actual_emitter(self):
        data = bytearray(fixture())
        struct.pack_into(">2I", data, 0x60, 0x111, 8)
        struct.pack_into(">2I", data, 0x110, 0x38600007, 0x4E800020)
        (self.inputs / "a_ship.rel").write_bytes(data)
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        capture = self.sidecar.read_bytes()
        self.assertEqual(struct.unpack_from(">I", capture, 8), (2,))
        observed = [(int(a, 16), int(w, 16)) for a, w in
                    re.findall(r"DECODE ([0-9A-F]+) ([0-9A-F]+)", result.stdout)]
        cursor = 12
        expected = []
        identities = []
        for _ in range(2):
            index, offset, address, size = struct.unpack_from(">4I", capture, cursor)
            identities.append((index, offset, address, size))
            words = struct.unpack_from(f">{size // 4}I", capture, cursor + 16)
            expected.extend((address + i * 4, word) for i, word in enumerate(words))
            cursor += 16 + size
        self.assertEqual(identities, [(1, 0x100, 0xC0400100, 8), (2, 0x110, 0xC0400110, 8)])
        self.assertEqual(observed[:4], expected)
        self.assertEqual(len(capture), cursor)

    def test_single_module_export_rejected(self):
        self.assert_rejected(self.invoke(single=True))

    def test_unignored_sidecar_suffix_rejected_before_emission(self):
        self.sidecar = self.root / "unsafe-name.bin"
        self.assert_rejected(self.invoke())
        self.assertFalse(self.output.exists())

    def test_non_c_backend_rejected_before_emission(self):
        self.assert_rejected(self.invoke(fault="llvm"))
        self.assertFalse(self.output.exists())

    def test_duplicate_ship_rejected_before_emission(self):
        (self.inputs / "z_later.rel").write_bytes(fixture())
        self.assert_rejected(self.invoke())
        self.assertFalse(self.output.exists())

    def test_relocation_failure_has_no_sidecar(self):
        (self.inputs / "z_later.rel").write_bytes(fixture(module=169, import_module=987))
        self.assert_rejected(self.invoke())

    def test_later_module_emission_failure_has_no_sidecar(self):
        self.assert_rejected(self.invoke(fault="emission"))

    def test_later_module_header_close_failure_has_no_sidecar(self):
        self.assert_rejected(self.invoke(fault="header"))

    def test_later_module_manifest_close_failure_has_no_sidecar(self):
        self.assert_rejected(self.invoke(fault="manifest"))

    def test_ship_chunk_failure_has_no_sidecar(self):
        self.assert_rejected(self.invoke(fault="ship-chunk"))

    def test_chunk_write_error_with_successful_close_has_no_sidecar(self):
        self.assert_rejected(self.invoke(fault="chunk-write-error"))

    def test_header_write_error_with_successful_close_has_no_sidecar(self):
        self.assert_rejected(self.invoke(fault="header-write-error"))

    def test_manifest_write_error_with_successful_close_has_no_sidecar(self):
        self.assert_rejected(self.invoke(fault="manifest-write-error"))

    def test_sidecar_open_failure_has_no_sidecar(self):
        self.assert_rejected(self.invoke(fault="open"))

    def test_sidecar_write_failure_has_no_sidecar(self):
        self.assert_rejected(self.invoke(fault="write"))

    def test_sidecar_close_failure_has_no_sidecar(self):
        self.assert_rejected(self.invoke(fault="close"))

    def test_existing_sidecar_is_not_overwritten(self):
        self.sidecar.write_bytes(b"synthetic stale marker")
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.sidecar.read_bytes(), b"synthetic stale marker")


if __name__ == "__main__":
    unittest.main(verbosity=2)
