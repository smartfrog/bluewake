"""Tests for the staged plan schema and the configuration gate ordering.

The staged plan is written once and read by verification; producer and
consumers must share one schema (top-level package and runtime hashes plus
their identity records). The private config is checked as canonical only when
it is canonical: after installation and after the guest the runtime may have
emitted the explicitly permitted `user-id: 00` mutation, which is validated by
the emitted-form check, preserved as evidence, restored to canonical, and only
then checked again.
"""

from contextlib import ExitStack
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

import run

CANONICAL = {'template': 'canonical'}
MUTATED = {'template': 'mutated'}


def fake_stage_shader(root, source):
    target = root / 'data/Vita3K/Vita3K/ur0/data/libshacccg.suprx'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(Path(source).read_bytes())
    return {'source': str(source), 'identity': run.file_identity(source),
            'staged': run.file_identity(target), 'install_path': str(target.relative_to(root))}


def fake_stage_disc(root, path, expected_sha256):
    destination = root / 'data/Vita3K/Vita3K/ux0/data/bluewake/GZLE01.iso'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.symlink_to(path)
    return {'path': str(path), 'identity': run.file_identity(path),
            'install_path': str(destination.relative_to(root)), 'transport': 'test'}


def fake_direct_phases(root, timeout, libraries, wayland_socket):
    return {name: {'command': [str(root / 'runtime/Vita3K')], 'environment': {},
                   'cwd': str(root / 'runtime'), 'renderer': {}, 'timeout_seconds': timeout,
                   'evidence_root': str(root / 'phases' / name)}
            for name in ('install', 'guest')}


class StagingSchemaTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)
        (self.root / 'build').mkdir()
        (self.root / 'receipts').mkdir()
        self.vpk = self.root / 'candidate.vpk'
        self.vpk.write_bytes(b'candidate-package')
        self.runtime = self.root / 'Vita3K'
        self.runtime.write_bytes(b'runtime-elf')
        self.shader = self.root / 'libshacccg.suprx'
        self.shader.write_bytes(b'shader')
        self.disc = self.root / 'disc.iso'
        self.disc.write_bytes(b'disc')
        self.firmware = self.root / 'firmware'
        (self.firmware / 'vs0').mkdir(parents=True)
        (self.firmware / 'sa0').mkdir()
        self.libraries = self.root / 'libraries'
        self.libraries.mkdir()
        self.args = SimpleNamespace(
            run_root_base=self.root / 'build', receipts_root=self.root / 'receipts', run_id='schema',
            vpk=self.vpk, vpk_sha256=run.sha256(self.vpk),
            runtime=self.runtime, runtime_sha256=run.sha256(self.runtime),
            disc=self.disc, disc_sha256=run.sha256(self.disc),
            shader_compiler=self.shader, libraries=self.libraries, firmware=self.firmware,
            wayland_socket=self.root / 'wayland-0', guest_source='0' * 40, expected_head='1' * 40,
            guest_ceiling_seconds=180.0, audio='deferred', continue_gx=64, hang_seconds=30.0)

    def tearDown(self):
        self.temporary.cleanup()

    def test_staging_produces_the_schema_verification_reads(self):
        with ExitStack() as stack:
            stack.enter_context(patch.object(run, 'stage_runtime', return_value={}))
            stack.enter_context(patch.object(run, 'stage_firmware', return_value={}))
            stack.enter_context(patch.object(run, 'stage_shader_compiler', side_effect=fake_stage_shader))
            stack.enter_context(patch.object(run, 'stage_disc', side_effect=fake_stage_disc))
            stack.enter_context(patch.object(run, 'library_identity', return_value={}))
            stack.enter_context(patch.object(run, 'runtime_inputs', return_value={}))
            stack.enter_context(patch.object(run, 'config_identity', return_value=CANONICAL))
            stack.enter_context(patch.object(run, 'direct_phases', side_effect=fake_direct_phases))
            runroot, receipts, plan = run.stage_trial(self.args)
        self.assertEqual(plan['vpk_sha256'], self.args.vpk_sha256)
        self.assertEqual(plan['runtime_sha256'], self.args.runtime_sha256)
        self.assertEqual(plan['vpk']['identity']['sha256'], self.args.vpk_sha256)
        self.assertEqual(plan['runtime']['identity']['sha256'], self.args.runtime_sha256)
        self.assertEqual(plan['phases']['install']['evidence_root'],
                         str(runroot / 'phases' / 'install'))
        self.assertTrue((receipts / 'effective-plan.json').is_file())
        with ExitStack() as stack:
            stack.enter_context(patch.object(run, 'git_head', return_value=self.args.expected_head))
            stack.enter_context(patch.object(run, 'git_clean', return_value=True))
            stack.enter_context(patch.object(run, 'pinned_sources', return_value={}))
            stack.enter_context(patch.object(run, 'runtime_inputs', return_value={}))
            stack.enter_context(patch.object(run, 'library_identity', return_value={}))
            run.verify_trial_inputs(runroot, plan, self.args)


