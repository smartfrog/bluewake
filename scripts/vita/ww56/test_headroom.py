"""Tests for the one-shot MemAvailable headroom gate.

WW56 blocks before a phase when the host cannot afford the phase's static
budget plus the required headroom. The gate never proceeds on a deficit and
never touches other applications.
"""

import unittest

from headroom import (
    DEFAULT_GUEST_BUDGET_BYTES,
    DEFAULT_GUEST_HEADROOM_BYTES,
    DEFAULT_INSTALL_BUDGET_BYTES,
    DEFAULT_INSTALL_HEADROOM_BYTES,
    headroom_decision,
    parse_mem_available,
)

MEMINFO = (
    "MemTotal:       32768000 kB\n"
    "MemFree:         4194304 kB\n"
    "MemAvailable:   12582912 kB\n"
    "Buffers:          262144 kB\n"
)


class ParseMemAvailableTests(unittest.TestCase):
    def test_reads_the_single_memavailable_value_in_bytes(self):
        self.assertEqual(parse_mem_available(MEMINFO), 12582912 * 1024)

    def test_missing_value_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "MemAvailable"):
            parse_mem_available("MemTotal: 32768000 kB\n")

    def test_duplicate_value_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "MemAvailable"):
            parse_mem_available(MEMINFO + "MemAvailable: 1 kB\n")

    def test_malformed_value_is_an_error(self):
        for line in ("MemAvailable:   12.5 kB\n", "MemAvailable: 123 MB\n", "MemAvailable: kB\n"):
            with self.subTest(line=line):
                with self.assertRaisesRegex(ValueError, "MemAvailable"):
                    parse_mem_available(line)


class HeadroomDecisionTests(unittest.TestCase):
    def test_passes_at_exactly_the_required_headroom(self):
        decision = headroom_decision(5 * 1024 ** 3, 5 * 1024 ** 3)
        self.assertTrue(decision["ok"])
        self.assertEqual(decision["deficit_bytes"], 0)

    def test_blocks_below_required_headroom_with_the_deficit(self):
        decision = headroom_decision(4 * 1024 ** 3, 4831838208)
        self.assertFalse(decision["ok"])
        self.assertEqual(decision["deficit_bytes"], 4831838208 - 4 * 1024 ** 3)

    def test_negative_or_boolean_inputs_are_refused(self):
        for available, required in ((-1, 1), (1, -1), (True, 1), (1, False)):
            with self.subTest(available=available, required=required):
                with self.assertRaisesRegex(ValueError, "byte count"):
                    headroom_decision(available, required)


class DefaultEnvelopeTests(unittest.TestCase):
    def test_coordinator_memory_envelope_defaults(self):
        self.assertEqual(DEFAULT_INSTALL_BUDGET_BYTES, 2684354560)
        self.assertEqual(DEFAULT_GUEST_BUDGET_BYTES, 4 * 1024 ** 3)
        self.assertEqual(DEFAULT_INSTALL_HEADROOM_BYTES, 2684354560 + 512 * 1024 ** 2)
        self.assertEqual(DEFAULT_GUEST_HEADROOM_BYTES, 4831838208)


if __name__ == '__main__':
    unittest.main()
