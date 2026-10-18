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
OUTPUT = Path(os.environ.get('WW05_BUILD_ROOT', '/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05'))
if not OUTPUT.is_absolute():
    raise ValueError('WW05_BUILD_ROOT must be an absolute path')
PINS = {'RecompCore': 'ddd031b2a7f45fcbda37a49daac9b308b8d95da1',
        'DolRecomp': 'b8b534591cba8ca7cd43943a655ee6e2591cf5de'}
COMPOSITE_DIGEST = 'afa9cfb7778995378b422d0138409c6fa90235ec66be122ad24dac45872a80c0'
COMPOSITE_CANON_MARKER = '#define DOLRECOMP_CANON_CTX'

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


def checked_composite(composite):
    header = composite / 'generated_composite.h'
    if not header.is_file():
        raise ValueError(f'composite header is missing: {header}')
    if COMPOSITE_CANON_MARKER not in header.read_text():
        raise ValueError('composite predates DOLRECOMP_CANON_CTX; regenerate it')
    digest = subprocess.check_output([sys.executable, '-B', ROOT / 'scripts/ios/composite_manifest.py',
                                      composite], text=True).split()[0]
    if digest != COMPOSITE_DIGEST:
        raise ValueError(f'composite digest differs from verified tree: {digest}')
    return digest


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


def dst_alpha_configure_option(enabled):
    return ['-DWW_RENDERER_DST_ALPHA_ADMIT=ON'] if enabled else []


def edge_observation_admit_configure_option(enabled):
    return [f'-DWW_EDGE_OBSERVATION_ADMIT={"ON" if enabled else "OFF"}']


def idle_deadline_advance_configure_option(enabled):
    return [f'-DWW_IDLE_DEADLINE_ADVANCE={"ON" if enabled else "OFF"}']


def checked_turn_cap(turn_cap):
    if isinstance(turn_cap, bool) or not isinstance(turn_cap, int) or turn_cap < 0:
        raise ValueError('turn cap must be zero or a positive integer')
    return turn_cap


def turn_cap_configure_option(turn_cap):
    return [] if checked_turn_cap(turn_cap) == 0 else [f'-DWW_TURN_CAP={turn_cap}']


def program_cache_configure_option(enabled):
    return ['-DWW_PROGRAM_CACHE=ON'] if enabled else []


def rejection_capsule_configure_option(enabled):
    return [f'-DWW05_REJECTION_CAPSULE={"ON" if enabled else "OFF"}']


def skip_title_logo_configure_option(enabled):
    return [f'-DWW_SKIP_TITLE_LOGO={"ON" if enabled else "OFF"}']


def experimental_configure_options():
    return ['-DWW05_DISPATCH_ABI_SENTINEL=OFF', '-DWW05_OBSERVE_NATIVE_CTX=OFF',
            '-DWW05_QUIET_LOGGING=OFF', '-DWW05_GLOBAL_CTX=OFF']


