import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
import unittest

import test_host

BASE = 'bbd79f1208c6ca5668075de08c1a261e3a0e1500'
DIAGNOSTIC_MACROS = ('WW05_OBSERVE_NATIVE_CTX', 'WW05_DISPATCH_ABI_SENTINEL', 'WW05_REJECTION_CAPSULE')


class NativeContextOffTest(unittest.TestCase):
    def test_on_host_call_identity_preserves_the_lifecycle_and_rfi_contract(self):
        root = test_host.ROOT
        output = root / 'build-f5/native-ctx-identity'
        output.mkdir(parents=True, exist_ok=True)
        for path in test_host.OUT.iterdir():
            destination = output / path.name
            if path.name != 'rel_lifecycle_probe' and not destination.exists():
                destination.symlink_to(path)
        commands = json.loads((test_host.OUT / 'compile_commands.json').read_text())
        objects = []
        for name in ('boot_host.c', 'rel_lifecycle.c', 'disc_service.c', 'native_ctx_trace.c',
                     'native_ctx_identity_fixture.c'):
            row = next(row for row in commands if row['file'].endswith('/boot_host.c'))
            arguments = shlex.split(row['command'])
            arguments[arguments.index(row['file'])] = str(root / 'vita/host' / name)
            obj = output / (name + '.o')
            arguments[arguments.index('-o') + 1] = str(obj)
            subprocess.run([*arguments, '-DWW05_OBSERVE_NATIVE_CTX=1'], cwd=row['directory'], check=True)
            objects.append(str(obj))
        link = subprocess.check_output([str(test_host.STACK / 'bin/ninja'), '-t', 'commands',
                                        'rel_lifecycle_probe'], cwd=test_host.OUT, text=True).splitlines()[-1]
        arguments = shlex.split(link.removeprefix(': && ').removesuffix(' && :'))
        arguments[arguments.index('-o') + 1] = str(output / 'rel_lifecycle_probe')
        subprocess.run([arguments[0], *objects, *arguments[1:]], cwd=test_host.OUT, check=True)
        subprocess.run([sys.executable, '-B', str(root / 'vita/host/test_rel_lifecycle.py'),
                        '--build-dir', str(output), 'RelLifecycleTest'], check=True)

    def test_affected_preprocessed_paths_and_machine_code_are_unchanged(self):
        root = test_host.ROOT
        output = root / 'build-f5/native-ctx-off-proof'
        output.mkdir(parents=True, exist_ok=True)
        commands = json.loads((test_host.OUT / 'compile_commands.json').read_text())
        proof = {}
        for name in ('boot_host.c', 'rel_lifecycle.c', 'disc_service.c'):
            row = next(row for row in commands if row['file'].endswith('/' + name))
            arguments = shlex.split(row['command'])
            arguments = [value for value in arguments if value != '-c']
            index = arguments.index('-o')
            del arguments[index:index + 2]
            arguments.remove(row['file'])
            old = output / 'baseline' / name
            old.parent.mkdir(exist_ok=True)
            source = Path(row['file']).read_text()
            without_diagnostics = test_host.without_option_regions(source, DIAGNOSTIC_MACROS)
            for macro in DIAGNOSTIC_MACROS:
                self.assertNotIn(macro, without_diagnostics, name)
            self.assertNotEqual(source, without_diagnostics, name)
            old.write_text(without_diagnostics)
            arguments.extend(['-I' + str(root / 'vita/host'), '-g0', '-frandom-seed=ww117'])
            for label, source in (('baseline', old), ('off', Path(row['file']))):
                for suffix, mode in (('i', ['-E', '-P']), ('o', ['-c'])):
                    subprocess.run([*arguments, *mode, str(source), '-o', str(output / (label + '-' + name + '.' + suffix))],
                                   cwd=row['directory'], check=True)
            for suffix in ('i', 'o'):
                baseline = (output / ('baseline-' + name + '.' + suffix)).read_bytes()
                current = (output / ('off-' + name + '.' + suffix)).read_bytes()
                self.assertEqual(baseline, current, name + ' .' + suffix)
                proof[name + '.' + suffix] = hashlib.sha256(current).hexdigest()
        ninja = (test_host.OUT / 'build.ninja').read_text()
        self.assertNotIn('--wrap=ww_host_edge', ninja)
        self.assertNotIn('native_ctx_sentinel', ninja)
        self.assertFalse(any('native_ctx_' in row['file'] for row in commands))
        for symbol in ('ww_native_trace_enter', 'ww_native_host_call', '__wrap_sceIoPread',
                       'ww_abi_gate', 'ww_abi_init', 'ww_abi_dump'):
            symbols = subprocess.check_output(['nm', str(test_host.OUT / 'libww05_services.a')], text=True)
            self.assertNotIn(symbol, symbols)
        self.assertNotIn(b'dispatch-abi-capsule.bin', (test_host.OUT / 'libww05_services.a').read_bytes())
        (output / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')

    def test_dispatch_fixture_elf_output_and_serialized_cpu_are_identical(self):
        root = test_host.ROOT
        output = root / 'build-f5/native-ctx-dispatch-off'
        output.mkdir(parents=True, exist_ok=True)
        baseline = output / 'baseline'
        baseline.mkdir(exist_ok=True)
        for name in ('dispatch_loop.h', 'dispatch_loop.c'):
            (baseline / name).write_bytes(subprocess.check_output(
                ['git', 'show', BASE + ':cmake/composite/' + name], cwd=root))
        fixture = output / 'serialize.c'
        fixture.write_text('#include "dispatch_loop.h"\n#include <stdio.h>\n#include <string.h>\n'
                           'static int dispatch(CPUState* cpu, u32 pc) { cpu->pc=pc+4; cpu->downcount-=2; return 1; }\n'
                           'int main(void) { CPUState cpu; memset(&cpu,0,sizeof(cpu)); cpu.pc=0x802b0470; '
                           'cpu.gpr[4]=17; bluewake_chassis_dispatch_loop(&cpu,cpu.pc,dispatch,NULL,NULL); '
                           'return fwrite(&cpu,1,sizeof(cpu),stdout)==sizeof(cpu)?0:1; }\n')
        for label, directory in (('baseline', baseline), ('off', root / 'cmake/composite')):
            flags = ['-std=c11', '-O2', '-g0', '-frandom-seed=ww117', '-Wl,--build-id=none',
                     '-I' + str(directory), '-I' + str(root / 'cmake/composite'),
                     '-I' + str(root / 'ref/vita-ww-01/RecompCore/GXRuntime/include')]
            for name, source in (('fixture', root / 'tests/composite_dispatch_loop_test.c'), ('serialize', fixture)):
                executable = output / (label + '-' + name)
                subprocess.run(['/usr/bin/gcc', *flags, str(source), str(directory / 'dispatch_loop.c'),
                                '-o', str(executable)], check=True)
                (output / (label + '-' + name + '.stdout')).write_bytes(subprocess.check_output([str(executable)]))
        for name in ('fixture', 'serialize'):
            self.assertEqual((output / ('baseline-' + name)).read_bytes(), (output / ('off-' + name)).read_bytes())
            self.assertEqual((output / ('baseline-' + name + '.stdout')).read_bytes(),
                             (output / ('off-' + name + '.stdout')).read_bytes())
