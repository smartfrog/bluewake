"""Tests for scripts/vita/relset/mem1_regions.py.

The MEM1 snapshots are full raw physical dumps (25,165,824 bytes, guest base
0x80000000). What they prove is bounded: the count, extent and growth of
non-zero bytes. Zero-valued live data is invisible, so these are lower bounds
on occupancy, never "used memory". The integration tests read the newest real
run's two snapshots when they are present and skip elsewhere.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/relset"))

from mem1_regions import (  # noqa: E402
    Mem1RegionError,
    compare_snapshots,
    measure_snapshot,
)

EVIDENCE = Path("/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05")
RUN = EVIDENCE / ("vita3k-run-1351063a0837427b87d242d99ace55ec/data/Vita3K"
                  "/Vita3K/ux0/data/bluewake-ww-fixture")


class MeasureSnapshotTest(unittest.TestCase):
    def test_counts_nonzero_bytes_and_their_extent(self):
        stats = measure_snapshot(bytes([0, 0, 5, 0, 7, 0]), guest_base=0x80000000)
        self.assertEqual(stats.bytes_total, 6)
        self.assertEqual(stats.nonzero, 2)
        self.assertEqual(stats.first_nonzero, 0x80000002)
        self.assertEqual(stats.last_nonzero, 0x80000004)

    def test_an_all_zero_snapshot_has_no_extent(self):
        stats = measure_snapshot(bytes(8), guest_base=0x80000000)
        self.assertEqual(stats.nonzero, 0)
        self.assertIsNone(stats.first_nonzero)
        self.assertIsNone(stats.last_nonzero)


class CompareSnapshotsTest(unittest.TestCase):
    def test_counts_newly_written_cleared_and_changed_bytes(self):
        growth = compare_snapshots(bytes([0, 0, 5, 0, 7, 0]),
                                   bytes([0, 3, 5, 0, 0, 9]))
        self.assertEqual(growth.newly_written, 2)
        self.assertEqual(growth.cleared, 1)
        self.assertEqual(growth.changed, 3)

    def test_identical_snapshots_show_no_change(self):
        growth = compare_snapshots(bytes([1, 2, 3, 4]), bytes([1, 2, 3, 4]))
        self.assertEqual((growth.newly_written, growth.cleared,
                          growth.changed), (0, 0, 0))

    def test_refuses_snapshots_of_different_sizes(self):
        with self.assertRaises(Mem1RegionError):
            compare_snapshots(bytes(4), bytes(8))


class RealEvidenceTest(unittest.TestCase):
    def setUp(self):
        if not RUN.is_dir():
            self.skipTest("the newest ww05 run is not on this machine")

    def test_the_two_snapshots_cover_the_full_24_mib_of_mem1(self):
        stats = measure_snapshot((RUN / "ww05.mem1-1.bin").read_bytes(),
                                 guest_base=0x80000000)
        self.assertEqual(stats.bytes_total, 25_165_824)
        self.assertEqual(stats.nonzero, 3_119_063)
        self.assertEqual(stats.first_nonzero, 0x80000020)
        self.assertEqual(stats.last_nonzero, 0x815304E7)

    def test_the_boot_grows_mem1_by_a_few_kib_between_the_snapshots(self):
        growth = compare_snapshots((RUN / "ww05.mem1-1.bin").read_bytes(),
                                   (RUN / "ww05.mem1-2.bin").read_bytes())
        self.assertEqual(growth.newly_written, 5_120)
        self.assertEqual(growth.cleared, 520)
        self.assertEqual(growth.changed, 13_135)


if __name__ == "__main__":
    unittest.main()
