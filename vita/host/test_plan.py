import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))
sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))


class PlanTest(unittest.TestCase):
    def test_audio_policy_is_recorded_for_each_explicit_profile(self):
        import stage_plan
        for enabled, expected in ((True, 'deferred'), (False, 'strict')):
            with self.subTest(enabled=enabled):
                args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=180, continue_gx=2048, defer_audio=enabled)
                plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
                self.assertEqual(plan['audio'], expected)

    def test_deferred_audio_cannot_be_used_without_the_real_command_journal(self):
        import stage_plan
        args = SimpleNamespace(timeout=180, continue_gx=0, defer_audio=True)
        with self.assertRaises(ValueError):
            stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)

    def test_shader_compiler_is_copied_into_the_private_profile_with_its_identity(self):
        import stage_plan
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-shader-') as directory:
            root = Path(directory)
            shader = root / 'shader.suprx'
            shader.write_bytes(b'synthetic private system module')
            record = stage_plan.stage_shader_compiler(root / 'run', shader)
            target = root / 'run' / record['install_path']
            self.assertEqual(target.read_bytes(), shader.read_bytes())
            self.assertEqual(record['staged'], stage_plan.file_identity(target))

    def test_optional_disc_staging_keeps_a_verified_identity_without_a_writable_copy(self):
        import stage_plan
        with tempfile.TemporaryDirectory(dir='/home/fred/projects/zeldaWWHDRecomp/build-f5', prefix='ww05-disc-') as directory:
            root = Path(directory)
            image = root / 'private.iso'
            image.write_bytes(b'verified private fixture')
            record = stage_plan.stage_disc(root / 'run', image, stage_plan.sha256(image))
            self.assertEqual(record['identity']['sha256'], stage_plan.sha256(image))
            self.assertEqual((root / 'run' / record['install_path']).readlink(), image)
            self.assertIn('no OS write confinement', record['transport'])

    def test_optional_disc_rejects_a_different_image_before_creating_an_install_target(self):
        import stage_plan
        with tempfile.TemporaryDirectory(dir='/home/fred/projects/zeldaWWHDRecomp/build-f5', prefix='ww05-disc-') as directory:
            root = Path(directory)
            image = root / 'private.iso'
            image.write_bytes(b'different private fixture')
            with self.assertRaisesRegex(ValueError, 'verified WW05 input'):
                stage_plan.stage_disc(root / 'run', image, 'a' * 64)
            self.assertFalse((root / 'run').exists())
    def test_continuation_is_opt_in_and_has_a_command_log_limit_not_an_execution_limit(self):
        import stage_plan
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=600, continue_gx=64, disc=None)
        plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
        self.assertEqual(plan['continue_gx'], 64)
        self.assertIn('no guest execution cap', plan['continuation_policy'])

    def test_continuation_log_limit_is_bounded(self):
        import stage_plan
        for limit in (-1, 196609):
            with self.subTest(limit=limit), self.assertRaises(ValueError):
                stage_plan.make_plan(Path('/unused'), SimpleNamespace(timeout=600, continue_gx=limit),
                                     'a' * 40, 'b' * 64, 'c' * 32)

    def test_full_draw_prefix_journal_is_accepted_without_becoming_an_execution_cap(self):
        import stage_plan
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=120, continue_gx=2048, disc=None)
        plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
        self.assertEqual(plan['continue_gx'], 2048)
        self.assertIn('no guest execution cap', plan['continuation_policy'])

    def test_late_scene_prefix_can_keep_all_133260_commands(self):
        import stage_plan
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=600, continue_gx=133260)
        plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
        self.assertEqual(plan['continue_gx'], 133260)

    def test_scene_memory_window_is_explicit_and_keeps_the_earlier_state_prefix(self):
        import stage_plan
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=600, continue_gx=133260,
                               mem1_snapshots=8, mem1_command_window=[101854, 102936])
        plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
        self.assertEqual(plan['mem1_command_window'], [101854, 102936])
        self.assertEqual(plan['continue_gx'], 133260)

    def test_sixteen_late_snapshots_keep_the_extended_guard_and_image_bounds(self):
        import stage_plan
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=1200, continue_gx=196608,
                               mem1_snapshots=16, mem1_command_window=[102937, 196608])
        plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
        self.assertEqual(plan['mem1_snapshots'], 16)
        self.assertEqual(plan['frame_capture_limit'], 128)
        self.assertEqual(plan['minimum_available_memory_bytes'], 5 * 1024 ** 3 // 2)

    def test_memory_window_cannot_select_unlogged_or_invalid_commands(self):
        import stage_plan
        for window in ([0, 10], [20, 10], [1, 133261], [True, 10], [1], [1, 2, 3]):
            with self.subTest(window=window), self.assertRaises(ValueError):
                stage_plan.make_plan(Path('/unused'), SimpleNamespace(timeout=600, continue_gx=133260,
                                     mem1_snapshots=2, mem1_command_window=window), 'a' * 40, 'b' * 64, 'c' * 32)

    def test_extended_scene_window_keeps_install_deadline_and_records_ram_guard(self):
        import stage_plan
        for timeout in (1200, 1800):
            with self.subTest(timeout=timeout):
                args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=timeout, continue_gx=133260,
                                       mem1_snapshots=8, mem1_command_window=[101854, 102936])
                plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
                self.assertEqual(plan['phases']['guest']['timeout_seconds'], timeout)
                self.assertEqual(plan['phases']['install']['timeout_seconds'], 600)
                self.assertEqual(plan['minimum_available_memory_bytes'], 5 * 1024 ** 3 // 2)
                self.assertEqual(plan['frame_capture_limit'], 128)
                self.assertEqual(plan['idle_timeout_seconds'], 30)

    def test_extended_capture_cannot_disable_the_short_watchdog(self):
        import stage_plan
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=1200, continue_gx=133260,
                               mem1_snapshots=8, mem1_command_window=[101854, 102936], idle_timeout=60)
        with self.assertRaisesRegex(ValueError, 'watchdog'):
            stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)

    def test_extended_capture_deadline_is_still_bounded(self):
        import stage_plan
        args = SimpleNamespace(timeout=1801, continue_gx=133260, mem1_snapshots=8,
                               mem1_command_window=[101854, 102936])
        with self.assertRaisesRegex(ValueError, 'deadline'):
            stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)

    def test_snapshots_without_a_window_cannot_extend_the_default_deadline(self):
        import stage_plan
        args = SimpleNamespace(timeout=1200, continue_gx=133260, mem1_snapshots=8)
        with self.assertRaisesRegex(ValueError, 'deadline'):
            stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)

    def test_memory_snapshot_request_is_explicit_bounded_and_requires_the_draw_journal(self):
        import stage_plan
        for count, journal in ((-1, 2048), (17, 2048), (1, 0), (2, 0)):
            with self.subTest(count=count, journal=journal), self.assertRaises(ValueError):
                stage_plan.make_plan(Path('/unused'), SimpleNamespace(timeout=120, continue_gx=journal, mem1_snapshots=count),
                                     'a' * 40, 'b' * 64, 'c' * 32)

    def test_requested_memory_captures_are_recorded_without_changing_the_default_baseline(self):
        import stage_plan
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=120, continue_gx=2048, mem1_snapshots=2)
        plan = stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)
        self.assertEqual(plan['mem1_snapshots'], 2)
        self.assertTrue(plan['live_frame_capture'])
        self.assertFalse(any('--bind' in phase['command'] for phase in plan['phases'].values()))
        args.mem1_snapshots = 0
        self.assertEqual(stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)['mem1_snapshots'], 0)
        self.assertNotIn('/unused/evidence/frames', stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)['phases']['guest']['command'])

    def test_optional_disc_does_not_add_mounts_to_native_commands(self):
        import stage_plan
        args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=600, continue_gx=64,
                               disc=Path('/private/input.iso'))
        plan = stage_plan.make_plan(Path('/private/run'), args, 'a' * 40, 'b' * 64, 'c' * 32)
        for phase in plan['phases'].values():
            command = phase['command']
            self.assertEqual(command[0], '/private/run/runtime/Vita3K')
            self.assertNotIn('/private/input.iso', command)
            self.assertNotIn('--ro-bind', command)
    def test_deeper_startup_allows_a_bounded_three_minute_phase(self):
        import stage_plan
        with tempfile.TemporaryDirectory(prefix='ww05-deadline-', dir='/home/fred/projects/zeldaWWHDRecomp/build-f5') as directory:
            args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=180)
            plan = stage_plan.make_plan(Path(directory), args, 'a' * 40, 'b' * 64, 'c' * 32)
            self.assertEqual(plan['phases']['install']['timeout_seconds'], 180)
            self.assertEqual(plan['phases']['guest']['timeout_seconds'], 180)

    def test_coordinator_requested_ten_minute_phases_keep_owned_cleanup(self):
        import stage_plan
        with tempfile.TemporaryDirectory(prefix='ww05-deadline-', dir='/home/fred/projects/zeldaWWHDRecomp/build-f5') as directory:
            args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=600)
            plan = stage_plan.make_plan(Path(directory), args, 'a' * 40, 'b' * 64, 'c' * 32)
            self.assertEqual(plan['phases']['install']['timeout_seconds'], 600)
            self.assertEqual(plan['phases']['guest']['timeout_seconds'], 600)
            self.assertIn('KILL/reap', plan['cleanup'])

    def test_phase_deadlines_remain_finite_positive_and_bounded(self):
        import stage_plan
        for timeout in (0, -1, 601, float('inf'), float('nan')):
            with self.subTest(timeout=timeout):
                args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=timeout)
                with self.assertRaisesRegex(ValueError, 'deadline'):
                    stage_plan.make_plan(Path('/unused'), args, 'a' * 40, 'b' * 64, 'c' * 32)

    def test_changed_elf_is_rejected_before_staging(self):
        import stage_plan
        with tempfile.TemporaryDirectory(prefix='ww05-package-', dir='/home/fred/projects/zeldaWWHDRecomp/build-f5') as directory:
            output = Path(directory)
            (output / 'private').mkdir()
            dol = output / 'private/main.dol'
            dol.write_bytes(b'synthetic DOL')
            (output / 'inputs.json').write_text(json.dumps({'dol_sha256': stage_plan.sha256(dol)}))
            for name in ('ww05_host', 'ww05_host.velf', 'eboot.bin', 'ww05.vpk_param.sfo'):
                (output / name).write_bytes(b'synthetic ' + name.encode())
            with zipfile.ZipFile(output / 'ww05.vpk', 'w') as archive:
                archive.write(dol, 'main.dol')
                archive.write(output / 'eboot.bin', 'eboot.bin')
                archive.write(output / 'ww05.vpk_param.sfo', 'sce_sys/param.sfo')
            artifacts = {str(path): {'bytes': path.stat().st_size, 'sha256': stage_plan.sha256(path)}
                         for path in output.iterdir() if path.name.startswith(('ww05', 'eboot'))}
            evidence = {'title': 'BWWG00005', 'method': 'startup-gx-v1', 'artifacts': artifacts}
            (output / 'ww05_host').write_bytes(b'changed ELF')
            with patch.object(stage_plan, 'OUTPUT', output):
                with self.assertRaisesRegex(ValueError, 'artifact changed: ww05_host'):
                    stage_plan.verify_package(evidence)

    def test_preparation_has_two_exact_commands_and_starts_no_process(self):
        spec = importlib.util.spec_from_file_location('ww05_stage', ROOT / 'scripts/vita/ww05/stage_plan.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix='ww05-plan-', dir='/home/fred/projects/zeldaWWHDRecomp/build-f5') as directory:
            root = Path(directory)
            args = SimpleNamespace(libraries=None, wayland_socket=None, timeout=60)
            with patch('subprocess.Popen', side_effect=AssertionError('preparation must not launch')):
                plan = module.make_plan(root, args, 'a' * 40, 'b' * 64, 'c' * 32)
            self.assertNotIn('process_budget', plan)
            self.assertNotIn('authorization', plan)
            self.assertEqual(plan['retry'], 'fresh private profile for every attempt')
            self.assertFalse(plan['real_execution'])
            self.assertEqual(plan['phases']['install']['command'][-5:],
                             [str(root / 'runtime/Vita3K'), '--console', '--backend-renderer', 'OpenGL', 'ww05.vpk'])
            self.assertEqual(plan['phases']['guest']['command'][-5:],
                             [str(root / 'runtime/Vita3K'), '--backend-renderer', 'OpenGL', '--installed-path', 'BWWG00005'])
            self.assertNotIn('--unshare-all', plan['phases']['guest']['command'])
            self.assertEqual(plan['phases']['guest']['timeout_seconds'], 60)


class CompositeIdentityTest(unittest.TestCase):
    def setUp(self):
        import game_build
        self.game_build = game_build
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww05-composite-')
        self.addCleanup(self.temporary.cleanup)
        self.composite = Path(self.temporary.name)
        self.header = self.composite / 'generated_composite.h'

    def manifest_digest(self):
        file_digest = hashlib.sha256(self.header.read_bytes()).hexdigest()
        return hashlib.sha256(f'{file_digest} generated_composite.h\n'.encode()).hexdigest()

    def test_composite_without_the_canon_macro_is_refused(self):
        self.header.write_text('// dispatcher without the adopted CPU check\n')
        with patch.object(self.game_build, 'COMPOSITE_DIGEST', self.manifest_digest()):
            with self.assertRaisesRegex(ValueError, 'DOLRECOMP_CANON_CTX'):
                self.game_build.checked_composite(self.composite)

    def test_missing_composite_header_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'header is missing'):
            self.game_build.checked_composite(self.composite)

    def test_composite_digest_must_match_the_pinned_tree(self):
        self.header.write_text('#define DOLRECOMP_CANON_CTX(ctx, address) (ctx)\n')
        with patch.object(self.game_build, 'COMPOSITE_DIGEST', '0' * 64):
            with self.assertRaisesRegex(ValueError, 'digest differs'):
                self.game_build.checked_composite(self.composite)

    def test_verified_composite_is_admitted(self):
        self.header.write_text('#define DOLRECOMP_CANON_CTX(ctx, address) (ctx)\n')
        with patch.object(self.game_build, 'COMPOSITE_DIGEST', self.manifest_digest()):
            self.assertEqual(self.game_build.checked_composite(self.composite), self.manifest_digest())

    def test_builder_profile_pins_the_same_digest(self):
        profile = (ROOT / 'scripts/builder/profiles/bluewake.sh').read_text()
        match = re.search(r'^COMPOSITE_DIGEST=([0-9a-f]{64})$', profile, re.MULTILINE)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), self.game_build.COMPOSITE_DIGEST)