class ConfigOrderingTests(unittest.TestCase):
    def setUp(self):
        self.runroot = Path('/unused-runroot')
        self.phase = Path('/unused-phase')
        self.plan = {'private_config': CANONICAL}
        self.calls = []
        self.canonical = False

    def config_identity(self, runroot, **kwargs):
        self.calls.append('config_identity')
        return CANONICAL if self.canonical else MUTATED

    def emitted_config(self, runroot, phase, **kwargs):
        self.calls.append('emitted_config')
        return b'canonical-bytes'

    def restore_config(self, runroot, content, **kwargs):
        self.calls.append('restore_config')
        self.canonical = True

    def gate_patches(self, stack):
        stack.enter_context(patch.object(run, 'emitted_config', side_effect=self.emitted_config))
        stack.enter_context(patch.object(run, 'restore_config', side_effect=self.restore_config))
        stack.enter_context(patch.object(run, 'config_identity', side_effect=self.config_identity))
        stack.enter_context(patch.object(run, 'verify_trial_inputs',
                                         side_effect=lambda *a, **k: self.calls.append('verify_trial_inputs')))
        stack.enter_context(patch.object(run, 'verify_phase_logs',
                                         side_effect=lambda *a, **k: self.calls.append('verify_phase_logs')))
        stack.enter_context(patch.object(run, 'verify_installed',
                                         side_effect=lambda *a, **k: self.calls.append('verify_installed') or {'ok': True}))

    def test_installation_gates_accept_the_permitted_mutation_before_restore(self):
        with ExitStack() as stack:
            self.gate_patches(stack)
            payload = run.installation_gates(self.runroot, self.plan, SimpleNamespace(), self.phase)
        self.assertEqual(payload, {'ok': True})
        self.assertEqual(self.calls, ['emitted_config', 'verify_phase_logs', 'verify_trial_inputs',
                                      'verify_installed', 'restore_config', 'config_identity'])
        self.assertTrue(self.canonical)

    def test_guest_gates_accept_the_permitted_mutation_before_restore(self):
        with ExitStack() as stack:
            self.gate_patches(stack)
            run.guest_gates(self.runroot, self.plan, SimpleNamespace(), self.phase)
        self.assertEqual(self.calls, ['emitted_config', 'verify_trial_inputs', 'verify_installed',
                                      'verify_phase_logs', 'restore_config', 'config_identity'])
        self.assertTrue(self.canonical)

    def test_canonical_verification_rejects_a_config_left_mutated(self):
        with patch.object(run, 'config_identity', return_value=MUTATED):
            with self.assertRaisesRegex(ValueError, 'canonical'):
                run.verify_private_config(self.runroot, self.plan)

    def test_canonical_verification_accepts_the_canonical_config(self):
        with patch.object(run, 'config_identity', return_value=CANONICAL):
            run.verify_private_config(self.runroot, self.plan)


if __name__ == '__main__':
    unittest.main()
