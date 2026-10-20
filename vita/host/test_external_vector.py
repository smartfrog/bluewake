import hashlib
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

import test_host
from vector_fixture import external_vector_fixture

ROOT = test_host.ROOT

(ROOT / 'build-f5').mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))


def synthetic_lineage(exception_number=4):
    source = bytearray([0xA5] * 152)
    struct.pack_into('>I', source, 104, 0x38600000)
    observed = bytearray(source)
    struct.pack_into('>I', observed, 104, 0x38600000 | exception_number)
    dol = bytearray(256) + source
    for offset, value in ((0, 256), (0x48, 0x80301D8C), (0x90, 152)):
        struct.pack_into('>I', dol, offset, value)
    return dol, bytes(source), bytes(observed)


class ExternalVectorInputTest(unittest.TestCase):
    def extract(self, dol, source, observed):
        import external_vector
        with patch.object(external_vector, 'TEMPLATE_SHA256', hashlib.sha256(source).hexdigest()):
            with patch.object(external_vector, 'OBSERVED_SHA256', hashlib.sha256(observed).hexdigest()):
                return external_vector.extract_external_vector(dol, observed)

    def test_only_the_observed_external_vector_is_returned(self):
        dol, source, observed = synthetic_lineage()
        self.assertEqual(self.extract(dol, source, observed), observed)

    def test_unknown_observed_vector_is_refused(self):
        import external_vector
        dol, _, observed = synthetic_lineage()
        with self.assertRaisesRegex(ValueError, 'identity'):
            external_vector.extract_external_vector(dol, observed)

    def test_each_changed_observed_byte_is_refused(self):
        import external_vector
        dol, source, observed = synthetic_lineage()
        with patch.object(external_vector, 'TEMPLATE_SHA256', hashlib.sha256(source).hexdigest()):
            with patch.object(external_vector, 'OBSERVED_SHA256', hashlib.sha256(observed).hexdigest()):
                for index in range(152):
                    with self.subTest(index=index):
                        changed = bytearray(observed)
                        changed[index] ^= 1
                        with self.assertRaisesRegex(ValueError, 'identity'):
                            external_vector.extract_external_vector(dol, changed)

    def test_unrelated_source_change_is_refused_even_with_matching_fixture_hashes(self):
        dol, source, observed = synthetic_lineage()
        changed = bytearray(source)
        changed[12] ^= 1
        dol[256:] = changed
        with self.assertRaisesRegex(ValueError, 'lineage'):
            self.extract(dol, changed, observed)

    def test_wrong_exception_number_is_not_an_external_vector(self):
        dol, source, observed = synthetic_lineage()
        changed = bytearray(observed)
        struct.pack_into('>I', changed, 104, 0x38600008)
        with self.assertRaisesRegex(ValueError, 'lineage'):
            self.extract(dol, source, changed)

    def test_truncated_template_payload_is_refused(self):
        dol, source, observed = synthetic_lineage()
        with self.assertRaisesRegex(ValueError, 'payload'):
            self.extract(dol[:-1], source, observed)

    def test_ambiguous_template_mapping_is_refused(self):
        dol, source, observed = synthetic_lineage()
        for offset, value in ((4, 256), (0x4C, 0x80301D8C), (0x94, 152)):
            struct.pack_into('>I', dol, offset, value)
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            self.extract(dol, source, observed)

    def test_invalid_input_cannot_reach_the_emitter_or_replace_generated_output(self):
        import external_vector
        dol, _, observed = synthetic_lineage()
        with tempfile.TemporaryDirectory(dir=str(ROOT / 'build-f5'), prefix='vector-input-') as directory:
            output = Path(directory) / 'generated.c'
            output.write_bytes(b'previous generated source')
            with patch('subprocess.run') as run:
                with self.assertRaisesRegex(ValueError, 'identity'):
                    external_vector.emit_external_vector(dol, observed, Path('unused-emitter'), output)
                run.assert_not_called()
            self.assertEqual(output.read_bytes(), b'previous generated source')

    def test_emitter_failure_does_not_replace_previous_generated_output(self):
        import external_vector
        dol, source, observed = synthetic_lineage()
        with tempfile.TemporaryDirectory(dir=str(ROOT / 'build-f5'), prefix='vector-input-') as directory:
            output = Path(directory) / 'generated.c'
            output.write_bytes(b'previous generated source')
            with patch.object(external_vector, 'TEMPLATE_SHA256', hashlib.sha256(source).hexdigest()), \
                 patch.object(external_vector, 'OBSERVED_SHA256', hashlib.sha256(observed).hexdigest()), \
                 patch('subprocess.run', side_effect=subprocess.CalledProcessError(1, 'emitter')):
                with self.assertRaises(subprocess.CalledProcessError):
                    external_vector.emit_external_vector(dol, observed, Path('unused-emitter'), output)
            self.assertEqual(output.read_bytes(), b'previous generated source')

    def test_generated_output_uses_a_snapshot_of_exactly_the_validated_input(self):
        import external_vector
        dol, source, observed = synthetic_lineage()
        with tempfile.TemporaryDirectory(dir=str(ROOT / 'build-f5'), prefix='vector-input-') as directory:
            output = Path(directory) / 'generated.c'
            with patch.object(external_vector, 'TEMPLATE_SHA256', hashlib.sha256(source).hexdigest()), \
                 patch.object(external_vector, 'OBSERVED_SHA256', hashlib.sha256(observed).hexdigest()), \
                 patch('subprocess.run', return_value=subprocess.CompletedProcess([], 0, b'synthetic generated source')) as run:
                external_vector.emit_external_vector(dol, observed, Path('fixture-emitter'), output)
                run.assert_called_once_with(['fixture-emitter', str(output.with_suffix('.bin'))],
                                            check=True, capture_output=True)
            self.assertEqual(output.with_suffix('.bin').read_bytes(), observed)
            self.assertEqual(output.read_bytes(), b'synthetic generated source')


class ExternalVectorGeneratorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def emit(self, code, vector=None):
        with tempfile.TemporaryDirectory(dir=test_host.OUT, prefix='vector-emit-') as directory:
            path = Path(directory) / 'synthetic.bin'
            path.write_bytes(code)
            command = [str(test_host.OUT / 'vector-generator/ww_vector_emit'), str(path)]
            if vector is not None:
                command.append(vector)
            return subprocess.run(command,
                                  capture_output=True, text=True)

    def test_pinned_emitter_generates_the_physical_vector_and_its_exact_reference(self):
        result = self.emit(external_vector_fixture())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('void func_00000500(CPUState* ctx)', result.stdout)
        self.assertIn('const u8 ww05_external_vector_reference[152]', result.stdout)
        self.assertIn('#define DOLRECOMP_CPU_GEKKO 1', result.stdout)

    def test_truncated_vector_is_rejected_before_emission(self):
        result = self.emit(external_vector_fixture()[:-1])
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')

    def test_trailing_input_bytes_are_not_silently_ignored(self):
        result = self.emit(external_vector_fixture() + b'\0')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')

    def test_pinned_emitter_uses_the_fp_unavailable_physical_vector_when_requested(self):
        result = self.emit(external_vector_fixture(7), '00000800')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('void func_00000800(CPUState* ctx)', result.stdout)
        self.assertIn('const u8 ww05_fp_vector_reference[152]', result.stdout)

    def test_emitter_uses_the_observed_decrementer_physical_base(self):
        result = self.emit(external_vector_fixture(8), '00000900')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('void func_00000900(CPUState* ctx)', result.stdout)
        self.assertIn('const u8 ww05_decrementer_vector_reference[152]', result.stdout)

    def test_unobserved_vector_base_is_refused_before_emission(self):
        result = self.emit(external_vector_fixture(), '00000700')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')


class FpVectorInputTest(unittest.TestCase):
    def test_only_the_observed_exception_seven_patch_is_accepted(self):
        import external_vector
        dol, source, observed = synthetic_lineage(7)
        with patch.object(external_vector, 'TEMPLATE_SHA256', hashlib.sha256(source).hexdigest()), \
             patch.object(external_vector, 'FP_OBSERVED_SHA256', hashlib.sha256(observed).hexdigest()):
            self.assertEqual(external_vector.extract_fp_vector(dol, observed), observed)

    def test_unknown_fp_vector_is_refused(self):
        import external_vector
        dol, _, observed = synthetic_lineage(7)
        with self.assertRaisesRegex(ValueError, 'identity'):
            external_vector.extract_fp_vector(dol, observed)

    def test_external_vector_cannot_be_substituted_for_the_fp_vector(self):
        import external_vector
        dol, source, observed = synthetic_lineage(4)
        with patch.object(external_vector, 'TEMPLATE_SHA256', hashlib.sha256(source).hexdigest()), \
             patch.object(external_vector, 'FP_OBSERVED_SHA256', hashlib.sha256(observed).hexdigest()):
            with self.assertRaisesRegex(ValueError, 'lineage'):
                external_vector.extract_fp_vector(dol, observed)

    def test_validated_fp_input_is_emitted_at_its_observed_physical_base(self):
        import external_vector
        dol, source, observed = synthetic_lineage(7)
        with tempfile.TemporaryDirectory(dir=str(ROOT / 'build-f5'), prefix='fp-vector-input-') as directory:
            output = Path(directory) / 'generated.c'
            with patch.object(external_vector, 'TEMPLATE_SHA256', hashlib.sha256(source).hexdigest()), \
                 patch.object(external_vector, 'FP_OBSERVED_SHA256', hashlib.sha256(observed).hexdigest()), \
                 patch('subprocess.run', return_value=subprocess.CompletedProcess([], 0, b'synthetic generated source')) as run:
                external_vector.emit_fp_vector(dol, observed, Path('fixture-emitter'), output)
                run.assert_called_once_with(['fixture-emitter', str(output.with_suffix('.bin')), '00000800'],
                                            check=True, capture_output=True)
            self.assertEqual(output.with_suffix('.bin').read_bytes(), observed)