class BaselineBuildTest(unittest.TestCase):
    def setUp(self):
        import stage_plan
        from game_build import PINS, COMPOSITE_DIGEST
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-baseline-')
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        self.snapshot = self.output / 'artifacts-fixture'
        self.snapshot.mkdir()
        (self.snapshot / 'footprint.json').write_text('{}\n')
        (self.output / 'footprint.json').write_text('{}\n')
        self.evidence = {'source': 'a' * 40, 'snapshot': str(self.snapshot), 'pins': PINS,
                         'composite_digest': COMPOSITE_DIGEST,
                         'footprint_sha256': stage_plan.sha256(self.snapshot / 'footprint.json')}
        self.path = self.snapshot / 'build-evidence.json'

    def select(self, baseline=True, ancestor_status=0):
        import stage_plan
        self.path.write_text(json.dumps(self.evidence))
        with patch.object(stage_plan, 'OUTPUT', self.output), \
             patch('subprocess.run', return_value=SimpleNamespace(returncode=ancestor_status, stderr='synthetic git error')) as run:
            result = stage_plan.checked_build_receipt(self.path, 'b' * 40, baseline)
        return result, run

    def test_explicit_ancestor_build_keeps_its_guest_source_separate_from_the_launcher(self):
        evidence, run = self.select()
        self.assertEqual(evidence['source'], 'a' * 40)
        self.assertIn(['git', 'merge-base', '--is-ancestor', 'a' * 40, 'b' * 40], run.call_args.args)

    def test_default_delivery_does_not_accept_a_stale_guest_build(self):
        with self.assertRaisesRegex(ValueError, 'delivery SHA'):
            self.select(baseline=False)

    def test_default_delivery_still_accepts_the_matching_checkpoint(self):
        self.evidence['source'] = 'b' * 40
        evidence, run = self.select(baseline=False)
        self.assertEqual(evidence['source'], 'b' * 40)
        run.assert_not_called()

    def test_unrelated_commit_cannot_be_selected_as_the_baseline(self):
        with self.assertRaisesRegex(ValueError, 'ancestor'):
            self.select(ancestor_status=1)

    def test_a_git_ancestry_error_is_not_treated_as_authorized(self):
        with self.assertRaisesRegex(RuntimeError, 'ancestry'):
            self.select(ancestor_status=128)

    def test_changed_snapshot_footprint_is_refused(self):
        (self.snapshot / 'footprint.json').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'footprint'):
            self.select()

    def test_different_pins_are_refused(self):
        self.evidence['pins'] = {}
        with self.assertRaisesRegex(ValueError, 'pins'):
            self.select()

    def test_different_composite_identity_is_refused(self):
        self.evidence['composite_digest'] = 'f' * 64
        with self.assertRaisesRegex(ValueError, 'composite'):
            self.select()

    def test_a_receipt_outside_its_saved_snapshot_cannot_be_used_as_a_baseline(self):
        self.path = self.output / 'copied-receipt.json'
        with self.assertRaisesRegex(ValueError, 'preserved snapshot'):
            self.select()


