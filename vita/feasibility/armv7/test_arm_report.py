from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))


def report(platform="VITA", control_failure=False, runtime_failure=False):
    from arm_vectors import cases
    from fp_vectors import cases as runtime_cases
    lines = [f"ARM08 {'a' * 32} {'b' * 40} {'c' * 64} method=scalar-vfp-v1 phase=guest platform={platform}",
             f"ENV available={int(platform == 'VITA')} initial=00000000"]
    for index, case in enumerate(cases()):
        actual = "0000000000000000" if control_failure and index == 0 else case["expected"]
        requested = ((0, 3, 1, 2)[case["rn"]] << 22) | (case["fz"] << 24)
        status = "FAIL" if control_failure and index == 0 else "PASS"
        if platform == "HOST":
            requested, actual, status = 0, "0000000000000000", "SKIP"
        lines.append(f"CONTROL {case['id']} op={case['op']} rn={case['rn']} fz={case['fz']} "
                     f"a={case['a']} b={case['b']} expected={case['expected']} requested={requested:08x} "
                     f"readback={requested:08x} after={requested:08x} actual={actual} restored=00000000 {status}")
    for index, case in enumerate(runtime_cases()):
        actual = "0000000000000000" if runtime_failure and index == 0 else case["expected"]
        status = "FAIL" if runtime_failure and index == 0 else "PASS"
        lines.append(f"RUNTIME {case['id']} mode={case['mode']} expected={case['expected']} actual={actual} "
                     f"path=1 before=00000000 updated=00000000 operated=00000000 restored=00000000 restore=1 {status}")
    lines.extend(["RESTORE final=00000000 PASS",
                  f"END controls={'SKIP' if platform == 'HOST' else 'FAIL' if control_failure else 'PASS'} "
                  f"runtime={'FAIL' if runtime_failure else 'PASS'} restore=PASS control_cases=32 runtime_cases=71 "
                  f"control_failures={int(control_failure)} runtime_failures={int(runtime_failure)}"])
    return "\n".join(lines) + "\n"


class ArmReportTest(unittest.TestCase):
    def validate(self, text, platform="VITA"):
        from arm_report import analyze
        return analyze(text, "a" * 32, "b" * 40, "c" * 64, expected_platform=platform)

    def test_literal_control_oracles_have_distinct_arm_provenance(self):
        from arm_vectors import cases, PROVENANCE
        vectors = cases()
        self.assertEqual(len(vectors), 32)
        self.assertEqual(len({case['id'] for case in vectors}), 32)
        self.assertIn("DDI 0406", PROVENANCE)
        self.assertEqual(vectors[2]["expected"], "3ff0000000000001")

    def test_control_failure_is_an_attribution_stop_not_runtime_failure(self):
        result = self.validate(report(control_failure=True))
        self.assertEqual((result["controls"], result["runtime"], result["attribution"]),
                         ("FAIL", "PASS", "STOP_CONTROL"))

    def test_runtime_failure_with_correct_control_is_not_cpu_go(self):
        result = self.validate(report(runtime_failure=True))
        self.assertEqual((result["controls"], result["runtime"], result["attribution"]),
                         ("PASS", "FAIL", "REVIEW_RUNTIME_OBSERVATIONS"))

    def test_host_control_is_explicitly_skipped(self):
        self.assertEqual(self.validate(report("HOST"), "HOST")["controls"], "SKIP")

    def test_host_report_cannot_be_guest_evidence(self):
        with self.assertRaises(ValueError):
            self.validate(report("HOST"))

    def test_wrong_identity_is_rejected(self):
        for old, new in (("a" * 32, "d" * 32), ("b" * 40, "d" * 40), ("c" * 64, "d" * 64)):
            with self.subTest(old=old), self.assertRaises(ValueError):
                self.validate(report().replace(old, new))

    def test_truncation_duplicate_and_dishonest_verdict_are_rejected(self):
        text = report()
        lines = text.splitlines()
        for changed in (text[:-1], "\n".join(lines[:-1]) + "\n",
                        text.replace(lines[3], lines[2]), text.replace("actual=3ff0000000000000", "actual=0000000000000000", 1)):
            with self.subTest(changed=changed[:100]), self.assertRaises(ValueError):
                self.validate(changed)

    def test_failed_control_readback_is_preserved_as_valid_failure(self):
        text = report().replace("readback=00c00000", "readback=00000000", 1)
        lines = text.splitlines()
        lines[3] = lines[3].removesuffix("PASS") + "FAIL"
        lines[-1] = lines[-1].replace("controls=PASS", "controls=FAIL").replace("control_failures=0", "control_failures=1")
        self.assertEqual(self.validate("\n".join(lines) + "\n")["attribution"], "STOP_CONTROL")