class DecrementerVectorInputTest(unittest.TestCase):
    def extract(self, source, dol, observed):
        import external_vector
        with patch.object(external_vector, 'TEMPLATE_SHA256', hashlib.sha256(source).hexdigest()), \
             patch.object(external_vector, 'DECREMENTER_OBSERVED_SHA256', hashlib.sha256(observed).hexdigest()):
            return external_vector.extract_decrementer_vector(dol, observed)

    def test_observed_exception_eight_patch_is_the_only_accepted_template_change(self):
        dol, source, observed = synthetic_lineage(8)
        self.assertEqual(self.extract(source, dol, observed), observed)

    def test_wrong_exception_number_cannot_become_a_decrementer_vector(self):
        dol, source, observed = synthetic_lineage(4)
        with self.assertRaisesRegex(ValueError, 'lineage'):
            self.extract(source, dol, observed)

    def test_unrelated_source_change_cannot_become_a_decrementer_vector(self):
        dol, source, observed = synthetic_lineage(8)
        changed = bytearray(source)
        changed[12] ^= 1
        dol[256:] = changed
        with self.assertRaisesRegex(ValueError, 'lineage'):
            self.extract(changed, dol, observed)

    def test_every_changed_decrementer_byte_is_refused_even_with_a_matching_fixture_hash(self):
        dol, source, observed = synthetic_lineage(8)
        for offset in range(152):
            with self.subTest(offset=offset):
                changed = bytearray(observed)
                changed[offset] ^= 1
                with self.assertRaisesRegex(ValueError, 'lineage'):
                    self.extract(source, dol, bytes(changed))

    def test_validated_decrementer_input_uses_the_observed_base(self):
        import external_vector
        dol, source, observed = synthetic_lineage(8)
        with tempfile.TemporaryDirectory(dir=str(ROOT / 'build-f5'), prefix='dec-vector-input-') as directory:
            output = Path(directory) / 'generated.c'
            with patch.object(external_vector, 'TEMPLATE_SHA256', hashlib.sha256(source).hexdigest()), \
                 patch.object(external_vector, 'DECREMENTER_OBSERVED_SHA256', hashlib.sha256(observed).hexdigest()), \
                 patch('subprocess.run', return_value=subprocess.CompletedProcess([], 0, b'synthetic generated source')) as run:
                external_vector.emit_decrementer_vector(dol, observed, Path('fixture-emitter'), output)
                run.assert_called_once_with(['fixture-emitter', str(output.with_suffix('.bin')), '00000900'],
                                            check=True, capture_output=True)
            self.assertEqual(output.with_suffix('.bin').read_bytes(), observed)


class ExternalVectorDispatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        subprocess.run([str(test_host.OUT / 'external_vector_probe'), scenario], check=True)

    def test_generated_vector_executes_its_stores_and_real_rfi(self):
        self.probe('execute')

    def test_every_single_byte_mutation_is_refused_before_execution(self):
        for offset in range(152):
            with self.subTest(offset=offset):
                self.probe(f'corrupt-{offset}')

    def test_unaligned_vector_pc_is_refused(self):
        self.probe('unaligned')

    def test_translation_enabled_is_not_a_supported_physical_vector_entry(self):
        self.probe('translated')

    def test_existing_exception_is_not_consumed_as_a_vector_entry(self):
        self.probe('pending')

    def test_truncated_ram_is_refused_before_reading_the_reference_range(self):
        self.probe('short-ram')

    def test_zero_remaining_quantum_cannot_execute_vector_instructions(self):
        self.probe('zero-budget')

    def test_one_cycle_deadline_dispatches_resume_the_actual_next_instruction(self):
        self.probe('single-cycle')


class FpVectorDispatchTest(ExternalVectorDispatchTest):
    def probe(self, scenario):
        subprocess.run([str(test_host.OUT / 'external_vector_probe'), scenario, 'fp'], check=True)

    def test_real_fp_exception_is_consumed_only_after_verifying_the_vector_without_enabling_fp_or_changing_numeric_state(self):
        self.probe('resume-valid')

    def test_altered_fp_vector_cannot_consume_the_fp_exception(self):
        self.probe('resume-corrupt')

    def test_another_pending_exception_is_not_cleared_with_fp_unavailable(self):
        self.probe('resume-other-exception')


class DecrementerVectorDispatchTest(ExternalVectorDispatchTest):
    def probe(self, scenario):
        subprocess.run([str(test_host.OUT / 'external_vector_probe'), scenario, 'decrementer'], check=True)

if __name__ == '__main__':
    unittest.main()
