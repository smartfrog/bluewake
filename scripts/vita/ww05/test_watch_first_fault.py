"""TDD tests for the first-fault fail-closed watch loop.

These tests never touch an emulator, a VPK or a run: they exercise the pure
decision and sample functions with the exact lines observed in
ww-game-watch-2502ec8d (the guest [exception] and the host Invalid write/read
lines) and the flood sample from fault-sample/.

One test per behaviour.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from first_fault import (DEFAULT_CEILING_BYTES, HEAD_LINES, TAIL_LINES, WINDOW_LINES,
                         Observation, Stop, bounded_sample, decide)

GUEST_EXCEPTION = ("[exception] bits=00000001 pc=00000000 srr0=00000000 "
                   "srr1=00000000 msr=00000000 lr=00000000 r0=00000000 "
                   "r3=9564D471 r4=9564CC51 r12=E0000000 fpscr=00000000 "
                   "program=00000000 pi_cause=00010000 pi_mask=00000FFC")
HOST_WRITE = ("[17:20:14.925] |E| [MemoryWrite]: Invalid write of uint32_t "
              "at addr: 0x0, val = 0xdd2cb604")
HOST_READ = ("[17:20:14.927] |E| [MemoryRead]: Invalid read of uint32_t "
             "at address: 0x92ef44")


class FirstFaultDecisionTest(unittest.TestCase):
    def test_guest_exception_stops_first(self):
        self.assertEqual(decide(Observation(guest_exception_line=GUEST_EXCEPTION)),
                         Stop("guest-exception", GUEST_EXCEPTION))

    def test_host_invalid_write_stops(self):
        self.assertEqual(decide(Observation(host_fault_line=HOST_WRITE)),
                         Stop("host-invalid-access", HOST_WRITE))

    def test_host_invalid_read_stops(self):
        self.assertEqual(decide(Observation(host_fault_line=HOST_READ)),
                         Stop("host-invalid-access", HOST_READ))

    def test_log_ceiling_stops(self):
        sizes = {"/run/guest/ww05.log": 1024,
                 "/run/host/run.log": DEFAULT_CEILING_BYTES}
        self.assertEqual(decide(Observation(log_sizes=sizes)),
                         Stop("log-size-ceiling", "/run/host/run.log"))

    def test_below_ceiling_continues(self):
        sizes = {"/run/guest/ww05.log": 16 * 1024 * 1024,
                 "/run/host/run.log": DEFAULT_CEILING_BYTES - 1}
        self.assertIsNone(decide(Observation(log_sizes=sizes)))

    def test_guest_exception_wins_over_host_fault(self):
        observation = Observation(guest_exception_line=GUEST_EXCEPTION,
                                  host_fault_line=HOST_WRITE,
                                  log_sizes={"/run/host/run.log": DEFAULT_CEILING_BYTES})
        self.assertEqual(decide(observation), Stop("guest-exception", GUEST_EXCEPTION))

    def test_ceiling_boundary_is_inclusive(self):
        at_ceiling = Observation(log_sizes={"/run/host/run.log": DEFAULT_CEILING_BYTES})
        below = Observation(log_sizes={"/run/host/run.log": DEFAULT_CEILING_BYTES - 1})
        self.assertIsNotNone(decide(at_ceiling))
        self.assertIsNone(decide(below))


class BoundedSampleTest(unittest.TestCase):
    def test_bounded_sample_never_exceeds_its_bounds(self):
        lines = [f"line {index}" for index in range(10000)]
        sample = bounded_sample(lines, fault_index=5000)
        self.assertEqual(len(sample["head"]), HEAD_LINES)
        self.assertEqual(len(sample["fault"]), WINDOW_LINES)
        self.assertEqual(len(sample["tail"]), TAIL_LINES)
        self.assertEqual(sample["fault"][WINDOW_LINES // 2], "line 5000")
        self.assertLess(len(sample["head"]) + len(sample["fault"]) + len(sample["tail"]),
                        len(lines))


if __name__ == "__main__":
    unittest.main()
