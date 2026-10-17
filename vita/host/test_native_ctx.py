import os
from pathlib import Path
import re
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build-f5/native-ctx-tests'


class NativeContextTraceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-Wall', '-Wextra',
                        '-Werror', '-I' + str(ROOT / 'vita/host'),
                        str(ROOT / 'vita/host/native_ctx_trace.c'),
                        str(ROOT / 'vita/host/native_ctx_probe.c'), '-o', str(OUT / 'probe')],
                       check=True)

    def test_trace_scenarios(self):
        for scenario in ('preserve', 'clobber', 'ctx-one', 'nesting', 'overflow', 'ring',
                         'pinning', 'short-write', 'full-disk', 'recursion', 'concurrency',
                         'interrupted', 'unarmed', 'markers'):
            with self.subTest(scenario=scenario):
                subprocess.run([str(OUT / 'probe'), scenario], check=True)


class DispatchAbiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-Wall', '-Wextra',
                        '-Werror', '-I' + str(ROOT / 'vita/host'),
                        str(ROOT / 'vita/host/native_ctx_sentinel.c'),
                        str(ROOT / 'vita/host/native_ctx_sentinel_probe.c'),
                        '-o', str(OUT / 'sentinel-probe')], check=True)

    def test_dispatch_boundaries(self):
        for scenario in ('ctx-one', 'result-one', 'target', 'thumb', 'sp', 'entry-sp', 'register',
                         'vfp', 'ring', 'short-write', 'failed-write', 'unarmed', 'overflow',
                         'short-active', 'short-complete', 'history-first', 'history-wrap', 'reentry',
                         'internal-classifier', 'internal-capsule', 'internal-lr', 'real-mismatch',
                         'internal-absent'):
            with self.subTest(scenario=scenario):
                subprocess.run([str(OUT / 'sentinel-probe'), scenario], check=True)

    def test_dispatch_stop_uses_the_classified_reason(self):
        source = (ROOT / 'vita/host/boot_host.c').read_text()
        self.assertIn('ww_abi_stop_reason(ww_abi_dump.failure, ww_abi_dump.io_error)', source)

    @unittest.skipUnless(shutil.which('arm-vita-eabi-gcc'), 'Vita SDK is required')
    def test_history_copy_has_no_arm_libcall(self):
        obj = OUT / 'sentinel-vita.o'
        subprocess.run(['arm-vita-eabi-gcc', '-std=gnu11', '-O3', '-mcpu=cortex-a9',
                        '-mfpu=neon', '-mfloat-abi=hard', '-mthumb', '-ffp-contract=off',
                        '-fno-fast-math', '-Wall', '-Wextra', '-Werror',
                        '-DWW05_DISPATCH_ABI_SENTINEL=1', '-c',
                        str(ROOT / 'vita/host/native_ctx_sentinel.c'), '-o', str(obj)], check=True)
        assembly = subprocess.check_output(['arm-vita-eabi-objdump', '-dr',
                                            '--disassemble=ww_abi_finish', str(obj)], text=True)
        (OUT / 'sentinel-vita-finish.txt').write_text(assembly)
        self.assertNotIn('memcpy', assembly)
        self.assertRegex(assembly, r'\bldrb(?:\.w)?\s')
        self.assertRegex(assembly, r'\bstrb(?:\.w)?\s')
        self.assertIn('#240', assembly)
        calls = re.findall(r'\bbl(?:x|\.w)?\s+[^<\n]*<([^>]+)>', assembly)
        self.assertEqual([name for name in calls if not name.startswith('stop')],
                         ['memcmp', 'memcmp'])
