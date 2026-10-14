#!/usr/bin/env python3
"""Configure-time contract for the module_export.c optimization override.

Configures the real cmake/composite/CMakeLists.txt against a synthetic,
game-free composite and reads the effective flags out of compile_commands.json.
No game data is needed and nothing is compiled.
"""
import json
import re
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
COMPOSITE_CMAKE = REPO / "cmake" / "composite"
OPTIMIZATION_FLAG = re.compile(r"-O[0-3]")
ERROR = "COMPOSITE_EXPORT_OPTIMIZATION_LEVEL must be empty, 0, 1, 2, or 3"


def write_file(path, text="/* synthetic fixture */\n"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def make_fixture(root):
    composite = root / "composite"
    for name in ("generated.h", "generated_composite.h", "module_tables.inc",
                 "rel_modules.inc", "rel_data.inc"):
        write_file(composite / name)
    write_file(composite / "chunks_dol" / "chunk_80003100.c",
               "int bw_fixture_chunk(void) { return 0; }\n")
    for name in ("chunk_0140_text1_802316E0.c", "chunk_0202_text1_803296E0.c"):
        write_file(composite / "chunks_dol" / name)
    write_file(composite / "chunks_d_a_npc_so_148" / "chunk_0000_rel1_C10800F4.c")
    runtime = root / "runtime"
    write_file(runtime / "include" / "core" / "cpu.h")
    for name in ("cpu.c", "cpu_exception.c", "cpu_interpreter.c",
                 "cpu_interpreter_table.c", "cpu_interpreter_float.c",
                 "cpu_interpreter_integer.c"):
        write_file(runtime / "src" / "core" / name)
    abi = root / "abi"
    write_file(abi / "StaticRecompABI.h")
    return composite, runtime, abi


class CompositeExportOptimizationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ww106 export optimization ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.composite, self.runtime, self.abi = make_fixture(self.root)

    def configure(self, name, definitions=()):
        build = self.root / name
        result = subprocess.run([
            "cmake", "-S", str(COMPOSITE_CMAKE), "-B", str(build),
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            f"-DCOMPOSITE_DIR={self.composite}",
            f"-DGXRUNTIME_DIR={self.runtime}",
            f"-DABI_DIR={self.abi}", *definitions], capture_output=True, text=True)
        return build, result

    def effective_flags(self, name, definitions=()):
        build, result = self.configure(name, definitions)
        self.assertEqual(result.returncode, 0, result.stderr)
        flags = {}
        for entry in json.loads((build / "compile_commands.json").read_text()):
            source = Path(entry["file"])
            if not source.is_absolute():
                source = Path(entry["directory"]) / source
            matches = [token for token in shlex.split(entry["command"])
                       if OPTIMIZATION_FLAG.fullmatch(token)]
            flags[source.resolve()] = matches[-1] if matches else None
        return flags

    def assert_effective(self, flags, source, expected):
        self.assertEqual(flags[Path(source).resolve()], expected, str(source))

    def test_target_level_one_inherits_o1_for_every_source(self):
        flags = self.effective_flags("build", ["-DCOMPOSITE_OPTIMIZATION_LEVEL=1"])
        for source in (COMPOSITE_CMAKE / "module_export.c",
                       COMPOSITE_CMAKE / "dispatch_loop.c",
                       self.composite / "chunks_dol" / "chunk_80003100.c",
                       self.runtime / "src" / "core" / "cpu.c"):
            self.assert_effective(flags, source, "-O1")

    def test_explicit_export_level_two_affects_only_module_export(self):
        flags = self.effective_flags("build", [
            "-DCOMPOSITE_OPTIMIZATION_LEVEL=1",
            "-DCOMPOSITE_EXPORT_OPTIMIZATION_LEVEL=2"])
        self.assert_effective(flags, COMPOSITE_CMAKE / "module_export.c", "-O2")
        for source in (COMPOSITE_CMAKE / "dispatch_loop.c",
                       self.composite / "chunks_dol" / "chunk_80003100.c",
                       self.runtime / "src" / "core" / "cpu.c"):
            self.assert_effective(flags, source, "-O1")

    def test_desktop_default_target_level_two_stays_o2(self):
        flags = self.effective_flags("build")
        self.assert_effective(flags, COMPOSITE_CMAKE / "module_export.c", "-O2")
        self.assert_effective(flags, COMPOSITE_CMAKE / "dispatch_loop.c", "-O2")

    def test_explicit_levels_zero_to_three_are_applied(self):
        for level in ("0", "1", "2", "3"):
            with self.subTest(level=level):
                flags = self.effective_flags(f"build-{level}", [
                    "-DCOMPOSITE_OPTIMIZATION_LEVEL=1",
                    f"-DCOMPOSITE_EXPORT_OPTIMIZATION_LEVEL={level}"])
                self.assert_effective(flags, COMPOSITE_CMAKE / "module_export.c",
                                      f"-O{level}")

    def test_invalid_export_level_fails_with_clear_error(self):
        for level in ("4", "x", "-1", "10"):
            with self.subTest(level=level):
                _, result = self.configure(f"build-{level}", [
                    f"-DCOMPOSITE_EXPORT_OPTIMIZATION_LEVEL={level}"])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(ERROR, result.stderr)


if __name__ == "__main__":
    unittest.main()
