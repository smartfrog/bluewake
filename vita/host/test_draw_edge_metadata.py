import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('gen_edge_intercept_table', ROOT / 'scripts/gen_edge_intercept_table.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class DrawEdgeMetadataTest(unittest.TestCase):
    def members(self, text, observations=(0x80001000,), services=()):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'd_a_fixture.cpp').write_text(text)
            return generator.collect_draw_observations(observations, services, root)

    def test_class_is_generated_from_observed_actor_draw_entry_symbols(self):
        self.assertEqual(self.members('/* 80001000-80001020 .text daFixture_Draw__FPv */'),
                         [(0x80001000, 'daFixture_Draw')])

    def test_unobserved_actor_draw_is_not_a_member(self):
        self.assertEqual(self.members('/* 80002000-80002020 .text daFixture_Draw__FPv */'), [])

    def test_nondraw_actor_symbol_is_not_a_member(self):
        self.assertEqual(self.members('/* 80001000-80001020 .text daFixture_Execute__FPv */'), [])

    def test_draw_key_with_a_host_service_is_not_a_member(self):
        self.assertEqual(self.members('/* 80001000-80001020 .text daFixture_Draw__FPv */',
                                      services=(0x80001000,)), [])

    def test_ambiguous_draw_identity_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'ambiguous actor draw'):
            self.members('/* 80001000-80001020 .text daOne_Draw__FPv */\n'
                         '/* 80001000-80001020 .text daTwo_Draw__FPv */')

    def test_checked_in_table_matches_generator_and_enumerates_the_four_current_members(self):
        expected = [(0x800D8DB8, 'daBg_Draw'), (0x8015D80C, 'daSea_Draw'),
                    (0x8015E3F0, 'daVrbox_Draw'), (0x8015EA5C, 'daVrbox2_Draw')]
        observations = generator.cases((ROOT / generator.EDGE).read_text(), 'bluewake_edge_observation_requires_host')
        self.assertEqual(generator.collect_draw_observations(observations, (), ROOT / 'source-port/game/src/d/actor'), expected)
        metadata = generator.draw_metadata(expected)
        self.assertIn('\n'.join(metadata), (ROOT / 'runtime/host/src/edge_intercept_table.h').read_text())

    def test_empty_class_preserves_legacy_admissions_and_refuses_all_other_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'edge_admission.h').write_bytes((ROOT / 'vita/host/edge_admission.h').read_bytes())
            (root / 'edge_intercept_table.h').write_text('#include <stdbool.h>\n#include <stdint.h>\n'
                'typedef uint32_t u32;\n' + '\n'.join(generator.draw_metadata([])) + '\n')
            (root / 'probe.c').write_text('#include "edge_admission.h"\n#include <assert.h>\nint main(void) {\n'
                'assert(ww_edge_admit_observation(0x80120188u, 0x80120188u, 0x80120188u, true, false));\n'
                'assert(ww_edge_admit_observation(0x8022F9FCu, 0x8022F9FCu, 0u, true, false));\n'
                'const uint32_t keys[] = {0x800D8DB8u, 0x8015D80Cu, 0x8015E3F0u, 0x8015EA5Cu, 0x800A0B60u, 0x80322B20u, 0x80001000u};\n'
                'for (unsigned i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i)\n'
                'assert(!ww_edge_admit_observation(keys[i], keys[i], keys[i], true, false));\nreturn 0; }\n')
            subprocess.run([os.environ['CC'], '-std=c11', '-Wall', '-Wextra', '-Werror', str(root / 'probe.c'),
                            '-o', str(root / 'probe')], check=True)
            subprocess.run([str(root / 'probe')], check=True)


if __name__ == '__main__':
    unittest.main()
