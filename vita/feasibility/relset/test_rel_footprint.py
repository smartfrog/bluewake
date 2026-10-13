"""Tests for scripts/vita/relset/rel_footprint.py.

The per-module compiled footprint comes from the real ww05 link map: one
`chunk_NNNN_rel1_<BASE>.c.obj` per translated REL chunk, with its `.text` and
`.rodata` sizes. The translator log names the modules and their guest code
slots; a module's chunks can spill into the slot after its own, so the owner is
the module with the greatest base not after the chunk. The integration tests
read the real build-f5 evidence when it is present on this machine and skip
elsewhere.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/relset"))

from rel_footprint import (  # noqa: E402
    RelFootprintError,
    build_catalog,
    parse_chunk_sections,
)

EVIDENCE = Path("/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05")

TRANSLATE = (
    "  module 181: /rels/d_a_acorn_leaf.rel -> base 0xC0400000\n"
    "  module 2: /rels/d_a_agbsw0.rel -> base 0xC0410000\n")

MAP = (
    " .text          0x810025d0    0x27434 composite/gGZLE01_recomp.a"
    "(chunk_0000_rel1_C04000F4.c.obj)\n"
    " .rodata        0x95809250      0xedc composite/gGZLE01_recomp.a"
    "(chunk_0000_rel1_C04000F4.c.obj)\n"
    " .data          0x95fa0bd8        0x0 composite/gGZLE01_recomp.a"
    "(chunk_0000_rel1_C04000F4.c.obj)\n"
    " .text          0x81029a04    0x8835c composite/gGZLE01_recomp.a"
    "(chunk_0000_rel1_C04100E4.c.obj)\n"
    " .rodata        0x9580a12c      0x200 composite/gGZLE01_recomp.a"
    "(chunk_0001_rel1_C04140E4.c.obj)\n")


class ParseChunkSectionsTest(unittest.TestCase):
    def test_parses_each_chunk_section_line(self):
        sections = parse_chunk_sections(MAP)
        self.assertEqual(
            [(section.chunk_index, section.base, section.section,
              section.bytes) for section in sections],
            [(0, 0xC04000F4, "text", 0x27434),
             (0, 0xC04000F4, "rodata", 0xEDC),
             (0, 0xC04000F4, "data", 0),
             (0, 0xC04100E4, "text", 0x8835C),
             (1, 0xC04140E4, "rodata", 0x200)])

    def test_ignores_lines_that_are_not_chunk_sections(self):
        self.assertEqual(parse_chunk_sections(" .text 0x1 0x2 main.c.obj\n"), [])

    def test_refuses_a_chunk_line_that_does_not_parse(self):
        with self.assertRaises(RelFootprintError):
            parse_chunk_sections(
                " .text          0x810025d0    0xZZZZZ composite/gGZLE01_recomp.a"
                "(chunk_0000_rel1_C04000F4.c.obj)\n")


class BuildCatalogTest(unittest.TestCase):
    def test_groups_chunks_into_their_module_by_greatest_base(self):
        catalog = build_catalog(TRANSLATE, MAP,
                                {"d_a_acorn_leaf": 9164, "d_a_agbsw0": 25380})
        self.assertEqual(
            [(module.module_id, module.name, module.text_bytes,
              module.rodata_bytes, module.raw_bytes, module.chunks)
             for module in catalog.modules],
            [(181, "d_a_acorn_leaf", 0x27434, 0xEDC, 9164, 1),
             (2, "d_a_agbsw0", 0x8835C, 0x200, 25380, 2)])
        self.assertEqual(catalog.compiled_bytes, 0x27434 + 0xEDC + 0x8835C + 0x200)
        self.assertEqual(catalog.raw_bytes, 9164 + 25380)

    def test_a_chunk_after_the_last_module_base_belongs_to_that_module(self):
        catalog = build_catalog(
            "  module 1: /rels/a.rel -> base 0xC0400000\n"
            "  module 2: /rels/b.rel -> base 0xC0410000\n",
            " .text 0x1 0x100 a(chunk_0004_rel1_C04100F4.c.obj)\n",
            {"a": 10, "b": 20})
        by_id = {module.module_id: module for module in catalog.modules}
        self.assertEqual(by_id[2].text_bytes, 0x100)
        self.assertEqual(by_id[1].text_bytes, 0)

    def test_refuses_a_chunk_before_the_first_module_base(self):
        with self.assertRaises(RelFootprintError):
            build_catalog("  module 1: /rels/a.rel -> base 0xC0400000\n",
                          " .text 0x1 0x100 a(chunk_0000_rel1_C03000F4.c.obj)\n",
                          {"a": 10})

    def test_refuses_a_module_without_a_raw_size(self):
        with self.assertRaises(RelFootprintError):
            build_catalog(TRANSLATE, MAP, {"d_a_acorn_leaf": 9164})

    def test_refuses_a_duplicate_module_base(self):
        with self.assertRaises(RelFootprintError):
            build_catalog(
                "  module 1: /rels/a.rel -> base 0xC0400000\n"
                "  module 2: /rels/b.rel -> base 0xC0400000\n",
                " .text 0x1 0x100 a(chunk_0000_rel1_C04000F4.c.obj)\n",
                {"a": 10, "b": 20})

    def test_largest_modules_are_ranked_by_compiled_bytes(self):
        catalog = build_catalog(TRANSLATE, MAP,
                                {"d_a_acorn_leaf": 9164, "d_a_agbsw0": 25380})
        largest = catalog.largest(1)
        self.assertEqual([module.name for module in largest], ["d_a_agbsw0"])


class RealEvidenceTest(unittest.TestCase):
    def setUp(self):
        if not EVIDENCE.is_dir():
            self.skipTest("the build-f5 ww05 evidence is not on this machine")

    def test_the_real_map_groups_all_415_modules(self):
        translate = (EVIDENCE / "logs/translate-rels.log").read_text(errors="replace")
        map_text = (EVIDENCE / "artifacts-c5a63623be104ea0ad64e7446d88a18c/ww05.map"
                    ).read_text(errors="replace")
        raw_dir = EVIDENCE / "rel-importer-check-h211_gub/private/rels"
        raw_sizes = {path.stem: path.stat().st_size for path in raw_dir.glob("*.rel")}
        catalog = build_catalog(translate, map_text, raw_sizes)
        self.assertEqual(len(catalog.modules), 415)
        self.assertEqual(len({module.module_id for module in catalog.modules}), 415)
        self.assertEqual(catalog.compiled_bytes, 197_397_962)
        self.assertEqual(catalog.raw_bytes, 6_996_836)

    def test_the_profile_list_module_is_the_smallest_kind_of_entry(self):
        translate = (EVIDENCE / "logs/translate-rels.log").read_text(errors="replace")
        map_text = (EVIDENCE / "artifacts-c5a63623be104ea0ad64e7446d88a18c/ww05.map"
                    ).read_text(errors="replace")
        raw_dir = EVIDENCE / "rel-importer-check-h211_gub/private/rels"
        raw_sizes = {path.stem: path.stat().st_size for path in raw_dir.glob("*.rel")}
        catalog = build_catalog(translate, map_text, raw_sizes)
        module1 = next(module for module in catalog.modules if module.module_id == 1)
        self.assertEqual(module1.name, "f_pc_profile_lst")
        self.assertEqual(module1.raw_bytes, 16_500)
        self.assertEqual(module1.compiled_bytes, 7_110)


if __name__ == "__main__":
    unittest.main()
