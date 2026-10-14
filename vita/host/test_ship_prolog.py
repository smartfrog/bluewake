from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest

import test_di_read
import test_host

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import ship_contract


def branch(source, target, link=True):
    return 0x48000000 | ((target - source) & 0x03FFFFFC) | int(link)


def instructions():
    return {
        0x80240EC0: [0x9421FFF0, 0x7C0802A6, 0x90010014, 0x93E1000C, 0x7C7F1B78,
                     branch(0x80240ED4, 0x80240744), 0x819F0010, 0x818C0034,
                     0x7D8903A6, 0x4E800421, 0x83E1000C, 0x80010014, 0x7C0803A6,
                     0x38210010, 0x4E800020],
        0xC0241178: [0x9421FFF0, 0x7C0802A6, 0x90010014, 0x93E1000C, 0x7C7F1B78,
                     branch(0xC024118C, 0xC0241198, False), 0x60000000, 0x60000000,
                     0x819F0000, 0x2C0C0000, 0x41820014, 0x7D8903A6, 0x4E800421,
                     0x3BFF0004, branch(0xC02411B0, 0xC0241198, False),
                     0x83E1000C, 0x80010014, 0x7C0803A6, 0x38210010, 0x4E800020],
        0xC02410B4: [0x4E800020],
        0xC1B100F4: [0x9421FFF0, 0x7C0802A6, 0x90010014, 0x3C60C1B2, 0x3863DAF0,
                     branch(0xC1B10108, 0xC0241178), branch(0xC1B1010C, 0xC02410B4),
                     0x80010014, 0x7C0803A6, 0x38210010, 0x4E800020] + [0x60000000] * 13,
        0xC1B1D414: [0x9421FFF0, 0x7C0802A6, 0x90010014,
                     branch(0xC1B1D420, 0xC1B1016C), branch(0xC1B1D424, 0xC1B1016C),
                     0x80010014, 0x7C0803A6, 0x38210010, 0x4E800020],
        0xC1B1016C: [0x3C80C1B2, 0x388424D0, 0x80A40000, 0x38A50001, 0x90A40000, 0x4E800020],
    }


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def fixture(output):
    code = instructions()
    raw = bytearray(0x280)
    for offset, value in ((0, 168), (0xC, 6), (0x10, 0x4C), (0x1C, 3), (0x20, 16),
                          (0x28, 0x1A0), (0x2C, 16), (0x38, 44), (0x3C, 88)):
        struct.pack_into('>I', raw, offset, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    sections = [(1, 0xC1B100F4, 0x80, 96), (2, 0xC1B1D414, 0x100, 36),
                (3, 0xC1B1016C, 0x130, 24), (4, 0xC1B1DAF0, 0x160, 12),
                (5, 0xC1B224D0, 0, 16)]
    for index, address, offset, size in sections:
        struct.pack_into('>II', raw, 0x4C + index * 8, offset | int(index < 4), size)
        if index < 4:
            raw[offset:offset + size] = struct.pack(f'>{len(code[address])}I', *code[address])
    canonical = bytearray(raw)
    struct.pack_into('>I', canonical, 0x160, 0xC1B1D414)
    struct.pack_into('>I', raw, 0x8C, 0x3C600000)
    struct.pack_into('>I', raw, 0x90, 0x38630000)
    struct.pack_into('>I', raw, 0x94, 0x48000001)
    struct.pack_into('>I', raw, 0x98, 0x48000001)
    struct.pack_into('>I', raw, 0x10C, 0x48000001)
    struct.pack_into('>IIII', raw, 0x1A0, 168, 0x1B0, 0, 0x210)
    entries = [(0, 202, 4, 0), (0, 1, 2, 0), (0, 202, 1, 0), (14, 6, 4, 0),
               (4, 4, 4, 0), (0, 202, 2, 0), (12, 10, 3, 0), (0, 203, 0, 0)]
    for at, entry in enumerate(entries):
        struct.pack_into('>HBBI', raw, 0x1B0 + at * 8, *entry)
    entries = [(0, 202, 1, 0), (20, 10, 0, 0x80241178), (4, 10, 0, 0x802410B4), (0, 203, 0, 0)]
    for at, entry in enumerate(entries):
        struct.pack_into('>HBBI', raw, 0x210 + at * 8, *entry)
    (output / 'ship.rel').write_bytes(raw)
    emitter = bytearray()
    for address, words in code.items():
        emitter.extend(struct.pack('>II', address, len(words)))
        emitter.extend(struct.pack(f'>{len(words)}I', *words))
    (output / 'ship.instructions').write_bytes(emitter)
    lines = ['static const StaticRecompRange fixture_ranges[] = {']
    lines.append('{0x80240744u, 0x80240748u},')
    lines.extend(f'{{0x{address & ~0x40000000 if address < 0xC1000000 else address:08X}u, '
                 f'0x{(address & ~0x40000000 if address < 0xC1000000 else address) + len(words) * 4:08X}u}},'
                 for address, words in code.items())
    lines += ['};', 'static const StaticRecompRange fixture_chunks[] = {']
    executable = sorted(sections[:3], key=lambda section: section[1])
    lines.extend(f'{{0x{address:08X}u, 0x{address + size:08X}u}},' for _, address, _, size in executable)
    lines += ['};', 'static const u64 fixture_hashes[] = {']
    lines.extend(f'0x{fnv(canonical[offset:offset + size]):016X}ull,' for _, _, offset, size in executable)
    lines += ['};', 'static const StaticRecompRelSection fixture_sections[] = {']
    lines.extend(f'{{168u, {index}u, 0x{address:08X}u, {size}u}},' for index, address, _, size in sections)
    lines += ['};', 'static const StaticRecompRelModule fixture_rel = {.module_id=168u, .section_count=6u,',
              '.section_info_offset=0x4Cu, .file_size=0x280u, .sections=fixture_sections, .num_sections=5u};',
              'static const u8 fixture_ctors[] = {0xC1, 0xB1, 0xD4, 0x14, 0, 0, 0, 0, 0, 0, 0, 0};',
              'static const BlueWakeRelData fixture_data[] = {',
              '{168u, 4u, 0xC1B1DAF0u, 12u, fixture_ctors}, {168u, 5u, 0xC1B224D0u, 16u, NULL}};']
    (output / 'ship_fixture.inc').write_text('\n'.join(lines) + '\n')
    section_map = [{'index': index, 'offset': offset, 'size': size, 'linked_start': address,
                    'executable': index < 4, 'bytes': canonical[offset:offset + size] if offset else None}
                   for index, address, offset, size in sections]
    sidecar = b'SHIP168\0' + struct.pack('>I', 3)
    for section in section_map[:3]:
        sidecar += struct.pack('>IIII', section['index'], section['offset'], section['linked_start'], section['size'])
        sidecar += section['bytes']
    canonical_sections = ship_contract.read_canonical(sidecar, section_map)
    (output / 'ship_contract.inc').write_text(ship_contract.emit_contract(bytes([1, 2, 3, 4]), raw,
        section_map, canonical_sections, hashlib.sha256(emitter + raw + sidecar).hexdigest()))
    return raw


class ShipPrologTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_di_read.DiscReadTest.setUpClass()

    def run_probe(self, scenario):
        output = test_host.OUT
        raw = bytearray((output / 'ship.rel').read_bytes())
        if scenario == 'bad-rel':
            raw[0x1C7] ^= 1
        if scenario.startswith('uncertified-'):
            offset = {'prolog': 0x34, 'epilog': 0x38, 'unresolved': 0x3C}[scenario.split('-')[1]]
            struct.pack_into('>I', raw, offset, struct.unpack_from('>I', raw, offset)[0] + 4)
        if scenario == 'other-actor':
            struct.pack_into('>I', raw, 0, 169)
        if scenario == 'lifecycle-data':
            struct.pack_into('>I', raw, 0x54, 0x80)
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww63-disc-') as directory:
            root = Path(directory)
            archive = bytearray(0x80)
            archive[:4] = b'RARC'
            struct.pack_into('>I', archive, 0xC, 0x60)
            struct.pack_into('>II', archive, 0x28, 1, 0x20)
            name = b'd_a_test.rel\0' if scenario == 'other-actor' else b'd_a_ship.rel\0'
            struct.pack_into('>II', archive, 0x30, len(name), 0x34)
            struct.pack_into('>III', archive, 0x44, 0x01000000, 0, len(raw))
            archive[0x54:0x54 + len(name)] = name
            archive.extend(raw)
            struct.pack_into('>I', archive, 4, len(archive))
            image = bytearray(test_di_read.DiscReadTest.image.build_image())
            fst = test_di_read.DiscReadTest.image.FST_OFFSET
            image[fst + 5 * 12 + 1:fst + 5 * 12 + 9] = b'RELS.arc'
            struct.pack_into('>II', image, fst + 16, len(image), len(archive))
            image.extend(archive)
            target = root / 'ux0:data/bluewake/GZLE01.iso'
            target.parent.mkdir(parents=True)
            target.write_bytes(image)
            (root / 'synthetic.dol').write_bytes(bytes([1, 2, 3, 5 if scenario == 'bad-dol' else 4]))
            result = subprocess.run([str(output / 'ship_prolog_probe'), scenario], cwd=root,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stderr

    def test_translated_caller_enters_prolog_walks_ctor_initializes_bss_and_returns(self):
        log = self.run_probe('success')
        for stage in ('entered', 'ctor-completed', 'caller-returned'):
            self.assertEqual(log.count(f'stage={stage}'), 1, log)
        self.assertIn('PASS caller-returned pc=80001004 bss-init-calls=2', log)

    def test_default_off_rejects_ship_lifecycle_before_publication(self):
        self.assertIn('unsupported lifecycle contract', self.run_probe('default-off'))

    def test_missing_contract_rejects_ship_lifecycle_before_publication(self):
        log = self.run_probe('missing-contract')
        self.assertIn('complete generated exact build contract', log)
        self.assertIn('unsupported lifecycle contract', log)

    def test_full_raw_dol_mismatch_cannot_enable_ship_contract(self):
        self.assertIn('full raw DOL identity mismatch', self.run_probe('bad-dol'))

    def test_full_raw_rel_import_stream_mismatch_rejects_before_publication(self):
        self.assertIn('full raw REL identity mismatch', self.run_probe('bad-rel'))

    def test_stale_compiled_code_identity_rejects_before_publication(self):
        self.assertIn('compiled chunk hashes', self.run_probe('stale-code'))

    def test_other_actor_lifecycle_is_rejected_even_with_ship_opt_in(self):
        self.assertIn('unsupported lifecycle contract', self.run_probe('other-actor'))

    def test_uncertified_lifecycle_addresses_are_rejected_even_when_raw_identity_matches(self):
        for entry in ('prolog', 'epilog', 'unresolved'):
            with self.subTest(entry=entry):
                self.assertIn('exact approved lifecycle addresses', self.run_probe(f'uncertified-{entry}'))

    def test_epilog_execution_remains_gated(self):
        for alias in ('linked', 'scratch'):
            with self.subTest(alias=alias):
                self.assertIn('no supported lifecycle contract', self.run_probe(f'epilog-{alias}'))

    def test_unresolved_execution_remains_gated(self):
        for alias in ('linked', 'scratch'):
            with self.subTest(alias=alias):
                self.assertIn('no supported lifecycle contract', self.run_probe(f'unresolved-{alias}'))

    def test_explicit_ship_unlink_remains_gated(self):
        self.assertIn('unsupported epilog and destruction contract', self.run_probe('unlink'))

    def test_native_actor_unlink_remains_gated(self):
        self.assertIn('native REL unlink', self.run_probe('native-unlink'))

    def test_changed_constructor_table_cannot_dispatch_a_constructor(self):
        self.assertIn('constructor target requires translated code owned by its caller', self.run_probe('ctor-mutated'))

    def test_changed_raw_image_cannot_continue_the_prolog(self):
        self.assertIn('lost its exact live owner', self.run_probe('raw-mutated'))

    def test_prolog_rejects_an_unapproved_caller_context(self):
        for register in ('lr', 'r31'):
            with self.subTest(register=register):
                self.assertIn('exact translated dynamic-link caller', self.run_probe(f'entry-{register}'))

    def test_prolog_return_requires_a_completed_constructor_walk(self):
        self.assertIn('preserve its translated caller', self.run_probe('return-without-ctor'))

    def test_prolog_return_preserves_the_inherited_register_context(self):
        for register in ('sp', 'r31', 'lr', 'r2', 'r13'):
            with self.subTest(register=register):
                self.assertIn('preserve its translated caller', self.run_probe(f'return-{register}'))

    def test_relink_preserves_initialized_bss_and_published_lifecycle_entries(self):
        self.assertIn('PASS relink-preserved', self.run_probe('relink'))

    def test_dynamic_caller_resume_requires_an_actual_prolog_return(self):
        self.assertIn('completed translated call', self.run_probe('resume-without-prolog'))

    def test_lifecycle_entries_require_executable_section_ownership(self):
        self.assertIn('lifecycle entries require owned translated executable sections', self.run_probe('lifecycle-data'))

    def emit_program(self, data):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='ship-emit-') as directory:
            source = Path(directory) / 'instructions.bin'
            source.write_bytes(data)
            return subprocess.run([str(test_host.OUT / 'vector-generator/ww_ship_fixture_emit'), str(source)],
                                  capture_output=True, text=True)

    def test_fixture_emitter_rejects_a_truncated_program_header(self):
        self.assertNotEqual(self.emit_program(b'\x80\x24\x0E').returncode, 0)

    def test_fixture_emitter_rejects_an_empty_program(self):
        self.assertNotEqual(self.emit_program(b'').returncode, 0)


if __name__ == '__main__':
    unittest.main()
