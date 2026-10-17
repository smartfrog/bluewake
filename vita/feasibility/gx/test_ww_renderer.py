#!/usr/bin/env python3
"""VITA-WW-17 renderer tests: run the C++ interface checks.

The C++ binary drives the host-facing renderer interface (ww_renderer.h) with
synthetic GX command batches and a recording backend in place of vitaGL, and
checks draws, programs, copies and presentations as events. This wrapper runs
it and checks the summary lines.

Environment:
  WW17_BUILD  build directory (default /home/fred/projects/zeldaWWHDRecomp/build-f17/ww17)
"""

import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD = Path("/home/fred/projects/zeldaWWHDRecomp/build-f17/ww17")
SOURCES = [
    ROOT / "vita/renderer/ww_renderer.h",
    ROOT / "vita/renderer/ww_renderer_backend.h",
    ROOT / "vita/renderer/ww_renderer.cpp",
    ROOT / "vita/renderer/ww_renderer_tests.cpp",
    ROOT / "vita/renderer/gx_vita.h",
    ROOT / "vita/renderer/gx_vita.cpp",
    ROOT / "vita/renderer/gx_dst_alpha.h",
]


def check_binary(binary):
    """A missing binary skips; a stale one fails (never a silent old pass)."""
    if not binary.is_file():
        raise unittest.SkipTest(f"{binary.name} not built; run scripts/vita/gx/build.py")
    newest = max(source.stat().st_mtime for source in SOURCES if source.is_file())
    if binary.stat().st_mtime < newest:
        raise AssertionError(f"stale binary {binary}; run scripts/vita/gx/build.py")


def find_tests():
    build = Path(os.environ.get("WW17_BUILD", DEFAULT_BUILD))
    return build / "gx-host" / "ww_renderer_tests"


class RendererTest(unittest.TestCase):
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
        self.assertIn("ww_renderer: all checks ok", self.output)

    def test_every_scenario_reported(self):
        for marker in ("renderer split batch draw ok",
                       "renderer display copy presents once ok",
                       "renderer holds next frame until present ok",
                       "renderer pipeline state ok",
                       "renderer multi-texmap draw ok",
                       "renderer program refusal kept ok",
                       "renderer efb copy ok",
                       "renderer stream rejection ok",
                       "renderer backend draw failure ok",
                       "renderer two frames ok"):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.output)


if __name__ == "__main__":
    unittest.main()
