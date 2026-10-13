"""Tests for pre-exec static-budget property and kernel-limit verification.

Every WW56 phase unit must prove MemoryMax, EffectiveMemoryMax and
MemorySwapMax before its gate releases the guest command. The property text
comes from `systemctl --user show`; the kernel text comes from the unit's
cgroup memory.max / memory.swap.max files.
"""

import unittest

from budget_properties import (
    parse_cgroup_limit,
    parse_properties,
    verify_budget_properties,
    verify_cgroup_budget,
)

BUDGET = 4294967296
SHOW = (
    "ControlGroup=/user.slice/user-1000.slice/user@1000.service/app.slice/ww56-1-guest.service\n"
    "MainPID=1547682\n"
    "MemoryMax=4294967296\n"
    "EffectiveMemoryMax=4294967296\n"
    "MemorySwapMax=0\n"
    "OOMPolicy=stop\n"
    "ActiveState=active\n"
)


class ParsePropertiesTests(unittest.TestCase):
    def test_parses_key_value_lines(self):
        self.assertEqual(parse_properties(SHOW)["MainPID"], "1547682")
        self.assertEqual(parse_properties(SHOW)["MemoryMax"], "4294967296")

    def test_rejects_duplicate_keys(self):
        with self.assertRaisesRegex(ValueError, "duplicate"):
            parse_properties(SHOW + "MainPID=1\n")

    def test_rejects_lines_without_a_key_value_pair(self):
        with self.assertRaisesRegex(ValueError, "unreadable property"):
            parse_properties("MainPID\n")


class VerifyBudgetPropertiesTests(unittest.TestCase):
    def test_accepts_the_exact_static_budget(self):
        evidence = verify_budget_properties(parse_properties(SHOW), budget_bytes=BUDGET)
        self.assertEqual(evidence["MemoryMax"], "4294967296")

    def test_rejects_memory_max_mismatch(self):
        props = parse_properties(SHOW.replace("MemoryMax=4294967296", "MemoryMax=2684354560"))
        with self.assertRaisesRegex(ValueError, "MemoryMax"):
            verify_budget_properties(props, budget_bytes=BUDGET)

    def test_rejects_effective_memory_max_mismatch(self):
        props = parse_properties(SHOW.replace("EffectiveMemoryMax=4294967296", "EffectiveMemoryMax=infinity"))
        with self.assertRaisesRegex(ValueError, "EffectiveMemoryMax"):
            verify_budget_properties(props, budget_bytes=BUDGET)

    def test_rejects_a_nonzero_swap_max(self):
        props = parse_properties(SHOW.replace("MemorySwapMax=0", "MemorySwapMax=infinity"))
        with self.assertRaisesRegex(ValueError, "MemorySwapMax"):
            verify_budget_properties(props, budget_bytes=BUDGET)

    def test_rejects_a_different_oom_policy(self):
        props = parse_properties(SHOW.replace("OOMPolicy=stop", "OOMPolicy=continue"))
        with self.assertRaisesRegex(ValueError, "OOMPolicy"):
            verify_budget_properties(props, budget_bytes=BUDGET)

    def test_rejects_missing_properties(self):
        with self.assertRaisesRegex(ValueError, "MemoryMax"):
            verify_budget_properties({"MemoryMax": "4294967296"}, budget_bytes=BUDGET)

    def test_rejects_a_non_byte_budget(self):
        with self.assertRaisesRegex(ValueError, "budget"):
            verify_budget_properties(parse_properties(SHOW), budget_bytes=0)


class CgroupBudgetTests(unittest.TestCase):
    def test_parses_max_and_numeric_limits(self):
        self.assertEqual(parse_cgroup_limit("4294967296\n"), BUDGET)
        self.assertEqual(parse_cgroup_limit("max\n"), "max")

    def test_rejects_unreadable_limits(self):
        for text in ("", "4G\n", "-1\n"):
            with self.subTest(text=text):
                with self.assertRaisesRegex(ValueError, "memory limit"):
                    parse_cgroup_limit(text)

    def test_accepts_matching_kernel_limits(self):
        evidence = verify_cgroup_budget("4294967296\n", "0\n", BUDGET)
        self.assertEqual(evidence["kernel_memory_max"], BUDGET)
        self.assertEqual(evidence["kernel_swap_max"], 0)

    def test_rejects_an_unlimited_kernel_memory_max(self):
        with self.assertRaisesRegex(ValueError, "memory.max"):
            verify_cgroup_budget("max\n", "0\n", BUDGET)

    def test_rejects_a_mismatched_kernel_memory_max(self):
        with self.assertRaisesRegex(ValueError, "memory.max"):
            verify_cgroup_budget("2684354560\n", "0\n", BUDGET)

    def test_rejects_a_nonzero_kernel_swap_max(self):
        with self.assertRaisesRegex(ValueError, "memory.swap.max"):
            verify_cgroup_budget("4294967296\n", "max\n", BUDGET)


if __name__ == '__main__':
    unittest.main()
