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
        0x80007000: [0x80A40198, 0x7CBA03A6, 0x80A4019C, 0x7CBB03A6,
                     0x8064000C, 0x80840010, 0x4C000064],
        # This game-free caller computes the SDK result in registers without mutating the guarded image.
        0x80240EA0: [0x9421FFF0, 0x7C0802A6, 0x90010014, 0x93E1000C, 0x7C7F1B78,
                     branch(0x80240EB4, 0x80240744), 0x807F0010, 0x80830010, 0x7C832214,
                     0x88A30030, 0x54A51838, 0x7C84282E, 0x5484003C, 0x7C832214,
                     0x81830034, 0x7D8C2214,
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
    raw = bytearray(74960)
    for offset, value in ((0, 168), (0xC, 6), (0x10, 0x4C), (0x1C, 3), (0x20, 16),
                          (0x24, 0x2B0), (0x28, 0x2A0), (0x2C, 16), (0x38, 44), (0x3C, 88), (0x48, 0xE3B8)):
        struct.pack_into('>I', raw, offset, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    sections = [(1, 0xC1B100F4, 0xF4, 96), (2, 0xC1B1D414, 0x200, 36),
                (3, 0xC1B1016C, 0x230, 24), (4, 0xC1B1DAF0, 0x260, 12),
                (5, 0xC1B224D0, 0, 16)]
    for index, address, offset, size in sections:
        struct.pack_into('>II', raw, 0x4C + index * 8, offset | int(index < 4), size)
        if index < 4:
            raw[offset:offset + size] = struct.pack(f'>{len(code[address])}I', *code[address])
    canonical = bytearray(raw)
    struct.pack_into('>I', canonical, 0x260, 0xC1B1D414)
    struct.pack_into('>I', raw, 0x100, 0x3C600000)
    struct.pack_into('>I', raw, 0x104, 0x38630000)
    struct.pack_into('>I', raw, 0x108, 0x48000001)
    struct.pack_into('>I', raw, 0x10C, 0x48000001)
    struct.pack_into('>I', raw, 0x20C, 0x48000001)
    struct.pack_into('>IIII', raw, 0x2A0, 168, 0x2B0, 0, 0x310)
    entries = [(0, 202, 4, 0), (0, 1, 2, 0), (0, 202, 1, 0), (14, 6, 4, 0),
               (4, 4, 4, 0), (0, 202, 2, 0), (12, 10, 3, 0), (0, 203, 0, 0)]
    for at, entry in enumerate(entries):
        struct.pack_into('>HBBI', raw, 0x2B0 + at * 8, *entry)
    entries = [(0, 202, 1, 0), (20, 10, 0, 0x80241178), (4, 10, 0, 0x802410B4), (0, 203, 0, 0)]
    for at, entry in enumerate(entries):
        struct.pack_into('>HBBI', raw, 0x310 + at * 8, *entry)
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
              f'.section_info_offset=0x4Cu, .file_size={len(raw)}u, .sections=fixture_sections, .num_sections=5u}};',
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

    def run_probe(self, scenario, ranges=None, fix_size=None):
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
        if fix_size is not None:
            struct.pack_into('>I', raw, 0x48, fix_size)
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww74-disc-') as directory:
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
            command = [str(output / 'ship_prolog_probe'), scenario]
            if scenario.startswith(('scratch-', 'icbi-', 'sections-', 'publication-')):
                if ranges is None:
                    ranges = [(start, start + 0x100) for start in range(0x81820000, 0x81F80000, 0x10000)]
                    if scenario == 'scratch-exhausted':
                        ranges = [(0x81820000, 0x81F80000), (0x81F90000, 0x82000000)]
                    if scenario == 'scratch-tagged-prefix':
                        ranges.append((0xC1F90000, 0xC1F90040))
                occupancy = root / 'scratch-ranges.bin'
                occupancy.write_bytes(struct.pack('>I', len(ranges)) + b''.join(
                    struct.pack('>II', start, end) for start, end in ranges))
                command.append(str(occupancy))
                if fix_size is not None:
                    command.append(hex(fix_size))
            result = subprocess.run(command, cwd=root,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stderr

    def test_translated_caller_enters_prolog_walks_ctor_initializes_bss_and_returns(self):
        log = self.run_probe('success')
        for stage in ('entered', 'ctor-completed', 'caller-returned'):
            self.assertEqual(log.count(f'stage={stage}'), 1, log)
        self.assertIn('PASS caller-returned pc=80001004 bss-init-calls=2', log)

    def test_publication_preserves_the_exact_pristine_lifecycle_offsets(self):
        self.assertIn('PASS publication pristine offsets=00000000,0000002C,00000058',
                      self.run_probe('publication-offsets'))

    def test_publication_allows_one_sdk_rebase_to_the_approved_lifecycle_targets(self):
        self.assertIn('PASS publication SDK prolog canonical=81B100F4 gate=not-entered',
                      self.run_probe('publication-sdk-target'))

    def test_publication_relink_preserves_pristine_offsets_and_initialized_bss(self):
        self.assertIn('PASS publication pristine', self.run_probe('publication-relink'))

    def test_publication_relink_refuses_tampered_lifecycle_offsets(self):
        for entry in ('prolog', 'epilog', 'unresolved'):
            with self.subTest(entry=entry):
                self.assertIn('PASS publication tampered refused',
                              self.run_probe(f'publication-tampered-{entry}'))

    def test_publication_sdk_image_enters_with_the_real_loader_context(self):
        log = self.run_probe('publication-sdk-entry')
        self.assertIn('PASS SDK entry accepted pc=81F900F4 lr=80240EE8 r3=81F90000 owner=80AE0918', log)
        self.assertEqual(log.count('stage=entered'), 1, log)

    def test_publication_non_fixed_sdk_image_enters_with_the_real_loader_context(self):
        self.assertIn('PASS SDK entry accepted', self.run_probe('publication-sdk-entry-unfixed'))

    def test_publication_sdk_image_cannot_refresh_its_hash_after_entry(self):
        self.assertIn('PASS SDK continuation tampering refused',
                      self.run_probe('publication-sdk-entry-after-tamper'))

    def test_publication_complete_sdk_image_cannot_enter_a_constructor_without_verified_prolog_entry(self):
        self.assertIn('PASS SDK constructor without entry refused',
                      self.run_probe('publication-sdk-entry-ctor'))

    def test_publication_sdk_entry_refuses_wrong_caller_context(self):
        for register in ('pc', 'lr', 'r31', 'r3'):
            with self.subTest(register=register):
                log = self.run_probe(f'publication-sdk-entry-{register}')
                self.assertIn('PASS SDK entry refused', log)
                self.assertNotIn('stage=entered', log)

    def test_publication_sdk_entry_refuses_tampering_anywhere_in_the_image(self):
        for field in ('header', 'lifecycle', 'code', 'data', 'stream', 'bss', 'import'):
            with self.subTest(field=field):
                log = self.run_probe(f'publication-sdk-entry-tampered-{field}')
                self.assertIn('PASS SDK entry refused', log)
                self.assertNotIn('stage=entered', log)

    def test_publication_sdk_entry_refuses_partial_sdk_writebacks(self):
        self.assertIn('PASS SDK entry refused', self.run_probe('publication-sdk-entry-partial'))

    def test_pristine_prolog_entry_refuses_a_different_pc(self):
        self.assertIn('PASS SDK entry refused', self.run_probe('publication-entry-pc'))

    def test_publication_sdk_mutation_still_fails_the_constructor_whole_image_hash_gate(self):
        self.assertIn('gate=REL constructor raw image changed after validation',
                      self.run_probe('publication-sdk-ctor'))

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

    def test_icbi_accepts_the_start_of_published_linked_ship_executable_code(self):
        self.assertIn('PASS icbi accepted', self.run_probe('icbi-start'))

    def test_icbi_accepts_an_unaligned_target_inside_published_linked_ship_code(self):
        self.assertIn('PASS icbi accepted', self.run_probe('icbi-interior'))

    def test_icbi_accepts_the_last_byte_of_published_linked_ship_code(self):
        self.assertIn('PASS icbi accepted', self.run_probe('icbi-last'))

    def test_icbi_accepts_the_tagged_staging_alias_of_published_linked_ship_code(self):
        self.assertIn('PASS icbi accepted', self.run_probe('icbi-tagged'))

    def test_icbi_accepts_non_executable_data_in_the_published_linked_image(self):
        self.assertIn('PASS icbi accepted', self.run_probe('icbi-data'))

    def test_icbi_accepts_non_executable_data_after_exact_section_table_relocation(self):
        log = self.run_probe('icbi-data-relocated')
        self.assertIn('PASS icbi accepted', log)
        self.assertNotIn('[rel-section-mismatch]', log)

    def test_icbi_refuses_in_image_data_when_the_relocated_section_guard_fails(self):
        log = self.run_probe('icbi-data-tampered')
        self.assertIn('PASS icbi refused', log)
        self.assertIn('[rel-section-mismatch]', log)

    def test_icbi_accepts_the_first_byte_of_the_published_linked_image(self):
        self.assertIn('PASS icbi accepted', self.run_probe('icbi-image-start'))

    def test_icbi_accepts_the_last_byte_of_the_published_linked_image(self):
        self.assertIn('PASS icbi accepted', self.run_probe('icbi-image-last'))

    def test_icbi_accepts_the_tagged_alias_of_the_last_image_byte(self):
        self.assertIn('PASS icbi accepted', self.run_probe('icbi-image-tagged'))

    def test_icbi_refuses_targets_outside_the_published_ship_image(self):
        for boundary in ('below', 'end', 'above'):
            with self.subTest(boundary=boundary):
                self.assertIn('PASS icbi refused', self.run_probe(f'icbi-{boundary}'))

    def test_icbi_refuses_ship_data_after_its_guest_publication_is_cleared(self):
        self.assertIn('PASS icbi refused', self.run_probe('icbi-unpublished'))

    def test_icbi_refuses_ship_data_after_its_linked_state_is_cleared(self):
        self.assertIn('PASS icbi refused', self.run_probe('icbi-unlinked'))

    def test_icbi_refuses_ship_data_after_its_unlinked_image_is_collected(self):
        self.assertIn('PASS icbi refused', self.run_probe('icbi-collected'))

    def test_icbi_refuses_ship_fallback_code_without_its_certification_contract(self):
        self.assertIn('PASS icbi refused', self.run_probe('icbi-no-contract'))

    def test_icbi_unknown_target_keeps_the_existing_refusal_text_and_context(self):
        self.assertIn('PASS icbi refused target=90000000', self.run_probe('icbi-unknown'))

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

    def test_completed_return_survives_supported_interrupt_handoff_and_restoration(self):
        for interrupt in ('external', 'decrementer'):
            with self.subTest(interrupt=interrupt):
                log = self.run_probe(f'interrupt-{interrupt}')
                self.assertIn('stage=interrupt-restored return=80240EE8', log)
                self.assertIn('PASS caller-returned pc=80001004 bss-init-calls=2', log)
                self.assertEqual(log.count('stage=caller-returned'), 1, log)

    def test_completed_return_survives_repeated_interrupts_before_the_caller_advances(self):
        log = self.run_probe('interrupt-repeated')
        self.assertEqual(log.count('stage=interrupt-restored return=80240EE8'), 2, log)
        self.assertIn('PASS caller-returned pc=80001004 bss-init-calls=2', log)

    def test_interrupted_return_revalidates_the_restored_caller_context(self):
        for register in ('sp', 'r31', 'lr', 'r2', 'r13'):
            with self.subTest(register=register):
                self.assertIn('preserve its translated caller', self.run_probe(f'interrupt-invalid-{register}'))

    def test_completed_return_admission_expires_after_the_caller_continuation_advances(self):
        log = self.run_probe('interrupt-expiry')
        self.assertIn('stage=interrupt-restored return=80240EE8', log)
        self.assertIn('PASS admission-expired pc=80001004', log)
        self.assertIn('completed translated call', log)

    def test_diverted_rfi_restoration_rejects_and_clears_the_completed_return_contract(self):
        log = self.run_probe('interrupt-diverted')
        self.assertIn('stage=restoration-dispatched target=80240EEC', log)
        self.assertIn('exception restoration changed the pending caller PC', log)
        self.assertIn('PASS suspended-admission-cleared', log)

    def test_rfi_cannot_reenter_the_prolog_with_the_suspended_owner_and_stack(self):
        log = self.run_probe('interrupt-reentry-rfi')
        self.assertIn('stage=restoration-dispatched target=C1B100F4', log)
        self.assertIn('exception restoration changed the pending caller PC', log)
        self.assertIn('PASS suspended-admission-cleared', log)

    def test_prolog_entry_is_rejected_while_its_completed_return_is_still_suspended(self):
        log = self.run_probe('interrupt-reentry-pending')
        self.assertIn('prolog entry is forbidden while its completed return is suspended', log)
        self.assertIn('PASS suspended-admission-cleared', log)

    def test_suspended_return_cannot_resume_without_executing_the_restoration_rfi(self):
        log = self.run_probe('interrupt-return-pending')
        self.assertIn('return requires its verified exception restoration', log)
        self.assertIn('PASS suspended-admission-cleared', log)

    def test_ship_materialization_uses_bounded_fallback_when_legacy_scratch_is_fragmented(self):
        log = self.run_probe('scratch-fragmented')
        self.assertIn('PASS staging guest=81F90000 bytes=74960 capacity=74976 controller=preserved', log)

    def test_ship_fallback_skips_a_live_staging_reservation(self):
        self.assertIn('PASS staging guest=81FA0020', self.run_probe('scratch-live-prefix'))

    def test_ship_fix_size_word_is_preserved_in_the_staged_raw_image(self):
        raw = (test_host.OUT / 'ship.rel').read_bytes()
        fix_size = struct.unpack_from('>I', raw, 0x48)[0]
        self.assertEqual(fix_size, 0xE3B8)
        self.assertIn(f'PASS fix-size raw={fix_size:08X} staged={fix_size:08X}',
                      self.run_probe('scratch-fragmented'))

    def test_ship_fallback_rejects_a_fix_area_reaching_or_crossing_the_game_ceiling(self):
        for fix_size in (0x20000, 0x20001, 0xFFFFFFFF):
            with self.subTest(fix_size=fix_size):
                log = self.run_probe('scratch-fix-rejected',
                                     ranges=[(0x81820000, 0x81F80000), (0x81F90000, 0x81FE0000)],
                                     fix_size=fix_size)
                self.assertIn('ship REL staging fix area reaches the module-region ceiling', log)
                self.assertIn('PASS rejected-before-publication', log)
                self.assertNotIn('[rel-materialized]', log)

    def test_ship_fallback_accepts_a_fix_area_one_byte_below_the_game_ceiling(self):
        log = self.run_probe('scratch-fix-safe',
                             ranges=[(0x81820000, 0x81F80000), (0x81F90000, 0x81FE0000)],
                             fix_size=0x1FFFF)
        self.assertIn('PASS staging guest=81FE0000', log)
        self.assertIn('PASS fix-size raw=0001FFFF staged=0001FFFF', log)

    def test_ship_materialization_still_rejects_when_both_staging_windows_are_exhausted(self):
        self.assertIn('REL scratch has no bounded unoccupied range', self.run_probe('scratch-exhausted'))

    def test_ship_fallback_does_not_overwrite_an_unowned_tagged_alias(self):
        self.assertIn('REL scratch overlaps an existing alias owner', self.run_probe('scratch-alias'))

    def test_ship_fallback_reserves_folded_tagged_canonical_ranges(self):
        self.assertIn('PASS staging guest=81F90040', self.run_probe('scratch-tagged-prefix'))

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
