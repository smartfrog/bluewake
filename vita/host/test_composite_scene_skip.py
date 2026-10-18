import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts' / 'vita' / 'ww05'))
import composite_scene_skip as skip


class CompositeSceneSkipTest(unittest.TestCase):
    def fixture(self):
        return ('// DolRecomp output\n#include "../generated.h"\n\n'
                'label_80023238:\n    ctx->pc = 0x80023238u;\n'
                '    // 80023238: li      r3, 5\n'
                '    ctx->gpr[3] = (u32)(s32)(5);\n\n'
                'label_80023248:\n    ctx->pc = 0x80023248u;\n')

    def test_rewrites_only_the_scene_argument(self):
        out = skip.transform(self.fixture(), 'chunk.c')
        self.assertEqual(out.count('ctx->gpr[3] = (u32)(s32)(12);'), 1)
        self.assertEqual(out.count(skip.MARK), 1)
        self.assertEqual(out.count('label_80023248'), 1)
        self.assertIn('#include <stdio.h>\n', out)
        self.assertNotIn('(u32)(s32)(5);', out)

    def test_repeat_is_unchanged(self):
        once = skip.transform(self.fixture(), 'chunk.c')
        self.assertEqual(skip.transform(once, 'chunk.c'), once)

    def test_unprepared_chunk_is_rejected(self):
        with self.assertRaises(ValueError):
            skip.transform('#include "../generated.h"\nint x;\n', 'chunk.c')

    def test_duplicate_block_is_rejected(self):
        with self.assertRaises(ValueError):
            skip.transform(self.fixture() + skip.BEFORE, 'chunk.c')


if __name__ == '__main__':
    unittest.main()
