from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

import test_host
import test_di_read
import test_rel_materialize


def fixture(output):
    raw = bytearray(384)
    for offset, value in ((0, 400), (0xC, 8), (0x10, 0x4C), (0x1C, 3),
                          (0x24, 0xE0), (0x28, 0xD0), (0x2C, 16),
                          (0x38, 4), (0x3C, 8), (0x40, 8), (0x48, 0xC4)):
        struct.pack_into('>I', raw, offset, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    struct.pack_into('>II', raw, 0x54, 0xA1, 32)
    struct.pack_into('>II', raw, 0x5C, 0xC0, 4)
    struct.pack_into('>8I', raw, 0xA0, *([0x60000000] * 8))
    struct.pack_into('>I', raw, 0xAC, 0x48000001)
    struct.pack_into('>4I', raw, 0xD0, 400, 0xE0, 0, 0x100)
    for offset, entry in ((0xE0, (0, 202, 1, 0)), (0xE8, (12, 10, 1, 16)),
                          (0xF0, (0, 203, 0, 0)), (0x100, (0, 202, 1, 0)),
                          (0x108, (18, 6, 0, 0x80001000)), (0x110, (4, 4, 0, 0x80001000)),
                          (0x118, (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, offset, *entry)
    canonical = bytearray(raw[0xA0:0xC0])
    struct.pack_into('>I', canonical, 12, 0x48000005)
    struct.pack_into('>H', canonical, 18, 0x8000)
    struct.pack_into('>H', canonical, 22, 0x1000)
    from test_ship_prolog import fnv
    lines = [f'static const u8 title_raw[] = {{{",".join(str(byte) for byte in raw)}}};',
             f'static const u8 title_code[] = {{{",".join(str(byte) for byte in canonical)}}};',
             'static const u8 title_ctors[] = {0,0,0,0};',
             'static const StaticRecompRelSection title_sections[] = {',
             '{400u,0u,0u,0u},{400u,1u,0xC1E000DCu,32u},{400u,2u,0xC1E01F28u,4u},',
             *[f'{{400u,{index}u,0u,0u}},' for index in range(3, 8)], '};',
             f'static const u64 title_hash = 0x{fnv(canonical):016X}ull;',
             'static const BlueWakeRelData title_canonical[] = {',
             '{400u,1u,0xC1E000DCu,32u,title_code},{400u,2u,0xC1E01F28u,4u,title_ctors}};',
             'static const BlueWakeRelLifecycleV2 title_lifecycle = {400u,0xC1E000DCu,0xC1E000E0u,0xC1E000E4u};']
    (output / 'rel_contract_fixture.inc').write_text('\n'.join(lines) + '\n')
    (output / 'title.rel').write_bytes(raw)
    (output / 'synthetic.dol').write_bytes(bytes([1, 2, 3, 4]))


def archive(scenario):
    profile = bytearray(test_rel_materialize.raw_rel())
    struct.pack_into('>I', profile, 0xC, 5)
    struct.pack_into('>II', profile, 0x6C, 0xB0, 16)
    title = bytearray((test_host.OUT / 'title.rel').read_bytes())
    if scenario == 'raw-identity':
        title[-1] ^= 1
    if scenario == 'catalog-id':
        struct.pack_into('>I', title, 0, 401)
        struct.pack_into('>I', title, 0xD0, 401)
    members = [(b'f_pc_profile_lst.rel', profile),
               (b'd_a_ship.rel', (test_host.OUT / 'ship.rel').read_bytes()),
               (b'd_a_title.rel', title)]
    content = bytearray(0x100)
    content[:4] = b'RARC'
    struct.pack_into('>I', content, 0xC, 0xE0)
    struct.pack_into('>II', content, 0x28, len(members), 0x20)
    names = bytearray()
    for index, (name, data) in enumerate(members):
        struct.pack_into('>III', content, 0x44 + index * 20,
                         0x01000000 | len(names), len(content) - 0x100, len(data))
        names.extend(name + b'\0')
        content.extend(data)
    struct.pack_into('>II', content, 0x30, len(names), 0x70)
    content[0x90:0x90 + len(names)] = names
    struct.pack_into('>I', content, 4, len(content))
    return content


class RelContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_di_read.DiscReadTest.setUpClass()

    def run_probe(self, scenario):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='rel-contract-') as directory:
            root = Path(directory)
            (root / 'synthetic.dol').write_bytes(bytes([1, 2, 3, 4]))
            image = bytearray(test_di_read.DiscReadTest.image.build_image())
            content = archive(scenario)
            fst = test_di_read.DiscReadTest.image.FST_OFFSET
            image[fst + 5 * 12 + 1:fst + 5 * 12 + 9] = b'RELS.arc'
            struct.pack_into('>II', image, fst + 16, len(image), len(content))
            image.extend(content)
            disc = root / 'ux0:data/bluewake/GZLE01.iso'
            disc.parent.mkdir(parents=True)
            disc.write_bytes(image)
            result = subprocess.run([str(test_host.OUT / 'rel_contract_probe'), scenario],
                                    cwd=root, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_legacy_adapter_refuses_an_unterminated_certified_constructor_table(self):
        self.run_probe('legacy-ctor-terminator')

    def test_selected_images_have_independent_live_certificates_after_sdk_append(self):
        self.run_probe('selected-append')

    def test_registration_does_not_grant_lifecycle_admission(self):
        self.run_probe('registration-only')

    def test_selected_admission_cannot_execute_a_title_initialization(self):
        for entry in ('prolog', 'epilog', 'unresolved'):
            with self.subTest(entry=entry):
                self.run_probe('execution-closed-' + entry)

    def test_title_append_updates_ship_hash_without_accepting_unapproved_writes(self):
        self.run_probe('append-tamper')

    def test_malformed_certificate_sets_fail_before_registration(self):
        for field in ('version', 'count-zero', 'count-limit', 'null', 'identity', 'duplicate', 'unknown-id',
                      'canonical-count', 'canonical-duplicate', 'canonical-code', 'export-duplicate',
                      'lifecycle-id', 'lifecycle-selector', 'lifecycle-offset', 'ctor-terminator',
                      'imports', 'relocation', 'stream-end', 'zero-bss', 'zero-bss-selector',
                      'empty-flags', 'empty-size', 'alignment', 'rel-version', 'fabricated-export'):
            with self.subTest(field=field):
                self.run_probe('schema-' + field)

    def test_invalid_append_keeps_previous_authority_and_fails_pending_state_closed(self):
        for field in ('head', 'tail', 'previous', 'next', 'cycle', 'skipped', 'candidate-owner',
                      'neighbor-owner', 'profile-global', 'neighbor-write', 'name', 'bss',
                      'import-size', 'empty-flags', 'empty-size', 'zero-bss-selector', 'zero-bss',
                      'lifecycle-selector', 'ctor-terminator', 'partial'):
            with self.subTest(field=field):
                self.run_probe('rollback-' + field)

    def test_original_raw_identity_is_checked_before_materialization(self):
        self.run_probe('raw-identity')

    def test_translated_but_uncertified_module_is_refused_before_publication(self):
        self.run_probe('uncertified')

    def test_default_off_refuses_before_publication(self):
        self.run_probe('default-off')

    def test_ship_only_opt_in_does_not_admit_registered_title_metadata(self):
        self.run_probe('ship-only')

    def test_certificate_ids_follow_the_catalog_not_a_title_allowlist(self):
        self.run_probe('catalog-id')

    def test_a_pending_append_cannot_be_replaced(self):
        self.run_probe('pending-replacement')

    def test_an_owner_cannot_reassociate_its_live_image_with_another_certificate(self):
        self.run_probe('owner-wrong-image')

    def test_certification_refuses_a_copied_image_record_with_the_same_owner(self):
        self.run_probe('pending-wrong-image')

    def test_sdk_entry_freezes_inputs_without_changing_cpu_state(self):
        self.run_probe('entry-observation')

    def test_internal_icbi_does_not_certify_the_pre_trim_image(self):
        self.run_probe('icbi-before-trim')

    def test_fixed_mode_does_not_choose_an_unfixed_matching_hash(self):
        self.run_probe('fixed-mismatch')

    def test_profile_residency_survives_its_controller_owner_expiring(self):
        self.run_probe('profile-owner-expired')

    def test_an_empty_sdk_list_cannot_skip_the_live_resident_profile(self):
        self.run_probe('seed-missing-profile-list')

    def test_an_initially_empty_list_sets_head_only_for_the_first_append(self):
        self.run_probe('empty-list')


if __name__ == '__main__':
    unittest.main()
