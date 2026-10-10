from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))

SOURCE = "a" * 40
MASK = 0x01C00000


def mode_word(rn, fz):
    return ((0, 3, 1, 2)[rn] << 22) | (fz << 24)


def control_line(case, initial=0, actual=None, verdict=None, requested=None, readback=None,
                 after=None, restored=None):
    requested = (initial & ~MASK) | mode_word(case["rn"], case["fz"]) if requested is None else requested
    actual = case["expected"] if actual is None else actual
    readback = requested if readback is None else readback
    after = requested if after is None else after
    restored = initial if restored is None else restored
    if verdict is None:
        failure = (readback != requested or (after & MASK) != (requested & MASK) or
                   actual != case["expected"] or restored != initial)
        verdict = "FAIL" if failure else "PASS"
    return (f"CONTROL {case['id']} op={case['op']} rn={case['rn']} fz={case['fz']} a={case['a']} "
            f"b={case['b']} expected={case['expected']} requested={requested:08x} "
            f"readback={readback:08x} after={after:08x} actual={actual} restored={restored:08x} {verdict}")


def fake_report(cases, overrides=None, initial=0):
    overrides = overrides or {}
    lines = [f"QEMU-CONTROL source={SOURCE} machine=vexpress-a9 cpu=cortex-a9 method=scalar-vfp-v1",
             f"ENV available=1 initial={initial:08x}"]
    failures = 0
    restore_ok = True
    for index, case in enumerate(cases):
        line = control_line(case, initial=initial, **overrides.get(index, {}))
        failures += line.endswith("FAIL")
        restore_ok &= line.split("restored=")[1].split()[0] == f"{initial:08x}"
        lines.append(line)
    lines.append("RUNTIME skipped reason=not-integrated cases=71")
    lines.append(f"END controls={'FAIL' if failures else 'PASS'} control_cases={len(cases)} "
                 f"control_failures={failures} restore={'PASS' if restore_ok else 'FAIL'}")
    return "\n".join(lines) + "\n"


FAKE_OBJDUMP = """\
60000100 <arm_vfp_read>:
60000100:\teef10a10 \tvmrs\tr0, fpscr
60000104:\te12fff1e \tbx\tlr

60000108 <arm_vfp_write>:
60000108:\teee10a10 \tvmsr\tfpscr, r0
6000010c:\te12fff1e \tbx\tlr

60000110 <arm_vfp_add>:
60000110:\teb000000 \tbl\t60000120 <arm_vfp_mul>
60000114:\tee302b01 \tvadd.f64\td2, d0, d1

60000120 <arm_vfp_mul>:
60000120:\tee202b01 \tvmul.f64\td2, d0, d1

60000130 <arm_vfp_convert>:
60000130:\teeb70ac0 \tvcvt.f32.f64\ts4, d0
60000134:\teeb70bc0 \tvcvt.f64.f32\td3, s4
"""


class QemuControlParseTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from arm_vectors import cases
        cls.cases = cases()

    def test_valid_report_parses_identity_environment_and_32_controls(self):
        from qemu_control import parse_report
        parsed = parse_report(fake_report(self.cases))
        self.assertEqual(parsed["identity"]["source"], SOURCE)
        self.assertEqual(parsed["identity"]["machine"], "vexpress-a9")
        self.assertEqual(parsed["identity"]["cpu"], "cortex-a9")
        self.assertEqual(parsed["initial"], 0)
        self.assertEqual(len(parsed["controls"]), 32)
        self.assertEqual(parsed["controls"][0]["id"], self.cases[0]["id"])
        self.assertEqual(parsed["end"]["control_failures"], 0)

    def test_truncated_report_is_rejected(self):
        from qemu_control import parse_report
        lines = fake_report(self.cases).splitlines()
        with self.assertRaises(ValueError):
            parse_report("\n".join(lines[:30]) + "\n")

    def test_excess_line_is_rejected(self):
        from qemu_control import parse_report
        text = fake_report(self.cases)
        with self.assertRaises(ValueError):
            parse_report(text.replace("RUNTIME skipped", "EXTRA junk\nRUNTIME skipped"))

    def test_missing_runtime_skip_record_is_rejected(self):
        from qemu_control import parse_report
        text = fake_report(self.cases)
        with self.assertRaises(ValueError):
            parse_report(text.replace("RUNTIME skipped reason=not-integrated cases=71\n", ""))

    def test_environment_without_vfp_is_rejected(self):
        from qemu_control import parse_report
        text = fake_report(self.cases).replace("ENV available=1", "ENV available=0")
        with self.assertRaises(ValueError):
            parse_report(text)


class QemuControlCompareTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from arm_vectors import cases
        cls.cases = cases()

    def compare(self, text):
        from qemu_control import compare_controls, parse_report
        return compare_controls(parse_report(text), self.cases)

    def index_of(self, case_id):
        return next(index for index, case in enumerate(self.cases) if case["id"] == case_id)

    def test_all_matching_bits_pass(self):
        results = self.compare(fake_report(self.cases))
        self.assertEqual([result["verdict"] for result in results], ["PASS"] * 32)
        self.assertEqual([result for result in results if result["failure"]], [])

    def test_bit_mismatch_fails_with_expected_and_observed(self):
        index = self.index_of("vadd-positive-rn2-fz0")
        results = self.compare(fake_report(self.cases, {index: {"actual": "3ff0000000000000"}}))
        self.assertEqual(results[index]["verdict"], "FAIL")
        self.assertIn("3ff0000000000001", results[index]["failure"])
        self.assertIn("3ff0000000000000", results[index]["failure"])

    def test_dishonest_pass_verdict_is_rejected(self):
        index = self.index_of("vadd-positive-rn2-fz0")
        text = fake_report(self.cases, {index: {"actual": "3ff0000000000000", "verdict": "PASS"}})
        with self.assertRaises(ValueError):
            self.compare(text)

    def test_wrong_case_identity_is_rejected(self):
        text = fake_report(self.cases).replace("vadd-positive-rn0-fz0", "vadd-positive-rn0-fz1", 1)
        with self.assertRaises(ValueError):
            self.compare(text)

    def test_requested_mode_that_drops_initial_bits_is_rejected(self):
        index = self.index_of("vadd-positive-rn0-fz0")
        text = fake_report(self.cases, {index: {"requested": 0x01000000}}, initial=0x02000000)
        with self.assertRaises(ValueError):
            self.compare(text)

    def test_readback_mismatch_fails(self):
        index = self.index_of("vmul-input-positive-rn0-fz1")
        results = self.compare(fake_report(self.cases, {index: {"readback": 0}}))
        self.assertEqual(results[index]["verdict"], "FAIL")
        self.assertIn("readback", results[index]["failure"])

    def test_restoration_mismatch_fails(self):
        index = self.index_of("vcvt-positive-even-rn2-fz0")
        results = self.compare(fake_report(self.cases, {index: {"restored": 0x00000010}}))
        self.assertEqual(results[index]["verdict"], "FAIL")
        self.assertIn("restored", results[index]["failure"])

    def test_end_failure_count_mismatch_is_rejected(self):
        index = self.index_of("vadd-positive-rn2-fz0")
        text = fake_report(self.cases, {index: {"actual": "3ff0000000000000"}})
        text = text.replace("control_failures=1", "control_failures=0")
        with self.assertRaises(ValueError):
            self.compare(text)

    def test_end_verdict_mismatch_is_rejected(self):
        text = fake_report(self.cases).replace("END controls=PASS", "END controls=FAIL")
        with self.assertRaises(ValueError):
            self.compare(text)


class QemuControlFormatTest(unittest.TestCase):
    def test_table_shows_each_case_with_expected_observed_and_verdict(self):
        from qemu_control import format_results
        results = [
            {"id": "vadd-positive-rn0-fz0", "expected": "3ff0000000000000",
             "actual": "3ff0000000000000", "verdict": "PASS", "failure": None},
            {"id": "vadd-positive-rn2-fz0", "expected": "3ff0000000000001",
             "actual": "3ff0000000000000", "verdict": "FAIL",
             "failure": "actual=3ff0000000000000 expected=3ff0000000000001"},
        ]
        text = format_results(results)
        self.assertIn("vadd-positive-rn0-fz0", text)
        self.assertIn("3ff0000000000000", text)
        self.assertIn("PASS", text)
        self.assertIn("vadd-positive-rn2-fz0", text)
        self.assertIn("3ff0000000000001", text)
        self.assertIn("FAIL", text)

    def test_failure_detail_names_the_case_and_both_bit_patterns(self):
        from qemu_control import format_results
        results = [
            {"id": "vmul-input-negative-rn0-fz1", "expected": "8000000000000000",
             "actual": "8000000000000002", "verdict": "FAIL",
             "failure": "actual=8000000000000002 expected=8000000000000000"},
        ]
        text = format_results(results)
        self.assertIn("vmul-input-negative-rn0-fz1", text)
        self.assertIn("8000000000000002", text)
        self.assertIn("8000000000000000", text)

    def test_summary_counts_pass_and_fail(self):
        from qemu_control import format_results
        results = [{"id": f"case-{index}", "expected": "0" * 16, "actual": "0" * 16,
                    "verdict": "PASS" if index else "FAIL", "failure": "x" if index == 0 else None}
                   for index in range(4)]
        text = format_results(results)
        self.assertIn("control_cases=4", text)
        self.assertIn("control_failures=1", text)
        self.assertIn("controls=FAIL", text)


class QemuInstructionProofTest(unittest.TestCase):
    def test_complete_objdump_has_no_missing_scalar_instructions(self):
        from qemu_control import instruction_proof
        proof = instruction_proof(FAKE_OBJDUMP)
        self.assertEqual(proof["missing"], [])

    def test_missing_vadd_f64_is_reported(self):
        from qemu_control import instruction_proof
        proof = instruction_proof(FAKE_OBJDUMP.replace("vadd.f64\td2, d0, d1", "vmov\td2, d0"))
        self.assertIn(("arm_vfp_add", "vadd.f64"), proof["missing"])

    def test_missing_vcvt_is_reported(self):
        from qemu_control import instruction_proof
        proof = instruction_proof(FAKE_OBJDUMP.replace("vcvt.f64.f32\td3, s4", "vmov\td3, s4"))
        self.assertIn(("arm_vfp_convert", "vcvt.f64.f32"), proof["missing"])

    def test_missing_function_is_reported(self):
        from qemu_control import instruction_proof
        proof = instruction_proof(FAKE_OBJDUMP.split("60000130 <arm_vfp_convert>")[0])
        self.assertIn(("arm_vfp_convert", "vcvt.f32.f64"), proof["missing"])


if __name__ == "__main__":
    unittest.main()
