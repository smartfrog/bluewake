from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

import test_di_read
import test_host


def yaz0_literals(content):
    return b'Yaz0' + struct.pack('>I', len(content)) + bytes(8) + b''.join(
        b'\xff' + content[index:index + 8] for index in range(0, len(content), 8))


def raw_rel():
    content = bytearray(256)
    for offset, value in ((0, 1), (0xC, 2), (0x10, 0x4C), (0x1C, 3),
                          (0x24, 0xA8), (0x28, 0xA0), (0x2C, 8)):
        struct.pack_into('>I', content, offset, value)
    content[0x30] = 1
    struct.pack_into('>II', content, 0x54, 0x81, 0x20)
    struct.pack_into('>II', content, 0xA0, 0, 0xA8)
    content[0xAA] = 203
    for index in range(0x80, 0xA0):
        content[index] = (index * 7 + 3) & 255
    return bytes(content)


def rel_archive(scenario):
    raw = bytearray(raw_rel())
    if scenario == 'bad-rel':
        struct.pack_into('>I', raw, 0x10, 0xFFFFFFFC)
    if scenario == 'wrong-module':
        struct.pack_into('>I', raw, 0, 99)
    if scenario == 'metadata':
        struct.pack_into('>I', raw, 0xC, 3)
    if scenario == 'catalog-word':
        struct.pack_into('>I', raw, 4, 1)
    member = yaz0_literals(raw) if scenario in ('compressed', 'bad-yaz0') else bytes(raw)
    if scenario == 'bad-yaz0':
        member = member[:17]
    archive = bytearray(0x80)
    archive[:4] = b'RARC'
    struct.pack_into('>I', archive, 0xC, 0x60)
    struct.pack_into('>II', archive, 0x28, 1, 0x20)
    name = b'f_pc_profile_lst.rel\0'
    struct.pack_into('>II', archive, 0x30, len(name), 0x34)
    struct.pack_into('>III', archive, 0x44, 0x01000000, 0, len(member))
    archive[0x54:0x54 + len(name)] = name
    archive.extend(member)
    struct.pack_into('>I', archive, 4, len(archive))
    if scenario == 'bad-archive':
        struct.pack_into('>I', archive, 0x2C, 0xFFFFFFF0)
    if scenario == 'oversize':
        member = b'Yaz0' + struct.pack('>I', 0x100001) + bytes(8)
        archive = archive[:0x80] + member
        struct.pack_into('>I', archive, 0x4C, len(member))
        struct.pack_into('>I', archive, 4, len(archive))
    return yaz0_literals(archive) if scenario == 'outer' else bytes(archive)


class RelMaterializeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_di_read.DiscReadTest.setUpClass()

    def run_probe(self, scenario):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='rel-materialize-') as directory:
            root = Path(directory)
            if scenario != 'missing-disc':
                image = bytearray(test_di_read.DiscReadTest.image.build_image())
                archive = rel_archive(scenario)
                fst = test_di_read.DiscReadTest.image.FST_OFFSET
                strings = fst + 5 * 12
                image[strings + 1:strings + 9] = b'RELS.arc'
                struct.pack_into('>II', image, fst + 16, len(image), len(archive))
                image.extend(archive[:-8] if scenario == 'short' else archive)
                target = root / 'ux0:data/bluewake/GZLE01.iso'
                target.parent.mkdir(parents=True)
                target.write_bytes(image)
            result = subprocess.run([str(test_host.OUT / 'rel_materialize_probe'), scenario],
                                    cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result

    def test_raw_requested_rel_is_read_from_iso_and_mapped_without_claiming_link_success(self):
        self.run_probe('raw')

    def test_yaz0_member_is_decoded_before_materialization(self):
        self.run_probe('compressed')

    def test_yaz0_archive_wrapper_uses_the_same_checked_decoder(self):
        self.run_probe('outer')

    def test_game_module_name_matching_is_case_insensitive(self):
        self.run_probe('case')

    def test_missing_or_short_disc_cannot_materialize_a_module(self):
        for scenario in ('missing-disc', 'short', 'not-found'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_malformed_or_oversize_images_stop_before_guest_mapping(self):
        for scenario in ('bad-archive', 'bad-yaz0', 'bad-rel', 'wrong-module', 'metadata', 'catalog-word', 'oversize'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_unowned_scratch_alias_is_not_overwritten(self):
        self.run_probe('overlap')

    def test_invalid_module_object_stops_before_materialization(self):
        self.run_probe('bad-object')

    def test_trace_failure_rolls_back_the_unpublished_mapping(self):
        self.run_probe('trace')

    def test_exhausted_scratch_stops_without_mapping_or_guest_object_mutation(self):
        self.run_probe('full')

    def test_alias_allocation_failure_stops_without_publishing_a_partial_module(self):
        self.run_probe('allocation')


if __name__ == '__main__':
    unittest.main()
