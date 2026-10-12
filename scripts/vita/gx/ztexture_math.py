#!/usr/bin/env python3
"""Execute the generated scalar Cg depth arithmetic against a 24-bit integer oracle."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, help='WW18 output directory')
    args = parser.parse_args()
    build = args.build.resolve()
    source = build / 'ztexture-math.cpp'
    binary = build / 'ztexture-math'
    subprocess.run([str(build / 'gx-host/gx_cg_tests'), '--write-ztexture-math',
                    str(source)], check=True)
    subprocess.run(['c++', '-std=c++20', '-O2', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
