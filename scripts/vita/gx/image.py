#!/usr/bin/env python3
"""Render a private captured GX fixture through vitaGL and retain a visible PNG."""

import argparse
from collections import Counter
import html
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import build as gx_build
import extract
import launch
import memory_snapshot

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD = ROOT / 'build-f6/ww18'
sys.path.insert(0, str(ROOT / 'vita/feasibility/gx'))
from test_png_capture import parse_png


def validate_fixture(directory, metadata):
    fifo = (directory / 'stream.fifo').read_bytes()
    if memory_snapshot.sha256(fifo) != metadata['fifo_sha256']:
        raise ValueError('fixture FIFO SHA-256 mismatch')
    if metadata['mem1_sha256'] is not None:
        memory = (directory / 'mem1.bin').read_bytes()
        if len(memory) > memory_snapshot.MEM1_BYTES or memory_snapshot.sha256(memory) != metadata['mem1_sha256']:
            raise ValueError('fixture MEM1 SHA-256/bounds mismatch')
    if metadata.get('snapshot'):
        if memory_snapshot.sha256((directory / 'mem1.ranges').read_bytes()) != metadata['snapshot']['ranges_sha256']:
            raise ValueError('fixture snapshot coverage SHA-256 mismatch')


def build_replay(build):
    source = gx_build.find_recompcore()
    gx_build.verify_source(source, gx_build.PINS['recompcore'])
    sdk = launch.CLOUDPUNK_STACK / 'vitasdk'
    directory = build / 'image-vita'
    environment = dict(os.environ, VITASDK=str(sdk))
    subprocess.run(['cmake', '-G', 'Ninja', '-S', str(ROOT / 'vita/renderer'), '-B', str(directory),
                    f'-DCMAKE_TOOLCHAIN_FILE={sdk}/share/vita.toolchain.cmake',
                    f'-DRECOMPCORE_DIR={source}', f'-DWW02_VITAGL_LIBRARY={launch.VITAGL_NO_SPLASH}',
                    '-DCMAKE_BUILD_TYPE=Release'], env=environment, check=True)
    subprocess.run(['cmake', '--build', str(directory), '--target', 'gx_image_replay.vpk', '-j', '2'],
                   env=environment, check=True)
    return directory / 'gx_image_replay.vpk'


def run_replay(fixture, build):
    vpk = build_replay(build)
    result = subprocess.run([sys.executable, str(ROOT / 'scripts/vita/gx/launch.py'),
                             '--output', str(build), '--vpk', str(vpk), '--fixture', str(fixture),
                             '--watched', '--surface-sync', '--share-readonly-stack', '--timeout', '120'],
                            text=True, capture_output=True)
    if not result.stdout.strip():
        raise ValueError(f'image launcher failed: {result.stderr}')
    launched = json.loads(result.stdout)
    root = build / ('vita3k-run-' + launched['run'])
    evidence = root / 'evidence'
    report = (evidence / 'report.txt').read_text() if (evidence / 'report.txt').is_file() else result.stderr
    capture = evidence / 'capture.png' if (evidence / 'capture.png').is_file() else None
    raw_capture = evidence / 'capture-rgba.png' if (evidence / 'capture-rgba.png').is_file() else None
    return dict(ok=result.returncode == 0 and launched['ok'], report=report, capture=capture, raw_capture=raw_capture,
                evidence=json.loads((evidence / 'result.json').read_text()), run_directory=str(root),
                surface_sync_config_sha256=launch.sha256(root / 'config/Vita3K/config.yml'))


def png_metadata(path):
    width, height, depth, color, pixels = parse_png(path)
    if (depth, color) != (8, 6) or len(pixels) != width * height * 4:
        raise ValueError('capture is not complete RGBA8 PNG data')
    colors = Counter(pixels[n:n + 4] for n in range(0, len(pixels), 4))
    return dict(path=path.name, sha256=launch.sha256(path), width=width, height=height,
                row_order='top-first', distinct_rgba=len(colors),
                alpha_nonzero=sum(count for rgba, count in colors.items() if rgba[3]),
                most_common=[dict(rgba=list(rgba), pixels=count) for rgba, count in colors.most_common(8)],
                pixel_status='uniform-readback' if len(colors) == 1 else 'nonuniform-readback')


