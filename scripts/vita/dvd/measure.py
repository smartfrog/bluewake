#!/usr/bin/env python3
"""Run the VITA-WW-13 real-image probe and store its evidence.

Writes report.txt, stderr.txt and evidence.json under the output directory.
The image is read only; the evidence records its path, size and hash, never
its bytes. Use a private local image path and an ignored output directory.
"""

import argparse
import hashlib
import json
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_PROBE = ROOT / 'build-f13/ww13-seam/dvd-probe-tests/dvd_probe'
DEFAULT_OUT = ROOT / 'build-f13/ww13-seam/measure'
PIN_DVD = Path('ref/vita-ww-01/RecompCore/GXRuntime/src/dvd.c')

PATTERNS = {
    'oracle': re.compile(r'^oracle entries=(\d+) files-largest=(.+?) start=(\d+) length=(\d+)$', re.M),
    'backend': re.compile(r'^backend (\S+)$', re.M),
    'throughput': re.compile(r'^throughput bytes=(\d+) seconds=([\d.]+) mib_per_s=([\d.]+)$', re.M),
    'stats': re.compile(r'^stats total=(\d+) chunk=(\d+) staging=(\d+)$', re.M),
}


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def parse(stdout):
    values = {}
    for name, pattern in PATTERNS.items():
        match = pattern.search(stdout)
        if match is None:
            raise SystemExit(f"probe output missing the {name} line")
        values[name] = match.groups()
    if 'REAL PASS' not in stdout and 'REAL FAIL' not in stdout:
        raise SystemExit('probe output missing its verdict line')
    return values


def measure(iso, probe, out, label, paths):
    iso = Path(iso)
    if not iso.is_file():
        raise SystemExit(f'not a file: {iso}')
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / 'report.txt'
    result = subprocess.run([str(probe), 'real', str(iso), str(report_path), *paths],
                            capture_output=True, text=True, timeout=600)
    (out / 'stdout.txt').write_text(result.stdout)
    (out / 'stderr.txt').write_text(result.stderr)
    if not report_path.is_file():
        raise SystemExit(f"probe wrote no report file: {report_path}")
    values = parse(result.stdout)
    oracle = values['oracle']
    throughput = values['throughput']
    stats = values['stats']
    head = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'],
                          capture_output=True, text=True, check=True).stdout.strip()
    evidence = {
        'label': label,
        'timestamp': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
        'git_head': head,
        'image': {'path': str(iso), 'size': iso.stat().st_size, 'sha256': sha256(iso)},
        'pin_dvd_sha256': sha256(ROOT / PIN_DVD),
        'probe': {'path': str(probe), 'sha256': sha256(probe),
                  'returncode': result.returncode, 'stdout': result.stdout,
                  'stderr': result.stderr},
        'report': {'path': str(report_path), 'sha256': sha256(report_path)},
        'parsed': {
            'backend': values['backend'][0],
            'entries': int(oracle[0]),
            'largest': oracle[1],
            'largest_start': int(oracle[2]),
            'largest_length': int(oracle[3]),
            'throughput_bytes': int(throughput[0]),
            'seconds': float(throughput[1]),
            'mib_per_s': float(throughput[2]),
            'total_bytes': int(stats[0]),
            'chunk_bytes': int(stats[1]),
            'staging_bytes': int(stats[2]),
            'verdict': 'REAL PASS' if 'REAL PASS' in result.stdout else 'REAL FAIL',
        },
    }
    (out / 'evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--probe', type=Path, default=DEFAULT_PROBE)
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--label', default='measurement')
    parser.add_argument('--path', action='append', default=[], dest='paths',
                        help='disc path to resolve through the pin (repeatable)')
    args = parser.parse_args()
    evidence = measure(args.iso, args.probe, args.out, args.label, args.paths)
    parsed = evidence['parsed']
    print(f"{parsed['verdict']}: backend={parsed['backend']} entries={parsed['entries']} "
          f"largest={parsed['largest']} ({parsed['largest_length']} bytes) "
          f"{parsed['mib_per_s']} MiB/s chunk={parsed['chunk_bytes']} staging={parsed['staging_bytes']}")
    return 0 if parsed['verdict'] == 'REAL PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