class CandidateBuildTest(unittest.TestCase):
    def setUp(self):
        import stage_plan
        from game_build import PINS
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww57-candidate-')
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        self.vpk = self.output / 'vita/ww05.vpk'
        self.vpk.parent.mkdir()
        self.eboot = self.output / 'vita/eboot.bin'
        self.eboot.write_bytes(b'synthetic candidate eboot')
        members = {'eboot.bin': self.eboot.read_bytes(), 'main.dol': b'synthetic candidate DOL',
                   'sce_sys/param.sfo': b'synthetic candidate SFO'}
        with zipfile.ZipFile(self.vpk, 'w') as archive:
            for name, content in members.items():
                archive.writestr(name, content)
        self.source = 'a' * 40
        self.digest = 'c' * 64
        self.vpk_sha256 = stage_plan.sha256(self.vpk)
        self.eboot_sha256 = stage_plan.sha256(self.eboot)
        self.evidence = {
            'status': 'PASS-private-offline-build-inspection', 'source': self.source, 'pins': PINS,
            'composite_digest': self.digest, 'current_composite_digest': self.digest,
            'artifacts': {
                'ww05.vpk': {'path': str(self.vpk), 'bytes': self.vpk.stat().st_size, 'sha256': self.vpk_sha256},
                'eboot.bin': {'path': str(self.eboot), 'bytes': self.eboot.stat().st_size, 'sha256': self.eboot_sha256},
            },
            'vpk_members': {name: {'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}
                            for name, content in members.items()},
            'config': {'pad_input': {'enabled': True, 'source': 'sceCtrlPeekBufferPositive',
                                     'sampling': 'guest-SI-only'}},
        }
        self.receipt = self.output / 'build-evidence.json'
        self.descriptor = {'kind': 'ww05-private-candidate-v1', 'receipt': str(self.receipt),
                           'source': self.source, 'composite_digest': self.digest,
                           'vpk_sha256': self.vpk_sha256, 'eboot_sha256': self.eboot_sha256,
                           'pad_input': True}
        self.descriptor_path = self.output / 'candidate.json'

    def write(self, descriptor=None, evidence=None):
        self.receipt.write_text(json.dumps(self.evidence if evidence is None else evidence))
        self.descriptor_path.write_text(json.dumps(self.descriptor if descriptor is None else descriptor))

    def admit(self, ancestor_status=0):
        import stage_plan
        with patch('subprocess.run', return_value=SimpleNamespace(returncode=ancestor_status,
                                                                  stderr='synthetic git error')) as run:
            descriptor = stage_plan.load_candidate_descriptor(self.descriptor_path)
            evidence = stage_plan.checked_candidate_receipt(descriptor, 'b' * 40)
            vpk, package = stage_plan.verify_candidate_package(descriptor, evidence)
        return descriptor, evidence, vpk, package, run

    def test_current_private_candidate_is_admitted_strictly(self):
        self.write()
        descriptor, evidence, vpk, package, run = self.admit()
        self.assertEqual(descriptor['source'], self.source)
        self.assertEqual(evidence['source'], self.source)
        self.assertEqual(evidence['composite_digest'], self.digest)
        self.assertEqual(vpk, self.vpk)
        self.assertEqual(package, self.vpk_sha256)
        self.assertIn(['git', 'merge-base', '--is-ancestor', self.source, 'b' * 40], run.call_args.args)

    def test_tampered_candidate_artifact_is_rejected(self):
        self.write()
        self.eboot.write_bytes(b'tampered candidate eboot')
        with self.assertRaisesRegex(ValueError, 'artifact changed: eboot.bin'):
            self.admit()

    def test_tampered_candidate_package_is_rejected(self):
        self.write()
        with zipfile.ZipFile(self.vpk, 'w') as archive:
            for name in ('eboot.bin', 'main.dol', 'sce_sys/param.sfo'):
                archive.writestr(name, b'tampered')
        with self.assertRaisesRegex(ValueError, 'artifact changed: ww05.vpk'):
            self.admit()

    def test_descriptor_artifact_hash_mismatch_is_rejected(self):
        for field in ('vpk_sha256', 'eboot_sha256'):
            with self.subTest(field=field):
                self.write(descriptor={**self.descriptor, field: 'f' * 64})
                with self.assertRaisesRegex(ValueError, 'does not match the descriptor'):
                    self.admit()

    def test_descriptor_and_receipt_identity_mismatches_are_rejected(self):
        for field, value in (('source', 'd' * 40), ('composite_digest', 'e' * 64),
                             ('current_composite_digest', 'e' * 64), ('pins', {})):
            with self.subTest(field=field):
                self.write(evidence={**self.evidence, field: value})
                with self.assertRaises(ValueError):
                    self.admit()

    def test_candidate_pad_state_must_match_the_receipt(self):
        self.write(evidence={**self.evidence, 'config': {'pad_input': {'enabled': False}}})
        with self.assertRaisesRegex(ValueError, 'PAD'):
            self.admit()
        self.write(descriptor={**self.descriptor, 'pad_input': False})
        with self.assertRaisesRegex(ValueError, 'PAD'):
            self.admit()

    def test_missing_receipt_fields_fail_closed(self):
        for field in ('status', 'source', 'pins', 'composite_digest', 'current_composite_digest',
                      'config', 'artifacts', 'vpk_members'):
            with self.subTest(field=field):
                evidence = dict(self.evidence)
                del evidence[field]
                self.write(evidence=evidence)
                with self.assertRaises(ValueError):
                    self.admit()

    def test_unknown_candidate_descriptor_schema_is_rejected(self):
        import stage_plan
        cases = ({**self.descriptor, 'kind': 'ww05-other-v1'},
                 {**self.descriptor, 'extra': True},
                 {key: value for key, value in self.descriptor.items() if key != 'eboot_sha256'})
        for descriptor in cases:
            with self.subTest(descriptor=sorted(descriptor)):
                self.write(descriptor=descriptor)
                with self.assertRaisesRegex(ValueError, 'candidate descriptor'):
                    stage_plan.load_candidate_descriptor(self.descriptor_path)

    def test_candidate_descriptor_identities_are_validated(self):
        import stage_plan
        for field, value in (('source', 'A' * 40), ('composite_digest', 'c' * 63),
                             ('vpk_sha256', 'z' * 64), ('eboot_sha256', 7), ('pad_input', 'yes'),
                             ('receipt', 7)):
            with self.subTest(field=field):
                self.write(descriptor={**self.descriptor, field: value})
                with self.assertRaises(ValueError):
                    stage_plan.load_candidate_descriptor(self.descriptor_path)

    def test_candidate_source_must_be_an_ancestor_of_the_launcher(self):
        self.write()
        with self.assertRaisesRegex(ValueError, 'ancestor'):
            self.admit(ancestor_status=1)
        with self.assertRaisesRegex(RuntimeError, 'ancestry'):
            self.admit(ancestor_status=128)

    def test_candidate_package_payload_mismatch_is_rejected(self):
        self.write(evidence={**self.evidence, 'vpk_members': {
            **self.evidence['vpk_members'], 'main.dol': {'bytes': 7, 'sha256': 'a' * 64}}})
        with self.assertRaisesRegex(ValueError, 'payload mismatch: main.dol'):
            self.admit()

    def test_candidate_receipt_is_not_admitted_by_the_legacy_baseline_path(self):
        import stage_plan
        self.write()
        with self.assertRaisesRegex(ValueError, 'composite'):
            stage_plan.checked_build_receipt(self.receipt, 'b' * 40)

    def test_check_mode_admits_the_candidate_and_writes_nothing(self):
        import stage_plan
        self.write()
        before = sorted(str(path) for path in self.output.rglob('*'))
        args = SimpleNamespace(timeout=60, idle_timeout=30, continue_gx=0, mem1_snapshots=0,
                               mem1_command_window=None, render_node=None,
                               candidate_build=self.descriptor_path, baseline_build=None,
                               wayland_socket=Path('/run/user/1000/wayland-0'))
        with patch('subprocess.check_output', side_effect=['b' * 40, '']), \
                patch.object(stage_plan, 'checked_sources'), \
                patch('subprocess.run', return_value=SimpleNamespace(returncode=0, stderr='')):
            self.assertEqual(stage_plan.check(args), 0)
        self.assertEqual(before, sorted(str(path) for path in self.output.rglob('*')))

    def test_candidate_and_baseline_selection_are_mutually_exclusive(self):
        import stage_plan
        args = SimpleNamespace(timeout=60, render_node=None, candidate_build=self.descriptor_path,
                               baseline_build=Path('/unused/baseline.json'))
        with self.assertRaisesRegex(ValueError, 'mutually exclusive'):
            stage_plan.stage(args)
        with self.assertRaisesRegex(ValueError, 'mutually exclusive'):
            stage_plan.check(args)


if __name__ == '__main__':
    unittest.main()
