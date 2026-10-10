from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))
import build


class StackEvidenceTest(unittest.TestCase):
    def test_repeated_function_retains_largest_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            for sizes in ((64, 8), (8, 64)):
                with self.subTest(sizes=sizes):
                    (output / "first.su").write_text(f"first.c:1:1:helper\t{sizes[0]}\tstatic\n")
                    (output / "second.su").write_text(f"second.c:2:1:helper\t{sizes[1]}\tstatic\n")
                    self.assertEqual(build.collect_stack_usage(output),
                                     {"helper": {"bytes": 64, "kind": "static"}})


if __name__ == "__main__":
    unittest.main()
