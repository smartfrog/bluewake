#!/usr/bin/env python3
"""Measure what the WW05 MEM1 snapshots actually hold.

The snapshots are raw physical MEM1 dumps: 25,165,824 bytes at guest base
0x80000000, taken at named commands during a boot. Two facts can be read
honestly from them:

- the non-zero byte count and the extent of the non-zero region, as a lower
  bound on what the guest has written (zero-valued live data is invisible);
- the growth between two snapshots: bytes that went zero -> non-zero (newly
  written), non-zero -> zero (cleared) and any byte that changed.

The REL scratch lives at 0x81822E80, above MEM1's end at 0x81800000, so it is
not in these dumps at all; the run's own markers carry it instead. Nothing
here is "used memory": it is what the bytes prove.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

_IS_ZERO = bytes(1 if value == 0 else 0 for value in range(256))


class Mem1RegionError(ValueError):
    """The input is not a pair of comparable snapshots."""


@dataclass(frozen=True)
class SnapshotStats:
    bytes_total: int
    guest_base: int
    nonzero: int
    first_nonzero: Optional[int]
    last_nonzero: Optional[int]


@dataclass(frozen=True)
class SnapshotGrowth:
    newly_written: int
    cleared: int
    changed: int


def measure_snapshot(data: bytes, guest_base: int) -> SnapshotStats:
    total = len(data)
    nonzero = total - data.count(0)
    if nonzero == 0:
        return SnapshotStats(bytes_total=total, guest_base=guest_base,
                             nonzero=0, first_nonzero=None, last_nonzero=None)
    first = total - len(data.lstrip(b"\x00"))
    last = len(data.rstrip(b"\x00")) - 1
    return SnapshotStats(bytes_total=total, guest_base=guest_base,
                         nonzero=nonzero,
                         first_nonzero=guest_base + first,
                         last_nonzero=guest_base + last)


def _zero_bits(data: bytes) -> int:
    return int.from_bytes(data.translate(_IS_ZERO), "big")


def compare_snapshots(before: bytes, after: bytes) -> SnapshotGrowth:
    if len(before) != len(after):
        raise Mem1RegionError(
            "snapshots differ in size: " + str(len(before)) + " and "
            + str(len(after)))
    ones = int.from_bytes(b"\x01" * len(before), "big")
    before_zero = _zero_bits(before)
    after_zero = _zero_bits(after)
    newly_written = (before_zero & (ones ^ after_zero)).bit_count()
    cleared = (after_zero & (ones ^ before_zero)).bit_count()
    if len(before) % 4 == 0:
        words_before = memoryview(before).cast("I")
        words_after = memoryview(after).cast("I")
        changed = 0
        for left, right in zip(words_before, words_after):
            if left == right:
                continue
            changed += sum(1 for a, b in zip(left.to_bytes(4, "little"),
                                             right.to_bytes(4, "little"))
                           if a != b)
    else:
        changed = sum(1 for left, right in zip(before, after) if left != right)
    return SnapshotGrowth(newly_written=newly_written, cleared=cleared,
                          changed=changed)