def write_manifest(output, manifest):
    (output / 'image.json').write_text(json.dumps(manifest, indent=2) + '\n')
    description = html.escape(manifest['verdict'])
    image = '<img src="capture.png" alt="Actual vitaGL framebuffer readback">' if manifest['image'] else ''
    (output / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Private GX replay</title>'
                                     f'<h1>{description}</h1>{image}'
                                     '<p>Framebuffer evidence, not game or pixel parity certification. '
                                     'Uniform/transparent captures may expose a readback limitation.</p>'
                                     '<p><a href="image.json">SHA and source manifest</a> · '
                                     '<a href="report.txt">Renderer report</a></p>\n')


def render_fixture(fixture, metadata, output, build):
    fixture, output, build = Path(fixture).resolve(), Path(output).resolve(), Path(build).resolve()
    validate_fixture(fixture, metadata)
    output.mkdir(parents=True, exist_ok=False)
    manifest = dict(format='gx-frame-image-v1', verdict='image-unavailable', image=None,
                    fixture=str(fixture), capture=metadata, parity_verified=False,
                    code_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    code_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
                    recompcore_pin=gx_build.PINS['recompcore'],
                    source_sha256={str(path.relative_to(ROOT)): launch.sha256(path)
                                   for path in sorted((ROOT / 'vita/renderer').iterdir())
                                   if path.is_file() and (path.suffix in ('.h', '.cpp') or path.name == 'CMakeLists.txt')})
    for path in [ROOT / 'vita/feasibility/gx/png_capture.c', ROOT / 'vita/feasibility/gx/png_capture.h',
                 ROOT / 'scripts/vita/gx/image.py', ROOT / 'scripts/vita/gx/launch.py', ROOT / 'scripts/vita/gx/watch.py']:
        manifest['source_sha256'][str(path.relative_to(ROOT))] = launch.sha256(path)
    preflight, status = extract.replay_fixture(fixture, metadata, binary=build / 'gx-host/gx_bridge')
    (output / 'preflight.txt').write_text(preflight)
    if status == 2 or 'verdict=rejected' in preflight:
        manifest['verdict'] = 'memory-required' if status == 2 else 'replay-rejected'
        (output / 'report.txt').write_text(preflight)
        write_manifest(output, manifest)
        return status or 1
    try:
        result = run_replay(fixture, build)
        manifest['run'] = result['evidence']
        manifest['run_directory'] = result.get('run_directory')
        manifest['surface_sync_enabled'] = True
        manifest['surface_sync_config_sha256'] = result.get('surface_sync_config_sha256')
        (output / 'report.txt').write_text(result['report'])
        if result['capture'] is not None:
            shutil.copyfile(result['capture'], output / 'capture.png')
            manifest['image'] = png_metadata(output / 'capture.png')
            if result.get('raw_capture') is not None:
                shutil.copyfile(result['raw_capture'], output / 'capture-rgba.png')
                manifest['raw_image'] = png_metadata(output / 'capture-rgba.png')
                raw_pixels = parse_png(output / 'capture-rgba.png')[-1]
                display_pixels = parse_png(output / 'capture.png')[-1]
                if len(raw_pixels) != len(display_pixels) or any(raw_pixels[channel::4] != display_pixels[channel::4]
                                                               for channel in range(3)):
                    raise ValueError('display PNG RGB differs from raw readback')
                manifest['image']['pixel_transform'] = 'display alpha=255; every RGB byte equals raw readback'
            captures = [line for line in result['report'].splitlines() if line.startswith('capture written ')]
            manifest['capture_point'] = captures[-1] if captures else 'not-reported'
            manifest['verdict'] = 'image-captured' if result['ok'] and status == 0 else 'image-captured-with-coverage-gaps'
            if manifest['image']['alpha_nonzero'] == 0:
                manifest['verdict'] = 'image-readback-blocked'
        write_manifest(output, manifest)
        print(json.dumps(dict(verdict=manifest['verdict'], image=str(output / 'capture.png') if manifest['image'] else None,
                              manifest=str(output / 'image.json')), sort_keys=True))
        return 0 if manifest['verdict'] == 'image-captured' else (2 if manifest['verdict'] == 'image-readback-blocked' else 1)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        manifest['error'] = str(error)
        write_manifest(output, manifest)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='new private image evidence directory')
    parser.add_argument('--build', type=Path, default=Path(os.environ.get('WW02_BUILD', DEFAULT_BUILD)))
    args = parser.parse_args()
    try:
        metadata = json.loads((args.fixture / 'capture.json').read_text())
        return render_fixture(args.fixture, metadata, args.output, args.build)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'refusal reason={error}')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
