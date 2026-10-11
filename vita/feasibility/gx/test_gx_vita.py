#!/usr/bin/env python3
"""VITA-WW-02 translation tests: run the C++ GX -> vitaGL translation checks.

The C++ binary replays each fixture through the pinned frontend and asserts
the translation against the fixture's own values (geometry, GL pipeline
tokens, texture bytes cross-checked with the pinned codecs). This wrapper runs
it and checks the summary lines.

Environment:
  WW02_BUILD  build directory (default: <main checkout>/build-f2/ww02)
"""

import os
import subprocess
import unittest
from pathlib import Path

DEFAULT_BUILD = Path("/home/fred/projects/zeldaWWHDRecomp/build-f2/ww02")
ROOT = Path(__file__).resolve().parents[3]
SOURCES = [
    ROOT / "vita/feasibility/gx/gx_fixture.hpp",
    ROOT / "vita/renderer/gx_vita.h",
    ROOT / "vita/renderer/gx_vita.cpp",
    ROOT / "vita/renderer/gx_vita_tests.cpp",
]

def check_binary(binary):
    """A missing binary skips; a stale one fails (never a silent old pass)."""
    if not binary.is_file():
        raise unittest.SkipTest(f"{binary.name} not built; run scripts/vita/gx/build.py")
    newest = max(source.stat().st_mtime for source in SOURCES if source.is_file())
    if binary.stat().st_mtime < newest:
        raise AssertionError(f"stale binary {binary}; run scripts/vita/gx/build.py")


def find_tests():
    build = Path(os.environ.get("WW02_BUILD", DEFAULT_BUILD))
    return build / "gx-host" / "gx_vita_tests"


class TranslationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        check_binary(find_tests())
        result = subprocess.run([str(find_tests())], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        cls.output = result.stdout
        cls.stderr = result.stderr
        cls.returncode = result.returncode

    def test_all_translations_pass(self):
        self.assertEqual(self.returncode, 0, self.stderr)
        self.assertIn("gx_vita: all translations ok", self.output)

    def test_each_fixture_translated(self):
        for scenario in ("textured_triangle", "ci8_triangle", "state_triangle",
                         "cel_triangle"):
            with self.subTest(scenario=scenario):
                self.assertIn(f"{scenario} ok", self.output)

    def test_cel_program_differs_from_textured(self):
        keys = {}
        for line in self.output.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[0].endswith("_triangle") and parts[1] == "ok":
                for field in parts[2:]:
                    if field.startswith("key="):
                        keys[parts[0]] = field
        self.assertNotEqual(keys["cel_triangle"], keys["textured_triangle"])


if __name__ == "__main__":
    unittest.main()
