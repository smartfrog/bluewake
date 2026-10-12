import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STACK = Path('/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack')


class StaticModeTest(unittest.TestCase):
    def configure(self, static, vita=False, module_level=None):
        with tempfile.TemporaryDirectory(prefix='build-ww05-static-', dir=ROOT) as directory:
            output = Path(directory)
            source = output / 'generated'
            source.mkdir()
            for name in ('generated.h', 'generated_composite.h', 'module_tables.inc',
                         'rel_modules.inc', 'rel_data.inc'):
                (source / name).touch()
            (source / 'chunks_dol').mkdir()
            (source / 'chunks_dol/chunk.c').write_text('void chunk(void) {}\n')
            options = [f'-DCMAKE_C_COMPILER={os.environ["CC"]}', '-DCOMPOSITE_OPTIMIZATION_LEVEL=0']
            if vita:
                (output / 'ww05_identity.h').write_text('#define WW05_SOURCE "' + 'a' * 40 + '"\n')
                (output / 'ww05_dsp_init.h').write_text('static const unsigned char ww05_dsp_init_reference[128] = {0};\n')
                (output / 'ww05_external_vector.c').touch()
                (output / 'ww05_fp_vector.c').touch()
                (output / 'synthetic.dol').write_bytes(b'synthetic configuration input only')
                options = [f'-DCMAKE_TOOLCHAIN_FILE={STACK}/vitasdk/share/vita.toolchain.cmake',
                           f'-DRECOMPCORE_DIR={ROOT}/ref/vita-ww-01/RecompCore',
                           f'-DWW05_IDENTITY_DIR={output}', f'-DWW05_DOL={output}/synthetic.dol']
                if module_level is not None:
                    options.append(f'-DWW05_MODULE_OPT_LEVEL={module_level}')
            environment = {**os.environ, 'VITASDK': str(STACK / 'vitasdk')}
            subprocess.run([str(STACK / 'bin/cmake'), '-S', str(ROOT / ('vita/host' if vita else 'cmake/composite')),
                            '-B', str(output / 'build'), '-G', 'Ninja',
                            *options,
                            f'-DCMAKE_MAKE_PROGRAM={STACK}/bin/ninja',
                            '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                            f'-DCOMPOSITE_DIR={source}',
                            f'-DGXRUNTIME_DIR={ROOT}/ref/vita-ww-01/RecompCore/GXRuntime',
                            f'-DABI_DIR={ROOT}/ref/vita-ww-01/RecompCore/Source/Core/Core/PowerPC/StaticRecomp',
                            *(['-DBLUEWAKE_STATIC_MODULE=ON'] if static else [])],
                           check=True, capture_output=True, text=True, env=environment)
            commands = json.loads((output / 'build/compile_commands.json').read_text())
            return commands, (output / 'build/build.ninja').read_text()

    def test_static_module_borrows_the_hosts_cpu_owner(self):
        commands, ninja = self.configure(True)
        cpu = [entry for entry in commands if '/src/core/cpu' in entry['file']]
        self.assertEqual(cpu, [])
        self.assertIn('STATIC_LIBRARY_LINKER', ninja)

    def test_default_shared_module_keeps_all_six_cpu_sources(self):
        commands, ninja = self.configure(False)
        cpu = [entry for entry in commands if '/src/core/cpu' in entry['file']]
        self.assertEqual(len(cpu), 6)
        self.assertIn('SHARED_LIBRARY_LINKER', ninja)

    def test_private_vita_chunks_disable_the_costly_gcc_dominator_pass(self):
        commands, _ = self.configure(True, vita=True)
        chunks = [row for row in commands if row['file'].endswith('/chunks_dol/chunk.c')]
        self.assertEqual(len(chunks), 1)
        self.assertIn('-O1', chunks[0]['command'])
        self.assertIn('-fno-tree-dominator-opts', chunks[0]['command'])
        runtime = [row for row in commands if '/GXRuntime/src/' in row['file']]
        self.assertTrue(runtime)
        self.assertTrue(all('-fno-tree-dominator-opts' not in row['command'] for row in runtime))

    def test_vita_module_optimization_level_selects_the_chunk_codegen(self):
        commands, _ = self.configure(True, vita=True, module_level=3)
        chunks = [row for row in commands if row['file'].endswith('/chunks_dol/chunk.c')]
        self.assertEqual(len(chunks), 1)
        levels = re.findall(r'-O\d', chunks[0]['command'])
        self.assertEqual(levels[-1], '-O3')
        self.assertIn('-fno-tree-dominator-opts', chunks[0]['command'])

    def test_vita_boot_aggregator_compiles_the_pinned_dvd_through_the_existing_seam(self):
        commands, ninja = self.configure(True, vita=True)
        redirected = [row for row in commands if row['file'].endswith('/src/dvd.c') and 'dvd_seam.h' in row['command']]
        self.assertEqual(len(redirected), 1)
        self.assertIn('ww13_dvd_io', ninja)
        self.assertIn('ww13_dvd_pin', ninja)
        self.assertIn('-mcpu=cortex-a9', redirected[0]['command'])


if __name__ == '__main__':
    unittest.main()
