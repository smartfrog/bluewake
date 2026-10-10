import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STACK = Path('/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack')


class StaticModeTest(unittest.TestCase):
    def configure(self, static):
        with tempfile.TemporaryDirectory(prefix='build-ww05-static-', dir=ROOT) as directory:
            output = Path(directory)
            source = output / 'generated'
            source.mkdir()
            for name in ('generated.h', 'generated_composite.h', 'module_tables.inc',
                         'rel_modules.inc', 'rel_data.inc'):
                (source / name).touch()
            (source / 'chunks_dol').mkdir()
            (source / 'chunks_dol/chunk.c').write_text('void chunk(void) {}\n')
            subprocess.run([str(STACK / 'bin/cmake'), '-S', str(ROOT / 'cmake/composite'),
                            '-B', str(output / 'build'), '-G', 'Ninja',
                            f'-DCMAKE_C_COMPILER={os.environ["CC"]}',
                            f'-DCMAKE_MAKE_PROGRAM={STACK}/bin/ninja',
                            '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                            '-DCOMPOSITE_OPTIMIZATION_LEVEL=0',
                            f'-DCOMPOSITE_DIR={source}',
                            f'-DGXRUNTIME_DIR={ROOT}/ref/vita-ww-01/RecompCore/GXRuntime',
                            f'-DABI_DIR={ROOT}/ref/vita-ww-01/RecompCore/Source/Core/Core/PowerPC/StaticRecomp',
                            *(['-DBLUEWAKE_STATIC_MODULE=ON'] if static else [])],
                           check=True, capture_output=True, text=True)
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


if __name__ == '__main__':
    unittest.main()