def vita_configure_arguments(cmake, ninja, source, output, toolchain, recompcore, composite, identity, dol,
                             vitagl, renderer_sdk, opt_level, vi_clock, pad_input, dst_alpha_admit,
                             edge_observation_admit, idle_deadline_advance, turn_cap, program_cache,
                             rejection_capsule, skip_title_logo):
    return [
        cmake, '-S', source, '-B', output, '-G', 'Ninja',
        f'-DCMAKE_TOOLCHAIN_FILE={toolchain}',
        f'-DCMAKE_MAKE_PROGRAM={ninja}', '-DCMAKE_BUILD_TYPE=Release',
        f'-DRECOMPCORE_DIR={recompcore}', f'-DCOMPOSITE_DIR={composite}',
        f'-DWW05_IDENTITY_DIR={identity}', f'-DWW05_DOL={dol}',
        f'-DWW05_VITAGL_LIBRARY={vitagl}', f'-DWW05_RENDERER_SDK={renderer_sdk}',
        f'-DWW05_MODULE_OPT_LEVEL={opt_level}',
        f'-DWW05_ENABLE_VI_CLOCK={"ON" if vi_clock else "OFF"}',
        f'-DWW05_ENABLE_PAD_INPUT={"ON" if pad_input else "OFF"}',
        *dst_alpha_configure_option(dst_alpha_admit),
        *edge_observation_admit_configure_option(edge_observation_admit),
        *idle_deadline_advance_configure_option(idle_deadline_advance),
        *turn_cap_configure_option(turn_cap),
        *program_cache_configure_option(program_cache),
        *rejection_capsule_configure_option(rejection_capsule),
        *skip_title_logo_configure_option(skip_title_logo),
        *experimental_configure_options(),
    ]


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=OUTPUT)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--vi-clock', action=argparse.BooleanOptionalAction, default=True,
                        help='guest-cycle VI clock (on by default; --no-vi-clock disables it)')
    parser.add_argument('--pad-input', action='store_true', help='enable experimental Vita controls through guest SI (off by default)')
    parser.add_argument('--dst-alpha-admit', action=argparse.BooleanOptionalAction, default=True,
                        help='admit the non-conflict destination-alpha mono-pass (on by default)')
    parser.add_argument('--edge-observation-admit', action=argparse.BooleanOptionalAction, default=True,
                        help='admit the module-owned GroundCross caller-return observation edge (on by default)')
    parser.add_argument('--idle-deadline-advance', action=argparse.BooleanOptionalAction, default=True,
                        help='let a recognised scheduler-idle turn run to the next device deadline (needs the VI clock; on by default)')
    parser.add_argument('--turn-cap', type=int, default=4096,
                        help='cap scheduler turns per host frame (default 4096; zero keeps the CMake default)')
    parser.add_argument('--program-cache', action=argparse.BooleanOptionalAction, default=True,
                        help='translated-program cache (on by default)')
    parser.add_argument('--rejection-capsule', action=argparse.BooleanOptionalAction, default=True,
                        help='bounded admission-rejection capsule (on by default)')
    parser.add_argument('--skip-title-logo', action='store_true',
                        help='diagnostic: open the file-select menu instead of the title logo and movie (off by default)')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--opt-level', choices=('1', '2', '3'), default='1',
                        help='translated module optimization level (default matches the verified private module)')
    parser.add_argument('--external-vector', type=Path, default=OUTPUT / 'private/external-vector.bin',
                        help='exact private vector captured from the documented guest trial')
    parser.add_argument('--fp-vector', type=Path, default=OUTPUT / 'private/fp-vector.bin',
                         help='exact private FP-unavailable vector captured from the documented guest trial')
    parser.add_argument('--decrementer-vector', type=Path, default=OUTPUT / 'private/decrementer-vector.bin',
                        help='exact private decrementer vector captured from the documented guest trial')
    return parser


