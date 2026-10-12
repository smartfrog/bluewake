import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import memory_snapshot


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)
        self.manifest = self.root / 'memory.json'
        self.payload = self.root / 'memory.bin'
        self.fifo_hash = hashlib.sha256(b'captured-fifo').hexdigest()

    def tearDown(self):
        self.temporary.cleanup()

    def write_snapshot(self, regions=None, data=b'captured-memory'):
        self.payload.write_bytes(data)
        manifest = dict(format='gx-mem1-snapshot-v1', binary=self.payload.name,
                        binary_sha256=hashlib.sha256(data).hexdigest(), fifo_sha256=self.fifo_hash,
                        regions=regions or [dict(address='0x00001000', offset=0, size=len(data))])
        self.manifest.write_text(json.dumps(manifest))
        return manifest

    def rewrite(self, manifest):
        self.manifest.write_text(json.dumps(manifest))

    def test_sparse_memory_keeps_physical_address_and_exact_payload(self):
        self.write_snapshot()
        loaded = memory_snapshot.load(self.manifest, self.fifo_hash)
        self.assertEqual(loaded.image[0x1000:0x100F], b'captured-memory')
        self.assertEqual(loaded.ranges, [(0x1000, 15)])

    def test_full_mem1_is_accepted(self):
        data = bytes(memory_snapshot.MEM1_BYTES)
        self.write_snapshot([dict(address=0, offset=0, size=len(data))], data)
        loaded = memory_snapshot.load(self.manifest, self.fifo_hash)
        self.assertEqual(loaded.image, data)
        self.assertEqual(loaded.ranges, [(0, len(data))])

    def test_adjacent_regions_merge_for_resolver_available_span(self):
        self.write_snapshot([dict(address=0x1002, offset=2, size=2),
                             dict(address=0x1000, offset=0, size=2)], b'abcd')
        loaded = memory_snapshot.load(self.manifest, self.fifo_hash)
        self.assertEqual(loaded.ranges, [(0x1000, 4)])

    def test_physical_hole_is_retained_in_coverage(self):
        self.write_snapshot([dict(address=0x1000, offset=0, size=2),
                             dict(address=0x2000, offset=2, size=2)], b'abcd')
        loaded = memory_snapshot.load(self.manifest, self.fifo_hash)
        self.assertEqual(loaded.ranges, [(0x1000, 2), (0x2000, 2)])

    def test_binary_hash_mismatch_is_refused(self):
        manifest = self.write_snapshot()
        manifest['binary_sha256'] = '0' * 64
        self.rewrite(manifest)
        with self.assertRaisesRegex(ValueError, 'binary.*SHA-256'):
            memory_snapshot.load(self.manifest, self.fifo_hash)

    def test_wrong_capture_is_refused(self):
        self.write_snapshot()
        with self.assertRaisesRegex(ValueError, 'FIFO.*SHA-256'):
            memory_snapshot.load(self.manifest, '0' * 64)

    def test_overlapping_physical_regions_are_refused(self):
        self.write_snapshot([dict(address=0x1000, offset=0, size=4),
                             dict(address=0x1002, offset=4, size=4)], b'abcdefgh')
        with self.assertRaisesRegex(ValueError, 'overlap'):
            memory_snapshot.load(self.manifest, self.fifo_hash)

    def test_out_of_mem1_region_is_refused(self):
        self.write_snapshot([dict(address=memory_snapshot.MEM1_BYTES - 1, offset=0, size=2)], b'ab')
        with self.assertRaisesRegex(ValueError, 'MEM1'):
            memory_snapshot.load(self.manifest, self.fifo_hash)

    def test_truncated_payload_is_refused(self):
        self.write_snapshot([dict(address=0x1000, offset=0, size=3)], b'ab')
        with self.assertRaisesRegex(ValueError, 'binary bounds'):
            memory_snapshot.load(self.manifest, self.fifo_hash)

    def test_boolean_size_is_not_an_integer_extent(self):
        self.write_snapshot([dict(address=0x1000, offset=0, size=True)], b'ab')
        with self.assertRaisesRegex(ValueError, 'integer'):
            memory_snapshot.load(self.manifest, self.fifo_hash)

    def test_binary_path_cannot_escape_manifest_directory(self):
        manifest = self.write_snapshot()
        manifest['binary'] = '../outside.bin'
        self.rewrite(manifest)
        with self.assertRaisesRegex(ValueError, 'binary.*path'):
            memory_snapshot.load(self.manifest, self.fifo_hash)

    def test_unknown_version_is_refused(self):
        manifest = self.write_snapshot()
        manifest['format'] = 'gx-mem1-snapshot-v2'
        self.rewrite(manifest)
        with self.assertRaisesRegex(ValueError, 'format'):
            memory_snapshot.load(self.manifest, self.fifo_hash)

    def test_cached_addresses_are_not_physical_manifest_addresses(self):
        self.write_snapshot([dict(address='0x80001000', offset=0, size=2)], b'ab')
        with self.assertRaisesRegex(ValueError, 'MEM1'):
            memory_snapshot.load(self.manifest, self.fifo_hash)

    def test_coverage_transport_has_explicit_version_and_ranges(self):
        self.write_snapshot()
        loaded = memory_snapshot.load(self.manifest, self.fifo_hash)
        self.assertEqual(loaded.coverage_text(), 'GX_MEM1_RANGES_V1\n00001000 0000000F\n')

    def test_duplicate_manifest_keys_are_refused(self):
        self.write_snapshot()
        text = self.manifest.read_text().replace('"regions":', '"format": "gx-mem1-snapshot-v1", "regions":')
        self.manifest.write_text(text)
        with self.assertRaisesRegex(ValueError, 'duplicate snapshot key'):
            memory_snapshot.load(self.manifest, self.fifo_hash)

    def test_overlapping_binary_spans_are_refused(self):
        self.write_snapshot([dict(address=0x1000, offset=0, size=4),
                             dict(address=0x2000, offset=2, size=4)], b'abcdef')
        with self.assertRaisesRegex(ValueError, 'binary regions overlap'):
            memory_snapshot.load(self.manifest, self.fifo_hash)

    def test_staged_manifest_can_be_loaded_again(self):
        self.write_snapshot()
        loaded = memory_snapshot.load(self.manifest, self.fifo_hash)
        output = self.root / 'fixture'
        output.mkdir()
        loaded.stage(output)
        replayed = memory_snapshot.load(output / 'memory.json', self.fifo_hash)
        self.assertEqual(replayed.image, loaded.image)
        self.assertEqual(replayed.ranges, loaded.ranges)


if __name__ == '__main__':
    unittest.main()
