#!/usr/bin/env python3
"""VITA-WW-02 Cg generation tests: run the C++ generator checks.

The C++ binary builds keys from the real fixtures (through the pinned
frontend) and checks the generated Cg for the cel, passthrough and untextured
shapes, the refusal with a reason for unsupported arguments, and determinism.
A missing binary skips; a stale one fails.

Environment:
  WW02_BUILD  build directory (default: <main checkout>/build-f2/ww02)
"""

import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD = ROOT / 'build-f2/ww02'
SOURCES = [
    ROOT / "vita/feasibility/gx/gx_fixture.hpp",
    ROOT / "vita/renderer/gx_vita.h",
    ROOT / "vita/renderer/gx_cg.h",
    ROOT / "vita/renderer/gx_cg.cpp",
    ROOT / "vita/renderer/gx_dst_alpha.h",
    ROOT / "vita/renderer/gx_cg_tests.cpp",
]


def check_binary(binary):
    if not binary.is_file():
        raise unittest.SkipTest(f"{binary.name} not built; run scripts/vita/gx/build.py")
    newest = max(source.stat().st_mtime for source in SOURCES if source.is_file())
    if binary.stat().st_mtime < newest:
        raise AssertionError(f"stale binary {binary}; run scripts/vita/gx/build.py")


def find_tests():
    build = Path(os.environ.get("WW02_BUILD", DEFAULT_BUILD))
    return build / "gx-host" / "gx_cg_tests"


class CgGenerationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        check_binary(find_tests())
        result = subprocess.run([str(find_tests())], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        cls.output = result.stdout
        cls.stderr = result.stderr
        cls.returncode = result.returncode

    def test_all_checks_pass(self):
        self.assertEqual(self.returncode, 0, self.stderr)
        self.assertIn("gx_cg: all checks ok", self.output)

    def test_each_shape_checked(self):
        for marker in ("cel ok", "passthrough ok", "untextured ok",
                       "indirect generated ok", "unsupported ok", "determinism ok"):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.output)

    def test_unsupported_names_a_reason(self):
        for line in self.output.splitlines():
            if line.startswith("unsupported ok:"):
                self.assertNotEqual(line.split(":", 1)[1].strip(), "")
                break
        else:
            self.fail("no unsupported reason reported")


if __name__ == "__main__":
    unittest.main()