def parse_arguments(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.jobs < 1 or args.jobs > 4:
        parser.error('jobs must be between one and four')
    try:
        args.turn_cap = checked_turn_cap(args.turn_cap)
    except ValueError as error:
        parser.error(str(error))
    return args


def evidence_config(args):
    return {'static_module': True, 'aurora': False, 'aurora_recomp': True,
            'optimization': int(args.opt_level), 'compiler_pass_disabled': 'tree-dominator-opts',
            'fp_patch': None, 'lazy_fp': True, 'fp_exception_suffix_refund': True,
            'vi_clock': args.vi_clock, 'pad_input': args.pad_input, 'pad_sampling': 'guest-SI-only',
            'dst_alpha_admit': args.dst_alpha_admit,
            'edge_observation_admit': args.edge_observation_admit,
            'idle_deadline_advance': args.idle_deadline_advance,
            'turn_cap': args.turn_cap, 'program_cache': args.program_cache,
            'rejection_capsule': args.rejection_capsule,
            'dispatch_abi_sentinel': False, 'observe_native_ctx': False,
            'quiet_logging': False, 'global_ctx': False,
            'skip_title_logo': args.skip_title_logo}


def main():
    args = parse_arguments()
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
                                         '--ship-disc', args.disc,
                                         '--output-dir', composite])
    digest = checked_composite(composite)
    if args.prepare_only:
        print('PREPARED PRIVATE INPUTS ONLY; Vita3K NOT RUN')
        return 0
    source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    identity = output / 'identity'
    identity.mkdir(exist_ok=True)
    (identity / 'ww05_identity.h').write_text(f'#define WW05_SOURCE "{source}"\n')
    from dsp_ucode import INIT_UCODE_SHA256, extract_init_ucode
    init_code = extract_init_ucode((private / 'main.dol').read_bytes())
    init_header = identity / 'ww05_dsp_init.h'
    init_header.write_text('static const unsigned char ww05_dsp_init_reference[128] = {' +
                            ','.join(str(value) for value in init_code) + '};\n')
    from external_vector import (OBSERVED_SHA256, FP_OBSERVED_SHA256, DECREMENTER_OBSERVED_SHA256,
                                 TEMPLATE_SHA256, emit_external_vector, emit_fp_vector, emit_decrementer_vector)
    vector_generator = output / 'vector-generator'
    build.run('vector-generator-configure', [cmake, '-S', ROOT / 'vita/feasibility/armv7/generator',
        '-B', vector_generator, '-G', 'Ninja', f'-DCMAKE_C_COMPILER={cc}',
        f'-DCMAKE_MAKE_PROGRAM={ninja}', f'-DDOLRECOMP_DIR={sources["DolRecomp"]}',
        '-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON'])
    build.run('vector-generator-build', [cmake, '--build', vector_generator,
        '--target', 'ww_vector_emit', '-j', args.jobs])
    vectors = {}
    for name, path, address, expected, emit in (
            ('external_vector', args.external_vector, '00000500', OBSERVED_SHA256, emit_external_vector),
            ('fp_vector', args.fp_vector, '00000800', FP_OBSERVED_SHA256, emit_fp_vector),
            ('decrementer_vector', args.decrementer_vector, '00000900', DECREMENTER_OBSERVED_SHA256,
             emit_decrementer_vector)):
        observed = path.read_bytes()
        vector_source = identity / f'ww05_{name}.c'
        emit((private / 'main.dol').read_bytes(), observed, vector_generator / 'ww_vector_emit', vector_source)
        vectors[name] = {'address': address, 'bytes': len(observed), 'sha256': expected,
                         'template_sha256': TEMPLATE_SHA256, 'private_source_sha256': sha256(vector_source)}
    os.environ['VITASDK'] = str(STACK / 'vitasdk')
    vita = output / 'vita'
    vitagl = Path('/home/fred/cloudpunk-vita/tools/vita3k/vitagl-source/libvitaGL.a')
    renderer_sdk = Path('/home/fred/cloudpunk-vita/tools/vitasdk/arm-vita-eabi')
    vitagl_sha256 = sha256(vitagl)
    build.run('vita-configure', vita_configure_arguments(
        cmake, ninja, ROOT / 'vita/host', vita, f'{STACK}/vitasdk/share/vita.toolchain.cmake',
        sources['RecompCore'], composite, identity, f'{private}/main.dol', vitagl, renderer_sdk,
        args.opt_level, args.vi_clock, args.pad_input, args.dst_alpha_admit, args.edge_observation_admit,
        args.idle_deadline_advance, args.turn_cap, args.program_cache, args.rejection_capsule,
        args.skip_title_logo))
    build.run('vita-build', [cmake, '--build', vita, '-j', args.jobs])
    from inspect_build import inspect
    inspect(vita)
    if sha256(vitagl) != vitagl_sha256:
        raise ValueError('no-splash vitaGL archive changed during build')
    artifacts = preserve_artifacts(vita, output)
    snapshot = Path(next(iter(artifacts))).parent
    for path in (output / 'footprint.json', vita / 'ww05.map', vita / 'compile_commands.json',
                 vita / 'ww05_pad_input.json'):
        shutil.copyfile(path, snapshot / path.name)
    shutil.copyfile(init_header, snapshot / init_header.name)
    for name in vectors:
        for suffix in ('.c', '.bin'):
            vector_path = identity / f'ww05_{name}{suffix}'
            shutil.copyfile(vector_path, snapshot / vector_path.name)
    receipt = json.dumps({'source': source, 'pins': PINS,
        'renderer': {'vitagl': str(vitagl), 'vitagl_sha256': vitagl_sha256, 'sdk': str(renderer_sdk)},
        'dsp_init': {'bytes': len(init_code), 'sha256': INIT_UCODE_SHA256,
                      'private_header_sha256': sha256(init_header)},
        **vectors,
        'composite_digest': digest, 'title': 'BWWG00005', 'method': 'startup-gx-v1',
        'artifacts': artifacts, 'config': evidence_config(args),
        'footprint_sha256': sha256(output / 'footprint.json'),
        'snapshot': str(snapshot)}, indent=2) + '\n'
    (snapshot / 'build-evidence.json').write_text(receipt)
    (output / 'build-evidence.json').write_text(receipt)
    checked_sources()
    print('PRIVATE BUILD ONLY; Vita3K NOT RUN')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
