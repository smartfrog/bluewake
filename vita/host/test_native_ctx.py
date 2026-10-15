import os
from pathlib import Path
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
