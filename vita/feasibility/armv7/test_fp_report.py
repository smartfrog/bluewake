from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))


class ReportTest(unittest.TestCase):
    def test_incomplete_or_dishonest_report_is_rejected(self):
        from fp_report import validate
        case = {"id": "x", "mode": 0, "expected": "0000000000000001"}
        header = "RUN fresh source package\n"
        passed = "CASE x mode=0 expected=0000000000000001 actual=0000000000000001 path=1 PASS\n"
        terminal = "END PASS cases=1 failures=0\n"
        for text in ("", header, header + passed, header + terminal,
                     header + passed + terminal.rstrip(), header + passed * 2 + terminal,
                     header + passed.replace("CASE x", "CASE other") + terminal,
                     header + passed.replace("actual=0000000000000001", "actual=0000000000000000") + terminal,
                     header + passed.replace("path=1", "path=0") + terminal,
                     header + passed.replace("expected=0000000000000001", "expected=0000000000000000") + terminal):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    validate(text, "fresh", "source", "package", [case])

    def test_complete_passing_report_is_accepted(self):
        from fp_report import validate
        case = {"id": "x", "mode": 0, "expected": "0000000000000001"}
        text = ("RUN fresh source package\n"
                "CASE x mode=0 expected=0000000000000001 actual=0000000000000001 path=1 PASS\n"
                "END PASS cases=1 failures=0\n")
        self.assertEqual(validate(text, "fresh", "source", "package", [case]), 0)

    def test_semantic_failure_remains_nonzero(self):
        from fp_report import validate
        case = {"id": "round-rn2-ni0", "mode": 2, "expected": "3ff0000000000001"}
        text = ("RUN fresh source package\n"
                "CASE round-rn2-ni0 mode=2 expected=3ff0000000000001 "
                "actual=3ff0000000000000 path=1 FAIL\n"
                "END FAIL cases=1 failures=1\n")
        self.assertEqual(validate(text, "fresh", "source", "package", [case]), 1)

    def test_stale_identity_is_rejected(self):
        from fp_report import validate
        with self.assertRaisesRegex(ValueError, "identity"):
            validate("RUN old source package\nEND PASS cases=0 failures=0\n",
                     "fresh", "source", "package", [])


if __name__ == "__main__":
    unittest.main()
