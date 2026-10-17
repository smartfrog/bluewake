#!/usr/bin/env python3
"""Prove a reused private CMake tree flips the edge observation admission from ON to OFF."""

import argparse
from pathlib import Path
import subprocess

from game_build import ROOT, edge_observation_admit_configure_option

# Configure-only: the tests-only source list needs these generated inputs to exist.
TEST_INPUTS = ('ww05_external_vector.c', 'ww05_fp_vector.c', 'ww05_decrementer_vector.c')


def cache_option(build):
    prefix = 'WW_EDGE_OBSERVATION_ADMIT:'
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if line.startswith(prefix):
            return line.split('=', 1)[1]
    raise ValueError(f'WW_EDGE_OBSERVATION_ADMIT missing from {build}/CMakeCache.txt')


def configure(cmake, build, recompcore, enabled):
    command = [cmake, '-S', str(ROOT / 'vita/host'), '-B', str(build), '-DWW05_TESTS_ONLY=ON',
               f'-DRECOMPCORE_DIR={recompcore}', '-DCMAKE_C_COMPILER=/usr/bin/gcc',
               *edge_observation_admit_configure_option(enabled)]
    print('+ ' + ' '.join(command), flush=True)
    subprocess.run(command, check=True)
    value = cache_option(build)
    expected = 'ON' if enabled else 'OFF'
    print(f'WW_EDGE_OBSERVATION_ADMIT={value}', flush=True)
    if value != expected:
        raise ValueError(f'reused tree kept {value} after asking for {expected}')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True,
                        help='scratch CMake build directory, which must not exist yet')
    parser.add_argument('--recompcore', type=Path, required=True)
    parser.add_argument('--cmake', default='cmake')
    args = parser.parse_args()
    work = args.work.resolve()
    if work.exists():
        raise ValueError(f'scratch directory must not exist: {work}')
    work.mkdir(parents=True)
    for name in TEST_INPUTS:
        (work / name).write_text('')
    configure(args.cmake, work, args.recompcore.resolve(), True)
    configure(args.cmake, work, args.recompcore.resolve(), False)
    print('PASS edge observation admission reconfigures an existing tree from ON to OFF')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
