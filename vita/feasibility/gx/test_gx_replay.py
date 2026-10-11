#!/usr/bin/env python3
"""VITA-WW-02 GX fixture tests: synthetic GX streams through the pinned frontend.

The fixtures build documented GX command streams; the harness replays them
through the pinned RetailGxFrontend and GxCoreSink and prints one line per
decoded fact. Every expected value here is one the fixture wrote into the
stream, so the pinned decode is checked against the fixture's own inputs.

Environment:
  WW02_BUILD  build directory (default: <main checkout>/build-f2/ww02)
"""

import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MAIN_CHECKOUT = Path("/home/fred/projects/zeldaWWHDRecomp")
DEFAULT_BUILD = MAIN_CHECKOUT / "build-f2" / "ww02"


def find_replay():
    build = Path(os.environ.get("WW02_BUILD", DEFAULT_BUILD))
    return build / "gx-host" / "gx_replay"


def run_scenario(name):
    result = subprocess.run([str(find_replay()), name],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise AssertionError(f"gx_replay {name} failed ({result.returncode}): {result.stderr}")
    return result.stdout


def parse_report(output):
    """One dict per 'kind key=value ...' line group; repeated kinds append."""
    lines = {}
    for line in output.splitlines():
        parts = line.split()
        if not parts:
            continue
        lines.setdefault(parts[0], []).append(parts[1:])
    return lines


class TexturedTriangleTest(unittest.TestCase):
    """Direct position + UV, one triangle: the fixture's own bytes come back."""

    @classmethod
    def setUpClass(cls):
        if not find_replay().is_file():
            raise unittest.SkipTest("gx_replay not built; run scripts/vita/gx/build.py")
        cls.lines = parse_report(run_scenario("textured_triangle"))

    def test_plan_is_built(self):
        self.assertEqual(self.lines["plan"][0][:2], ["ok=1", "vertices=3"])

    def test_positions_and_uvs_match_the_fixture(self):
        vertices = [dict(field.split("=", 1) for field in entry[1:])
                    for entry in self.lines["v"]]
        self.assertEqual(len(vertices), 3)
        expected = [
            {"pos": "0,0,0", "uv0": "0,0"},
            {"pos": "1,0,0", "uv0": "1,0"},
            {"pos": "0,1,0", "uv0": "0,1"},
        ]
        for index, want in enumerate(expected):
            with self.subTest(vertex=index):
                for field, value in want.items():
                    self.assertEqual(vertices[index][field], value)

    def test_triangle_indices_are_the_triangle(self):
        self.assertEqual(self.lines["idx"][0], ["0", "1", "2"])

    def test_pipeline_is_the_textured_direct_path(self):
        pipeline = dict(field.split("=", 1) for field in self.lines["pipeline"][0])
        self.assertEqual(pipeline["textured"], "1")
        self.assertEqual(pipeline["has_pos_mtx_idx"], "0")

    def test_bound_texture_is_the_fixture_texture(self):
        texture = dict(field.split("=", 1) for field in self.lines["texture"][0])
        self.assertEqual(texture["format"], "6")  # GX_TF_RGBA8
        self.assertEqual(texture["width"], "4")
        self.assertEqual(texture["height"], "4")


if __name__ == "__main__":
    unittest.main()
