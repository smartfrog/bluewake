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


def raw_rel(alignment):
    content = bytearray(256)
    for offset, value in ((0, 1), (0xC, 2), (0x10, 0x4C), (0x1C, 3),
                          (0x24, 0xA8), (0x28, 0xA0), (0x2C, 8)):
        struct.pack_into('>I', content, offset, value)
    content[0x30] = 1
    struct.pack_into('>II', content, 0x54, 0x81, 0x20)
    struct.pack_into('>II', content, 0xA0, 0, 0xA8)
    content[0xAA] = 203
    content[0x43] = alignment
    for index in range(0x80, 0xA0):
        content[index] = (index * 7 + 3) & 255
    return bytes(content)


def rarc(members):
    archive = bytearray(0x100)
    archive[:4] = b'RARC'
    struct.pack_into('>I', archive, 0xC, 0xE0)
    struct.pack_into('>II', archive, 0x28, len(members), 0x20)
    names = bytearray()
    for index, (name, content) in enumerate(members):
        struct.pack_into('>III', archive, 0x44 + index * 20,
                         0x01000000 | len(names), len(archive) - 0x100, len(content))
        names.extend(name + b'\0')
        archive.extend(content)
    struct.pack_into('>II', archive, 0x30, len(names), 0x70)
    archive[0x90:0x90 + len(names)] = names
    struct.pack_into('>I', archive, 4, len(archive))
    return bytes(archive)


def base_image(archive, stored_length=None):
    image = bytearray(test_di_read.DiscReadTest.image.build_image())
    fst = test_di_read.DiscReadTest.image.FST_OFFSET
    strings = fst + 5 * 12
    image[strings + 1:strings + 9] = b'RELS.arc'
    struct.pack_into('>II', image, fst + 16, len(image),
                     len(archive) if stored_length is None else stored_length)
    image.extend(archive)
    return image


def add_loose(image, member, offset=None, length=None):
    image = bytearray(image)
    fst = test_di_read.DiscReadTest.image.FST_OFFSET
    base_strings = len(test_di_read.DiscReadTest.image.STRINGS)
    old_strings = bytes(image[fst + 5 * 12:fst + 5 * 12 + base_strings])
    new_strings = old_strings + b'rels\0d_a_title.rel\0'
    struct.pack_into('>I', image, fst + 8, 7)
    struct.pack_into('>III', image, fst + 5 * 12, 0x01000000 | base_strings, 0, 7)
    struct.pack_into('>III', image, fst + 6 * 12,
                     base_strings + len(b'rels\0'),
                     len(image) if offset is None else offset,
                     len(member) if length is None else length)
    image.extend(member)
    strings = fst + 7 * 12
    image[strings:strings + len(new_strings)] = new_strings
    struct.pack_into('>I', image, 0x428, 7 * 12 + len(new_strings))
    return image


class RelProviderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_di_read.DiscReadTest.setUpClass()

    def run_probe(self, scenario, argument, image, expected=None):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='rel-provider-') as directory:
            root = Path(directory)
            target = root / 'ux0:data/bluewake/GZLE01.iso'
            target.parent.mkdir(parents=True)
            target.write_bytes(image)
            if expected is not None:
                (root / 'expected.bin').write_bytes(expected)
            result = subprocess.run([str(test_host.OUT / 'rel_provider_probe'), scenario, argument],
                                    cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result

    def serve(self, image, expected):
        self.assertEqual(expected[0x43], 8, 'fixture must keep the original alignment byte')
        self.run_probe('serve', 'expected.bin', image, expected)

    def reject(self, image, message):
        result = self.run_probe('error', 'd_a_title', image)
        self.assertIn(message, result.stderr)

    def test_archive_member_is_served_without_a_loose_file(self):
        title = raw_rel(8)
        image = base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4)),
                                 (b'd_a_title.rel', title)]))
        self.serve(image, title)

    def test_archive_member_wins_over_a_loose_file(self):
        title = raw_rel(8)
        image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4)),
                                       (b'd_a_title.rel', title)])), raw_rel(4))
        self.serve(image, title)

    def test_loose_raw_member_serves_when_absent_from_the_archive(self):
        title = raw_rel(8)
        image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])), title)
        self.serve(image, title)

    def test_loose_yaz0_member_serves_when_absent_from_the_archive(self):
        title = raw_rel(8)
        image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])),
                          yaz0_literals(title))
        self.serve(image, title)

    def test_loose_lookup_is_case_insensitive(self):
        title = raw_rel(8)
        image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])), title)
        self.run_probe('serve-case', 'expected.bin', image, title)

    def test_absent_module_fails_closed(self):
        image = base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))]))
        self.reject(image, 'requested REL is absent from the owned ISO archive and loose rels/')

    def test_malformed_loose_member_fails_closed(self):
        for member, message in (
                (bytes(16), 'REL loose member is invalid, too large or could not be allocated'),
                (b'Yaz0' + struct.pack('>I', 256) + bytes(8),
                 'REL loose member Yaz0 wrapper is invalid or exceeds the bound'),
                (b'Yaz0' + struct.pack('>I', 0x100001) + bytes(8),
                 'REL loose member Yaz0 wrapper is invalid or exceeds the bound')):
            with self.subTest(member=member[:16]):
                image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])), member)
                self.reject(image, message)

    def test_duplicate_archive_members_stay_ambiguous_without_fallback(self):
        title = raw_rel(8)
        image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4)),
                                       (b'd_a_title.rel', title),
                                       (b'D_A_TITLE.rel', raw_rel(4))])), title)
        self.reject(image, 'REL archive contains an ambiguous module name')

    def test_corrupt_archive_does_not_fall_back_to_a_loose_file(self):
        archive = bytearray(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))]))
        struct.pack_into('>I', archive, 0x2C, 0xFFFFFFF0)
        image = add_loose(base_image(bytes(archive)), raw_rel(8))
        self.reject(image, 'REL archive RARC tables are invalid')

    def test_truncated_archive_read_does_not_fall_back_to_a_loose_file(self):
        archive = rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])
        image = add_loose(base_image(archive, stored_length=len(archive) + 0x1000), raw_rel(8))
        self.reject(image, 'REL archive ISO read failed or was short')

    def test_missing_archive_does_not_fall_back_to_a_loose_file(self):
        image = add_loose(test_di_read.DiscReadTest.image.build_image(), raw_rel(8))
        self.reject(image, 'REL provider requires a bounded RELS.arc in the owned disc FST')

    def test_unsafe_module_names_are_rejected_before_the_fallback(self):
        image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])), raw_rel(8))
        for name in ('', '..', '../d_a_title', 'a/b', 'a.b', 'a b', 'a-b', 'a' * 96):
            with self.subTest(name=name):
                result = self.run_probe('unsafe', name, image)
                self.assertIn('REL loose module name is not a bounded identifier', result.stderr)

    def test_truncated_loose_read_fails_closed(self):
        title = raw_rel(8)
        image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])), title,
                          length=len(title) + 0x100)
        self.reject(image, 'REL loose member ISO read failed or was short')

    def test_invalid_loose_fst_range_fails_before_read(self):
        for offset, length in ((None, 0), (0x7FFFF000, 0x2000), (None, 0x2000000)):
            with self.subTest(offset=offset, length=length):
                image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])),
                                  raw_rel(8), offset=offset, length=length)
                self.reject(image, 'REL loose member ISO range is invalid or unbounded')

    def test_loose_member_allocation_failure_leaves_no_image(self):
        title = raw_rel(8)
        image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])), title)
        result = self.run_probe('allocation', str(len(title)), image)
        self.assertIn('REL loose member allocation failed', result.stderr)

    def test_archive_allocation_failure_leaves_no_image(self):
        archive = rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])
        image = add_loose(base_image(archive), raw_rel(8))
        result = self.run_probe('allocation', str(len(archive)), image)
        self.assertIn('REL archive allocation failed', result.stderr)

    def test_archive_close_failure_leaves_no_image(self):
        archive = rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])
        image = add_loose(base_image(archive), raw_rel(8))
        result = self.run_probe('closefail', '1', image)
        self.assertIn('REL archive ISO stream close failed', result.stderr)

    def test_loose_close_failure_leaves_no_image(self):
        title = raw_rel(8)
        image = add_loose(base_image(rarc([(b'f_pc_profile_lst.rel', raw_rel(4))])), title)
        result = self.run_probe('closefail', '2', image)
        self.assertIn('REL loose member ISO stream close failed', result.stderr)


if __name__ == '__main__':
    unittest.main()
