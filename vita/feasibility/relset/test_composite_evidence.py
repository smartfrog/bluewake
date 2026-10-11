"""Tests for scripts/vita/relset/composite_evidence.py.

The static picture of the REL catalog comes from the real build logs of the
ww05 lane: the translator's per-module lines (module id, name, linked base)
and the composite generator's summary. The integration tests read the real
build-f5 evidence when it is present on this machine and skip elsewhere.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/relset"))

from composite_evidence import (  # noqa: E402
    CompositeEvidenceError,
    parse_composite_log,
    parse_rel_module_lines,
    parse_rel_module_table,
)

EVIDENCE = Path("/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05")


class ParseRelModuleLinesTest(unittest.TestCase):
    def test_parses_module_id_name_and_base(self):
        entries = parse_rel_module_lines(
            "REL base start: 0xC0400000\n"
            "found 415 REL modules\n"
            "  module 181: /rels/d_a_acorn_leaf.rel -> base 0xC0400000\n"
            "  module 2: /rels/d_a_agbsw0.rel -> base 0xC0410000\n")
        self.assertEqual([(entry.module_id, entry.name, entry.base)
                          for entry in entries],
                         [(181, "d_a_acorn_leaf", 0xC0400000),
                          (2, "d_a_agbsw0", 0xC0410000)])

    def test_refuses_a_module_line_that_does_not_parse(self):
        with self.assertRaises(CompositeEvidenceError):
            parse_rel_module_lines("  module x: /rels/d_a_agbsw0.rel -> base 0xC0410000\n")

    def test_an_empty_log_has_no_modules(self):
        self.assertEqual(parse_rel_module_lines("REL base start: 0xC0400000\n"), [])


class ParseCompositeLogTest(unittest.TestCase):
    def test_parses_the_chunk_and_module_counts(self):
        summary = parse_composite_log(
            "DOL: 206 chunks, 2 ranges\n"
            "RELs: 415 modules\n"
            "Composite: 748 chunks, 417 code ranges\n"
            "PASS: 748 chunks, 415 rel modules, 417 code ranges\n")
        self.assertEqual(summary.dol_chunks, 206)
        self.assertEqual(summary.rel_modules, 415)
        self.assertEqual(summary.composite_chunks, 748)
        self.assertEqual(summary.code_ranges, 417)
        self.assertEqual(summary.rel_chunks, 542)

    def test_refuses_a_log_without_the_summary(self):
        with self.assertRaises(CompositeEvidenceError):
            parse_composite_log("DOL: 206 chunks, 2 ranges\n")


class ParseRelModuleTableTest(unittest.TestCase):
    def test_collects_unique_module_ids(self):
        ids = parse_rel_module_table(
            "static const StaticRecompRelSection s_rel_sec_1[] = {\n"
            "    {1u, 0u, 0x00000000u, 0x00000000u},\n"
            "    {1u, 1u, 0xC1F800D4u, 0x0000009Cu},\n"
            "};\n"
            "static const StaticRecompRelSection s_rel_sec_2[] = {\n"
            "    {2u, 1u, 0xC04100E4u, 0x000057B0u},\n"
            "};\n")
        self.assertEqual(ids, {1, 2})


class RealEvidenceTest(unittest.TestCase):
    def setUp(self):
        if not EVIDENCE.is_dir():
            self.skipTest("the build-f5 ww05 evidence is not on this machine")

    def test_the_translate_log_lists_415_unique_modules(self):
        entries = parse_rel_module_lines(
            (EVIDENCE / "logs/translate-rels.log").read_text(errors="replace"))
        self.assertEqual(len(entries), 415)
        self.assertEqual(len({entry.module_id for entry in entries}), 415)
        self.assertTrue(all(entry.name for entry in entries))

    def test_the_composite_log_agrees_with_the_module_log(self):
        summary = parse_composite_log(
            (EVIDENCE / "logs/generate-composite.log").read_text(errors="replace"))
        self.assertEqual(summary.rel_modules, 415)
        self.assertEqual(summary.dol_chunks, 206)
        self.assertEqual(summary.rel_chunks, 542)
        self.assertEqual(summary.code_ranges, 417)

    def test_module_ids_match_the_composite_tables(self):
        entries = parse_rel_module_lines(
            (EVIDENCE / "logs/translate-rels.log").read_text(errors="replace"))
        table_ids = parse_rel_module_table(
            (EVIDENCE / "composite/rel_modules.inc").read_text(errors="replace"))
        self.assertEqual({entry.module_id for entry in entries}, table_ids)


if __name__ == "__main__":
    unittest.main()
