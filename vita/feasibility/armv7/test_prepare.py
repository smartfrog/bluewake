import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("prepare", ROOT / "scripts/vita/armv7/prepare.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class ExecutionSeamTest(unittest.TestCase):
    def test_only_cpu_include_changes(self):
        original = '#include "../src/cpu/cpu.h"\nint main(void) { return 42; }\n'
        self.assertEqual(prepare.execution_source(original),
                         '#include "core/cpu.h"\nint main(void) { return 42; }\n')

    def test_changed_include_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "CPU include changed"):
            prepare.execution_source('#include "unexpected.h"\n')


class CorpusSplitTest(unittest.TestCase):
    def test_chunk_bodies_are_preserved(self):
        body = 'void func_80004000(CPUState* ctx) {\n    return;\n}\n\n'
        source = '#ifndef RECOMP_GENERATED_H\n' + body + '#define DOLRECOMP_ENTRY_POINT 0x80004000u\n'
        header, chunks = prepare.split_corpus(source)
        self.assertEqual(chunks, [(0x80004000, body)])
        self.assertIn('void func_80004000(CPUState* ctx);', header)

    def test_loop_helper_stays_with_its_chunk(self):
        first = 'void func_80004000(CPUState* ctx) {\n    return;\n}\n\n'
        second = ('static void loop_80004020(CPUState* ctx) {\n    return;\n}\n\n'
                  'void func_80004020(CPUState* ctx) {\n    loop_80004020(ctx);\n}\n\n')
        source = '#ifndef RECOMP_GENERATED_H\n' + first + second + '#define DOLRECOMP_ENTRY_POINT 0x80004000u\n'
        _, chunks = prepare.split_corpus(source)
        self.assertEqual(chunks, [(0x80004000, first), (0x80004020, second)])


if __name__ == "__main__":
    unittest.main()
