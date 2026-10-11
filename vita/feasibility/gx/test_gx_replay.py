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


class Ci8TriangleTest(unittest.TestCase):
    """A CI8 image through a 16-entry RGB5A3 TLUT: palette identity comes back."""

    @classmethod
    def setUpClass(cls):
        if not find_replay().is_file():
            raise unittest.SkipTest("gx_replay not built; run scripts/vita/gx/build.py")
        cls.lines = parse_report(run_scenario("ci8_triangle"))

    def test_plan_is_built(self):
        self.assertEqual(self.lines["plan"][0][:2], ["ok=1", "vertices=3"])

    def test_image_is_the_ci8_fixture(self):
        texture = dict(field.split("=", 1) for field in self.lines["texture"][0])
        self.assertEqual(texture["format"], "9")  # GX_TF_C8
        self.assertEqual(texture["width"], "8")
        self.assertEqual(texture["height"], "8")

    def test_palette_is_the_fixture_tlut(self):
        tlut = dict(field.split("=", 1) for field in self.lines["tlut"][0])
        self.assertEqual(tlut["format"], "2")  # GX_TL_RGB5A3
        self.assertEqual(tlut["entries"], "16")

    def test_pipeline_is_textured(self):
        pipeline = dict(field.split("=", 1) for field in self.lines["pipeline"][0])
        self.assertEqual(pipeline["textured"], "1")


class StateTriangleTest(unittest.TestCase):
    """BP depth/alpha/blend state comes back in the pipeline key."""

    @classmethod
    def setUpClass(cls):
        if not find_replay().is_file():
            raise unittest.SkipTest("gx_replay not built; run scripts/vita/gx/build.py")
        cls.lines = parse_report(run_scenario("state_triangle"))
        cls.pipeline = dict(field.split("=", 1) for field in cls.lines["pipeline"][0])

    def test_cull_mode_is_back(self):
        self.assertEqual(self.pipeline["cull_mode"], "1")

    def test_depth_state(self):
        self.assertEqual(self.pipeline["depth_test"], "1")
        self.assertEqual(self.pipeline["depth_func"], "3")  # LEqual
        self.assertEqual(self.pipeline["depth_update"], "1")

    def test_blend_state(self):
        self.assertEqual(self.pipeline["blend_enable"], "1")
        self.assertEqual(self.pipeline["src_factor"], "4")  # SrcAlpha
        self.assertEqual(self.pipeline["dst_factor"], "5")  # InvSrcAlpha
        self.assertEqual(self.pipeline["color_update"], "1")
        self.assertEqual(self.pipeline["alpha_update"], "1")

    def test_plan_still_carries_the_geometry(self):
        self.assertEqual(self.lines["plan"][0][:2], ["ok=1", "vertices=3"])


class CelTexgenTest(unittest.TestCase):
    """Two texgens, the second a Color0 toon source, with channel 0 lit."""

    @classmethod
    def setUpClass(cls):
        if not find_replay().is_file():
            raise unittest.SkipTest("gx_replay not built; run scripts/vita/gx/build.py")
        cls.lines = parse_report(run_scenario("cel_triangle"))
        cls.pipeline = dict(field.split("=", 1) for field in cls.lines["pipeline"][0])
        cls.texgens = [dict(field.split("=", 1) for field in entry[1:])
                       for entry in cls.lines["texgen"]]

    def test_two_texgens(self):
        self.assertEqual(self.pipeline["texgens"], "2")
        self.assertEqual(len(self.texgens), 2)

    def test_second_texgen_is_color0(self):
        self.assertEqual(self.texgens[0]["type"], "0")  # Regular
        self.assertEqual(self.texgens[1]["type"], "2")  # Color0
        self.assertEqual(self.texgens[1]["enabled"], "1")

    def test_channel_zero_is_lit(self):
        self.assertEqual(self.pipeline["litchan0"], "1")

    def test_tev_two_stages_sample_ramp_then_diffuse(self):
        stages = [dict(field.split("=", 1) for field in entry[1:])
                  for entry in self.lines["tevstage"]]
        self.assertEqual(len(stages), 2)
        self.assertEqual(stages[0]["texmap"], "1")
        self.assertEqual(stages[0]["texcoord"], "1")
        self.assertEqual(stages[0]["enable"], "1")
        self.assertEqual(stages[1]["texmap"], "0")
        self.assertEqual(stages[1]["texcoord"], "0")
        self.assertEqual(stages[1]["enable"], "1")

    def test_both_texmaps_carry_a_bound_image(self):
        self.assertEqual(self.pipeline["texmap_mask"], "3")  # texmap 0 and 1
        textures = {}
        for entry in self.lines["texture"]:
            texmap = int(entry[0].split("=")[1])
            textures[texmap] = dict(field.split("=", 1) for field in entry[1:])
        self.assertEqual(sorted(textures), [0, 1])
        self.assertEqual(textures[0]["valid"], "1")
        self.assertEqual(textures[1]["valid"], "1")
        self.assertEqual(textures[0]["width"], "4")
        self.assertEqual(textures[1]["width"], "4")

    def test_plan_is_textured(self):
        self.assertEqual(self.pipeline["textured"], "1")


if __name__ == "__main__":
    unittest.main()
