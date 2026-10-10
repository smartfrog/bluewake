#!/usr/bin/env python3
"""Private, opt-in Wind Waker ARMv7 build. Never invokes Vita3K or downloads tools."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[3]
STACK = Path('/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack')
OUTPUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05')
PINS = {'RecompCore': 'ddd031b2a7f45fcbda37a49daac9b308b8d95da1',
        'DolRecomp': 'b8b534591cba8ca7cd43943a655ee6e2591cf5de'}
COMPOSITE_DIGEST = '54f54434c3f9c899d43a96373dc0b4c1aed0e50db8b820b9698dfa76571a770a'

sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))
from build import checked_sources as checked_pinned_sources


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_disc(disc, extractor, output):
    if 'BLUEWAKE_ACCEPT_DOL_SHA1' in os.environ:
        raise ValueError('disc hash override is forbidden')
    result = subprocess.run([str(extractor), str(disc), str(output)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode:
        raise RuntimeError('disc verification failed: ' + result.stdout)
    if len(list((output / 'rels').glob('*.rel'))) != 415:
        raise ValueError('expected exactly 415 verified RELs')
    return result.stdout


def checked_sources():
    sources = checked_pinned_sources()
    return {'RecompCore': sources['recompcore'], 'DolRecomp': sources['dolrecomp']}


def checked_output(output):
    output = output.resolve()
    if output != OUTPUT:
        raise ValueError(f'private outputs must stay in {OUTPUT}')
    subprocess.run(['git', '-C', str(OUTPUT.parent.parent), 'check-ignore',
                    str(output / 'private/main.dol'), str(output / 'vita/ww05.vpk')],
                   check=True, capture_output=True)
    return output


def preserve_artifacts(vita, output):
    snapshot = output / ('artifacts-' + uuid.uuid4().hex)
    snapshot.mkdir(mode=0o700)
    artifacts = {}
    for name in ('ww05_host', 'ww05_host.velf', 'eboot.bin', 'ww05.vpk_param.sfo', 'ww05.vpk'):
        original = vita / name
        destination = snapshot / name
        expected = sha256(original)
        with original.open('rb') as source, destination.open('xb') as target:
            shutil.copyfileobj(source, target)
        if sha256(destination) != expected or sha256(original) != expected:
            raise ValueError('artifact changed while preserving build: ' + name)
        artifacts[str(destination)] = {'bytes': destination.stat().st_size, 'sha256': expected}
    return artifacts


class Build:
    def __init__(self, output):
        self.output = output
        self.logs = output / 'logs'
        self.logs.mkdir(parents=True, exist_ok=True)

    def run(self, name, command):
        command = [str(value) for value in command]
        index = 1
        while (self.logs / f'{name}-{index}.log').exists():
            index += 1
        path = self.logs / f'{name}-{index}.log'
        started = time.monotonic()
        with path.open('x') as log:
            log.write(shlex.join(command) + '\n')
            log.flush()
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        path.with_suffix('.json').write_text(json.dumps({'command': command, 'status': result.returncode,
                                                       'seconds': time.monotonic() - started}) + '\n')
        if result.returncode:
            raise RuntimeError(f'{name} failed ({result.returncode}): {path}')
        print('PASS ' + name, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=OUTPUT)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 4:
        parser.error('jobs must be between one and four')
    output = checked_output(args.out)
    sources = checked_sources()
    build = Build(output)
    cc = STACK / 'bin/aarch64-conda-linux-gnu-gcc'
    cmake = STACK / 'bin/cmake'
    ninja = STACK / 'bin/ninja'
    extractor = output / 'disc_extract'
    build.run('extractor-build', [cc, '-O2', '-Iapple/ios/src', 'scripts/ios/disc_extract.c',
                                  'apple/ios/src/disc_import.c', '-o', extractor])
    private = output / 'private'
    if not private.exists():
        (build.logs / 'disc-verify.log').write_text(verify_disc(args.disc, extractor, private))
    else:
        receipt = json.loads((output / 'inputs.json').read_text())
        if receipt['disc_sha256'] != sha256(args.disc) or receipt['dol_sha256'] != sha256(private / 'main.dol'):
            raise ValueError('private input changed; preserve evidence and stop')
    (output / 'inputs.json').write_text(json.dumps({'disc_sha256': sha256(args.disc),
                                                  'dol_sha256': sha256(private / 'main.dol'),
                                                  'pins': PINS}, indent=2) + '\n')
    generator = output / 'generator'
    build.run('generator-configure', [cmake, '-S', sources['DolRecomp'], '-B', generator, '-G', 'Ninja',
                                     f'-DCMAKE_C_COMPILER={cc}', f'-DCMAKE_MAKE_PROGRAM={ninja}',
                                      '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON'])
    build.run('generator-build', [cmake, '--build', generator, '--target', 'dolrecomp', '-j', args.jobs])
    translated = output / 'translated'
    composite = output / 'composite'
    if not composite.exists():
        build.run('translate-dol', [generator / 'dolrecomp', '--gamecube', '--backend', 'c', '--cpu', 'gekko',
                                    '--partition-instructions', '4096', private / 'main.dol',
                                    translated / 'dol', '-j', args.jobs])
        build.run('translate-rels', [generator / 'dolrecomp', '--gamecube', '--backend', 'c', '--cpu', 'gekko',
                                     '--rel-base', '0xC0400000', private / 'rels', translated / 'rels', '-j', args.jobs])
        build.run('generate-composite', [sys.executable, '-B', 'scripts/generate_composite.py',
                                         '--dol-dir', translated / 'dol/generated',
                                         '--rels-dir', translated / 'rels/generated/rels',
                                         '--rels-bin-dir', private / 'rels', '--main-dol', private / 'main.dol',
                                         '--output-dir', composite])
    digest = subprocess.check_output([sys.executable, '-B', ROOT / 'scripts/ios/composite_manifest.py', composite],
                                     text=True).split()[0]
    if digest != COMPOSITE_DIGEST:
        raise ValueError(f'composite digest differs from verified tree: {digest}')
    if args.prepare_only:
        print('PREPARED PRIVATE INPUTS ONLY; Vita3K NOT RUN')
        return 0
    source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    identity = output / 'identity'
    identity.mkdir(exist_ok=True)
    (identity / 'ww05_identity.h').write_text(f'#define WW05_SOURCE "{source}"\n')
    os.environ['VITASDK'] = str(STACK / 'vitasdk')
    vita = output / 'vita'
    build.run('vita-configure', [cmake, '-S', ROOT / 'vita/host', '-B', vita, '-G', 'Ninja',
                                 f'-DCMAKE_TOOLCHAIN_FILE={STACK}/vitasdk/share/vita.toolchain.cmake',
                                 f'-DCMAKE_MAKE_PROGRAM={ninja}', '-DCMAKE_BUILD_TYPE=Release',
                                 f'-DRECOMPCORE_DIR={sources["RecompCore"]}', f'-DCOMPOSITE_DIR={composite}',
                                 f'-DWW05_IDENTITY_DIR={identity}', f'-DWW05_DOL={private}/main.dol'])
    build.run('vita-build', [cmake, '--build', vita, '-j', args.jobs])
    from inspect_build import inspect
    inspect(vita)
    artifacts = preserve_artifacts(vita, output)
    snapshot = Path(next(iter(artifacts))).parent
    for path in (output / 'footprint.json', vita / 'ww05.map', vita / 'compile_commands.json'):
        shutil.copyfile(path, snapshot / path.name)
    receipt = json.dumps({'source': source, 'pins': PINS,
        'composite_digest': digest, 'title': 'BWWG00005', 'method': 'startup-gx-v1',
        'artifacts': artifacts, 'config': {'static_module': True, 'aurora': False, 'aurora_recomp': False,
        'optimization': 1, 'compiler_pass_disabled': 'tree-dominator-opts', 'fp_patch': None},
        'footprint_sha256': sha256(output / 'footprint.json'),
        'snapshot': str(snapshot)}, indent=2) + '\n'
    (snapshot / 'build-evidence.json').write_text(receipt)
    (output / 'build-evidence.json').write_text(receipt)
    checked_sources()
    print('PRIVATE BUILD ONLY; Vita3K NOT RUN')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
