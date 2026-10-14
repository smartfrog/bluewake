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


def actor_rel(module_id):
    actor = bytearray(raw_rel())
    struct.pack_into('>I', actor, 0, module_id)
    struct.pack_into('>I', actor, 0xC, 4)
    struct.pack_into('>I', actor, 0x20, 16)
    struct.pack_into('>II', actor, 0x5C, 0, 16)
    struct.pack_into('>II', actor, 0x64, 0xB0, 16)
    actor[0x33] = 2
    actor[0x30:0x33] = bytes(3)
    struct.pack_into('>II', actor, 0xA0, 0, 0xC0)
    struct.pack_into('>HBBI', actor, 0xC0, 0, 202, 3, 0)
    struct.pack_into('>HBBI', actor, 0xC8, 0, 1, 0, 0x80001000)
    struct.pack_into('>HBBI', actor, 0xD0, 0, 203, 0, 0)
    return actor


def rel_archive(scenario):
    raw = bytearray(raw_rel())
    if scenario.startswith(('link', 'second')):
        struct.pack_into('>I', raw, 0xC, 5)
        struct.pack_into('>II', raw, 0x6C, 0xB0, 16)
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
    if scenario.startswith('second'):
        actor = actor_rel(139)
        if scenario.startswith('second-ctor-'):
            struct.pack_into('>I', actor, 0xA0, 139)
            struct.pack_into('>HBBI', actor, 0xC8, 0, 1, 1, 8)
            struct.pack_into('>HBBI', actor, 0xD0, 4, 1, 1, 12)
            struct.pack_into('>HBBI', actor, 0xD8, 0, 203, 0, 0)
            if scenario == 'second-ctor-empty':
                struct.pack_into('>HBBI', actor, 0xC8, 0, 203, 0, 0)
        if scenario == 'second-bad-id':
            struct.pack_into('>I', actor, 0, 999)
        if scenario == 'second-bad-section':
            struct.pack_into('>I', actor, 0x58, 33)
        if scenario == 'second-bad-bss':
            struct.pack_into('>I', actor, 0x20, 17)
        if scenario == 'second-bad-reloc':
            actor[0xCA] = 2
        if scenario == 'second-reloc-overflow':
            struct.pack_into('>H', actor, 0xC8, 16)
        if scenario == 'second-reloc-untranslated':
            struct.pack_into('>I', actor, 0xA0, 999)
        if scenario == 'second-reloc-unterminated':
            actor[0xD2] = 201
        if scenario == 'second-reloc-export-mismatch':
            struct.pack_into('>I', actor, 0xCC, 0x80002000)
        if scenario == 'second-reloc-raw-export-mismatch':
            actor[0xB4] = 1
        if scenario == 'second-reloc-bss':
            actor[0xC3] = 2
        if scenario == 'second-bad-code-hash':
            actor[0x90] ^= 1
        if scenario.startswith('second-branch-'):
            actor[0xC3] = 1
            actor[0xCA] = 11 if scenario == 'second-branch-rel14-displacement' else 10
            struct.pack_into('>I', actor, 0x80, 0x40000000 if actor[0xCA] == 11 else 0x48000000)
            struct.pack_into('>I', actor, 0xA0, 139)
            actor[0xCB] = 1
            struct.pack_into('>I', actor, 0xCC, 4)
            struct.pack_into('>I', actor, 0xB0, 0x80001000)
            if scenario == 'second-branch-misaligned':
                struct.pack_into('>I', actor, 0xCC, 2)
            if scenario == 'second-branch-bss':
                actor[0xCB] = 2
                struct.pack_into('>I', actor, 0xCC, 0)
            if scenario == 'second-branch-exclusive-end':
                struct.pack_into('>I', actor, 0xA0, 0)
                struct.pack_into('>I', actor, 0xCC, 0x81800000)
            if scenario == 'second-branch-no-coverage':
                struct.pack_into('>I', actor, 0xA0, 0)
                struct.pack_into('>I', actor, 0xCC, 0x80004000)
            if scenario in ('second-branch-displacement', 'second-branch-rel14-displacement'):
                struct.pack_into('>I', actor, 0xA0, 0)
                struct.pack_into('>I', actor, 0xCC, 0x80240744)
        if scenario.startswith('second-dependency-'):
            struct.pack_into('>I', actor, 0xA0, 168)
            actor[0xCB] = 2
            struct.pack_into('>I', actor, 0xCC, 0)
        if scenario in ('second-lifecycle-prolog', 'second-profile-lifecycle-prolog'):
            struct.pack_into('>I', actor, 0x34, 4)
        if scenario == 'second-lifecycle-prolog-zero-offset':
            actor[0x30] = 1
        if scenario == 'second-lifecycle-epilog':
            actor[0x31] = 1
            struct.pack_into('>I', actor, 0x38, 4)
        members = [(b'f_pc_profile_lst.rel', member), (b'ww34_fixture_a.rel', actor)]
        if scenario in ('second-independent', 'second-hole-reuse', 'second-ctor-wrong-owner', 'second-ctor-changed-table') or scenario.startswith('second-dependency-'):
            members.append((b'ww34_fixture_b.rel', actor_rel(168)))
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

    def run_branch_validation(self, patch, target, relocation, word, exported, result='ok', owner='dol'):
        probe = test_host.OUT / 'rel_materialize_probe'
        completed = subprocess.run([str(probe), 'branch-validation', hex(patch), hex(target),
                                    str(relocation), hex(word), hex(exported), result, owner],
                                   capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_branch_validation_matches_prelinked_dol_and_self_targets_in_both_namespaces(self):
        for relocation, word, mask in ((10, 0x48000001, 0x03FFFFFC),
                                       (11, 0x41820001, 0x0000FFFC)):
            for tag in (0, 0x40000000):
                for owner in ('dol', 'self'):
                    with self.subTest(relocation=relocation, tag=tag, owner=owner):
                        patch = 0x80800000 | tag
                        target = 0x80800004 | (tag if owner == 'self' else 0)
                        self.run_branch_validation(patch, target, relocation, word, word | (4 & mask), owner=owner)

    def test_branch_validation_preserves_opcode_condition_and_link_bits(self):
        for relocation, word, exported in ((10, 0x48000000, 0x48000004),
                                           (10, 0x4BFFFFFD, 0x48000005),
                                           (11, 0x41820000, 0x41820004),
                                           (11, 0x4182FFFD, 0x41820005)):
            with self.subTest(relocation=relocation, word=word):
                self.run_branch_validation(0xC0800000, 0x80800004, relocation, word, exported)

    def test_branch_validation_tags_an_untagged_self_target_only_for_a_tagged_patch(self):
        for relocation, word in ((10, 0x48000001), (11, 0x41820001)):
            for patch, target, result in ((0xC0800000, 0x80800004, 'ok'),
                                          (0x80800000, 0xC0800004, 'displacement')):
                with self.subTest(relocation=relocation, patch=patch, target=target):
                    self.run_branch_validation(patch, target, relocation, word, word | 4, result, 'self')

    def test_branch_validation_rejects_a_mismatching_prelinked_displacement(self):
        for relocation, word in ((10, 0x48000001), (11, 0x41820001)):
            with self.subTest(relocation=relocation):
                self.run_branch_validation(0xC0800000, 0x80800004, relocation, word,
                                           word | 8, 'prelinked export')

    def test_branch_validation_signed_displacement_limits(self):
        for relocation, word, mask, minimum, maximum in (
                (10, 0x48000001, 0x03FFFFFC, -0x02000000, 0x01FFFFFC),
                (11, 0x41820001, 0x0000FFFC, -0x8000, 0x7FFC)):
            for delta in (minimum - 4, minimum, maximum, maximum + 4):
                with self.subTest(relocation=relocation, delta=delta):
                    target = 0xC2800000
                    patch = target - delta
                    self.run_branch_validation(patch, target, relocation, word,
                                               word | (delta & mask),
                                               'ok' if minimum <= delta <= maximum else 'displacement', 'self')

    def test_branch_validation_rejects_absolute_or_wrong_opcode_instructions(self):
        for relocation, word in ((10, 0x48000002), (11, 0x41820002),
                                 (10, 0x41820000), (11, 0x48000000)):
            with self.subTest(relocation=relocation, word=word):
                self.run_branch_validation(0xC0800000, 0x80800004, relocation, word, 0, 'instruction')

    def test_branch_validation_rejects_misaligned_dol_targets(self):
        for relocation, word in ((10, 0x48000000), (11, 0x41820000)):
            with self.subTest(relocation=relocation):
                self.run_branch_validation(0xC0800000, 0x80800002, relocation, word, 0, 'executable coverage')

    def test_branch_validation_rejects_exclusive_mem1_end(self):
        for relocation, word in ((10, 0x48000000), (11, 0x41820000)):
            with self.subTest(relocation=relocation):
                self.run_branch_validation(0xC1800000, 0x81800000, relocation, word, 0, 'outside MEM1')

    def test_branch_validation_does_not_admit_tagged_raw_dol_symbols(self):
        for relocation, word in ((10, 0x48000000), (11, 0x41820000)):
            with self.subTest(relocation=relocation):
                self.run_branch_validation(0xC0800000, 0xC0800004, relocation, word, 0, 'outside MEM1')

    def test_branch_validation_rejects_a_misaligned_patch_displacement(self):
        for relocation, word in ((10, 0x48000000), (11, 0x41820000)):
            with self.subTest(relocation=relocation):
                self.run_branch_validation(0xC0800002, 0x80800004, relocation, word, 0, 'displacement')

    def test_owned_constructor_order_and_guest_return(self):
        self.run_probe('second-ctor-order')

    def test_constructor_nested_leaf_calls_and_fp_stores_preserve_inherited_registers(self):
        self.run_probe('second-ctor-leaf')

    def test_owned_raw_constructor_table_maps_to_its_exact_prelinked_section(self):
        self.run_probe('second-ctor-raw')

    def test_empty_constructor_table_returns_without_a_call(self):
        self.run_probe('second-ctor-empty')

    def test_constructor_table_requires_its_callers_owner(self):
        self.run_probe('second-ctor-wrong-owner')

    def test_constructor_table_exclusive_section_end_is_not_mapped(self):
        self.run_probe('second-ctor-outside')

    def test_constructor_target_requires_translated_owned_code(self):
        self.run_probe('second-ctor-untranslated')

    def test_changed_raw_constructor_image_stops_before_dispatch(self):
        self.run_probe('second-ctor-changed-raw')

    def test_constructor_table_mutation_cannot_dispatch_another_live_owner(self):
        self.run_probe('second-ctor-changed-table')

    def test_constructor_continuation_after_unlink_stops_before_dispatch(self):
        self.run_probe('second-ctor-stale')

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

    def test_profile_list_links_to_the_verified_prelinked_export_and_returns_to_the_guest(self):
        self.run_probe('link')

    def test_second_catalog_module_links_without_displacing_the_profile_list(self):
        self.run_probe('second')

    def test_resident_profile_survives_expired_temporary_controller_storage(self):
        for scenario in ('second-profile-owner-expired', 'second-profile-owner-reused'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_expired_profile_controller_does_not_authorize_a_missing_or_foreign_global(self):
        for scenario in ('second-profile-global-cleared', 'second-profile-global-replaced'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_second_rel_lifecycle_still_fails_closed_after_profile_controller_expires(self):
        self.run_probe('second-profile-lifecycle-prolog')

    def test_actor_bss_and_data_are_initialized_independently_for_simultaneous_owners(self):
        self.run_probe('second-independent')

    def test_actor_relink_preserves_mutable_data_and_does_not_read_or_allocate_again(self):
        self.run_probe('second-repeat')

    def test_actor_unload_releases_only_its_scratch_and_reload_initializes_its_bss(self):
        self.run_probe('second-unload')

    def test_actor_relocation_and_section_failures_preserve_existing_owners(self):
        for scenario in ('second-bad-id', 'second-bad-section', 'second-bad-bss',
                         'second-bad-reloc', 'second-reloc-overflow', 'second-reloc-untranslated',
                         'second-reloc-unterminated', 'second-no-code', 'second-missing-bss'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_a_second_owner_cannot_take_a_live_actor_module(self):
        self.run_probe('second-wrong-owner')

    def test_a_live_owner_cannot_change_its_module_name(self):
        self.run_probe('second-wrong-name')

    def test_changed_raw_data_relocation_is_not_replaced_by_an_old_prelinked_value(self):
        self.run_probe('second-reloc-export-mismatch')

    def test_raw_data_bytes_outside_relocations_must_match_the_prelinked_export(self):
        self.run_probe('second-reloc-raw-export-mismatch')

    def test_bss_relocation_is_rejected_before_owner_publication(self):
        self.run_probe('second-reloc-bss')

    def test_branch_target_requires_aligned_executable_coverage_and_representable_displacement(self):
        for scenario in ('second-branch-misaligned', 'second-branch-bss', 'second-branch-no-coverage',
                         'second-branch-exclusive-end', 'second-branch-displacement',
                         'second-branch-rel14-displacement'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_translated_actor_import_requires_live_dependency_not_catalog_presence(self):
        self.run_probe('second-dependency-unlinked')

    def test_valid_executable_relocation_is_still_rejected_without_translation_certification(self):
        self.run_probe('second-branch-valid-unverified')

    def test_raw_executable_bytes_require_matching_compiled_chunk_hashes(self):
        self.run_probe('second-bad-code-hash')

    def test_live_importer_pins_dependency_until_explicit_unlink_and_collection(self):
        self.run_probe('second-dependency-live')

    def test_previously_unloaded_dependency_cannot_satisfy_a_new_import(self):
        self.run_probe('second-dependency-unloaded')

    def test_publication_field_clearing_is_not_completed_actor_unload(self):
        self.run_probe('second-unload-incomplete')

    def test_nontrivial_prolog_and_epilog_contracts_fail_closed(self):
        for scenario in ('second-lifecycle-prolog', 'second-lifecycle-prolog-zero-offset', 'second-lifecycle-epilog'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_raw_actor_entry_uses_the_real_host_dispatch_path(self):
        self.run_probe('second-dispatch')

    def test_native_unlink_without_an_epilog_and_destruction_contract_fails_closed(self):
        self.run_probe('second-native-unlink')

    def test_composite_successor_is_checked_before_its_first_instruction(self):
        for scenario in ('second-loop-unowned', 'second-loop-unlink'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_live_raw_entry_redirects_through_the_composite_loop_without_reallocating(self):
        self.run_probe('second-loop-live')

    def test_live_raw_entry_redirects_through_generated_indirect_dispatch_without_reallocating(self):
        self.run_probe('second-indirect-live')

    def test_second_materialization_failure_preserves_the_profile_and_releases_metadata(self):
        for scenario in ('second-materialize-sections', 'second-materialize-alias', 'second-materialize-trace'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_reusing_a_collected_hole_preserves_other_live_owners_and_shutdown_frees_metadata(self):
        self.run_probe('second-hole-reuse')

    def test_generated_indirect_host_call_is_checked_before_its_first_instruction(self):
        for scenario in ('second-indirect-unowned', 'second-indirect-unlink'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_section_offset_and_execution_flag_changes_cannot_create_new_raw_aliases(self):
        for scenario in ('second-loop-offset', 'second-loop-flags',
                         'second-indirect-offset', 'second-indirect-flags',
                         'second-loop-linked-offset', 'second-indirect-linked-flags'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_repeated_profile_link_does_not_read_or_materialize_again(self):
        self.run_probe('link-repeat')

    def test_invalid_profile_binding_is_not_published(self):
        for scenario in ('link-missing-data', 'link-unmapped', 'link-dirty-data', 'link-wrong-global', 'link-bad-return'):
            with self.subTest(scenario=scenario):
                self.run_probe(scenario)

    def test_profile_link_log_failure_preserves_the_guest_module_and_global(self):
        self.run_probe('link-trace')


if __name__ == '__main__':
    unittest.main()
