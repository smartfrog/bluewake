"""Tests for the one-shot memory snapshot summary.

A WW56 snapshot reads the owned process's smaps once, groups mappings by
backing and permission, and records cgroup readings. The summary is evidence
about mapping classes, never proof of ownership: the limitations stay in the
summary.
"""

import unittest

from snapshot import summarize_smaps

SMAPS = (
    "55d0000000-55d0100000 r-xp 00000000 00:01 123 /usr/bin/Vita3K\n"
    "Size:               1024 kB\n"
    "Rss:                 512 kB\n"
    "Pss:                 500 kB\n"
    "Anonymous:             0 kB\n"
    "Private_Clean:       512 kB\n"
    "Private_Dirty:         0 kB\n"
    "Shared_Clean:          0 kB\n"
    "Shared_Dirty:          0 kB\n"
    "Swap:                  0 kB\n"
    "7f00000000-7f00100000 rw-p 00000000 00:00 0 [anon]\n"
    "Size:               2048 kB\n"
    "Rss:                1024 kB\n"
    "Pss:                1020 kB\n"
    "Anonymous:          1024 kB\n"
    "Private_Clean:         0 kB\n"
    "Private_Dirty:      1024 kB\n"
    "Shared_Clean:          0 kB\n"
    "Shared_Dirty:          0 kB\n"
    "Swap:                  0 kB\n"
    "7f10000000-7f10001000 r-xp 00000000 00:01 456 /memfd:jit (deleted)\n"
    "Size:                 64 kB\n"
    "Rss:                  32 kB\n"
    "Pss:                  30 kB\n"
    "Anonymous:            32 kB\n"
    "Private_Clean:        32 kB\n"
    "Private_Dirty:         0 kB\n"
    "Shared_Clean:          0 kB\n"
    "Shared_Dirty:          0 kB\n"
    "Swap:                  0 kB\n"
)


class SummarizeSmapsTests(unittest.TestCase):
    def test_totals_sum_every_mapping_metric(self):
        summary = summarize_smaps(SMAPS)
        self.assertEqual(summary['units'], 'bytes')
        self.assertEqual(summary['mapping_count'], 3)
        self.assertEqual(summary['totals']['Rss'], (512 + 1024 + 32) * 1024)
        self.assertEqual(summary['totals']['Pss'], (500 + 1020 + 30) * 1024)

    def test_primary_backing_groups_are_disjoint(self):
        groups = summarize_smaps(SMAPS)['groups']
        self.assertEqual(groups['anonymous_backing']['Rss'], 1024 * 1024)
        self.assertEqual(groups['file_or_memfd_backed']['Rss'], (512 + 32) * 1024)
        self.assertEqual(groups['special_named']['Rss'], 0)

    def test_executable_groups_overlap_and_are_candidates_only(self):
        groups = summarize_smaps(SMAPS)['groups']
        self.assertEqual(groups['all_executable_mappings']['Rss'], (512 + 32) * 1024)
        self.assertEqual(groups['executable_anonymous_or_memfd_JIT_candidate_NOT_proven_JIT']['Rss'], 32 * 1024)
        self.assertIn('not proof', summarize_smaps(SMAPS)['limitations'][2])

    def test_an_unreadable_smaps_is_an_error(self):
        with self.assertRaisesRegex(ValueError, 'smaps'):
            summarize_smaps("not a mapping\n")


if __name__ == '__main__':
    unittest.main()
