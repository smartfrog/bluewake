import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts' / 'vita' / 'ww05'))
import composite_jaudio_defer as defer


class CompositeJaudioDeferTest(unittest.TestCase):
    def fixture(self):
        return ('// DolRecomp output\n#include "../generated.h"\n\n'
                'label_802AC6C0:\n    ctx->pc = 0x802AC6C0u;\n'
                '    cycle_block_prepaid = dolrecomp_block_can_precharge(ctx, 5u);\n')

    def test_guard_is_inserted_once(self):
        out = defer.transform(self.fixture(), 'chunk.c')
        self.assertEqual(out.count(defer.MARK), 1)
        self.assertEqual(out.count('mem_read32(ctx, ctx->gpr[13] - 27328u) == 0u'), 1)
        self.assertIn('ctx->pc = ctx->lr & ~3u;', out)
        self.assertIn('cycle_block_prepaid', out)

    def test_repeat_is_unchanged(self):
        once = defer.transform(self.fixture(), 'chunk.c')
        self.assertEqual(defer.transform(once, 'chunk.c'), once)

    def test_unprepared_is_rejected(self):
        with self.assertRaises(ValueError):
            defer.transform('#include "../generated.h"\nint x;\n', 'chunk.c')


if __name__ == '__main__':
    unittest.main()
