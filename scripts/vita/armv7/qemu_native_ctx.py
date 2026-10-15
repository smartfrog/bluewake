#!/usr/bin/env python3
"""Build and watch game-free Cortex-A9 tests of the actual native-context shims."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c11', '-O2', '-ffreestanding', '-fno-builtin', '-mcpu=cortex-a9',
             '-mfpu=neon', '-mfloat-abi=hard', '-Wall', '-Wextra', '-Werror',
             '-I' + str(ROOT / 'vita/host')]
    sources = ['vita/feasibility/armv7/qemu_startup.S', 'vita/host/native_ctx_shims.S',
               'vita/host/native_ctx_trace.c', 'vita/host/native_ctx_probe.c',
               'vita/host/native_ctx_fixture.S', 'vita/host/native_ctx_fixture.c']
    objects = []
    with (args.out / 'build.log').open('w') as log:
        for source in sources:
            obj = args.out / (Path(source).name + '.o')
            extra = ['-Dmain=ww_native_probe_main'] if source.endswith('native_ctx_probe.c') else []
            if args.baseline and source.endswith('native_ctx_fixture.c'):
                extra.append('-DWW_NATIVE_BASELINE=1')
            subprocess.run(['arm-none-eabi-gcc', *flags, *extra, '-c', str(ROOT / source),
                            '-o', str(obj)], check=True, stdout=log, stderr=subprocess.STDOUT)
            objects.append(str(obj))
        elf = args.out / 'native-ctx.elf'
        subprocess.run(['arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mfpu=neon', '-mfloat-abi=hard',
                        '-nostdlib', '-static', '-Wl,-Ttext=0x60000000', '-e', '_start',
                        *objects, '-lgcc', '-o', str(elf)], check=True, stdout=log, stderr=subprocess.STDOUT)
    with (args.out / 'disassembly.txt').open('w') as stream:
        subprocess.run(['arm-none-eabi-objdump', '-d', str(elf)], check=True, stdout=stream)
    command = ['qemu-system-aarch64', '-M', 'vexpress-a9', '-cpu', 'cortex-a9', '-m', '128M',
               '-display', 'none', '-serial', 'none', '-monitor', 'none',
               '-semihosting-config', 'enable=on,target=native', '-kernel', str(elf)]
    started = time.monotonic()
    watch = []
    with (args.out / 'qemu.log').open('w') as log:
        process = subprocess.Popen(command, stdout=log, stderr=log,
                                   env={**os.environ, 'QEMU_AUDIO_DRV': 'none'})
        try:
            while process.poll() is None:
                elapsed = time.monotonic() - started
                try:
                    stat = Path('/proc', str(process.pid), 'stat').read_text().split(') ', 1)[1].split()
                    watch.append({'seconds': elapsed, 'cpu_ticks': int(stat[11]) + int(stat[12]),
                                  'log_bytes': (args.out / 'qemu.log').stat().st_size})
                except FileNotFoundError:
                    break
                if elapsed > 10:
                    raise TimeoutError('game-free QEMU fixture exceeded 10 seconds')
                time.sleep(0.05)
            result = process.wait(timeout=1)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            (args.out / 'watch.json').write_text(json.dumps(watch, indent=2) + '\n')
    output = (args.out / 'qemu.log').read_text()
    print(output, end='')
    return 0 if result == 0 and 'NATIVE-CTX fixtures PASS\n' in output else 1


if __name__ == '__main__':
    raise SystemExit(main())
