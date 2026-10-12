#!/usr/bin/env python3
"""Execute generated scalar Cg fog statements against a separate numerical oracle."""

import argparse
import itertools
import math
from pathlib import Path
import random
import re
import struct
import subprocess


VARIANTS = list(itertools.product((2, 4, 5, 6, 7), (0, 1), (0, 1), (0, 1)))


def float32(value):
    return struct.unpack('f', struct.pack('f', value))[0]


def compile_generated(build):
    shaders = build / 'fog-shaders'
    subprocess.run([str(build / 'gx-host/gx_cg_tests'), '--write-fog-shaders', str(shaders)], check=True)
    source = ['#include <algorithm>\n#include <cmath>\n#include <iostream>\n',
              'using std::floor; using std::abs; using std::sqrt; using std::exp2; using std::clamp;\n',
              'float lerp(float a,float b,float t) { return a+(b-a)*t; }\n',
              'struct Float2 { float x,y; }; struct Float4 { float x,y,z,w; };\n',
              'struct Color { float r,g,b,a; }; struct Result { float weight; Color color; };\n']
    signature = '(Color prev, Color fog_color, Float4 fog_params, Float2 fog_depth, ' \
                'Float4* fog_range, Float2 fog_screen, Float4 screen, float fragment_depth)'
    for index, variant in enumerate(VARIANTS):
        shader = (shaders / ('-'.join(map(str, variant)) + '.cg')).read_text()
        statements = shader[shader.index('  float fog_z'):shader.index('  return prev')]
        statements = re.sub(r'\b\d+\.\d+\b', lambda match: match[0] + 'f', statements)
        source.append(f'Result fog_{index}{signature} {{\n{statements}return {{fog_alpha,prev}}; }}\n')
    source.append('using Fn = Result(*)' + signature + ';\nFn variants[] = {')
    source.append(','.join(f'fog_{index}' for index in range(len(VARIANTS))))
    source.append('};\nint main() {\nint index; while(std::cin>>index) {\n'
                  'Color prev,color; Float4 params,range[3],screen; Float2 depth,mapping; float fragment;\n'
                  'std::cin>>prev.r>>prev.g>>prev.b>>prev.a>>color.r>>color.g>>color.b>>color.a;\n'
                  'std::cin>>params.x>>params.y>>params.z>>params.w>>depth.x>>depth.y;\n'
                  'for(auto& k:range) std::cin>>k.x>>k.y>>k.z>>k.w;\n'
                  'std::cin>>mapping.x>>mapping.y>>screen.x>>screen.y>>screen.z>>screen.w>>fragment;\n'
                  'if(!std::cin) return 2;\n'
                  'auto r=variants[index](prev,color,params,depth,range,mapping,screen,fragment);\n'
                  'std::cout.precision(9);\n'
                  'std::cout<<r.weight<<" "<<r.color.r<<" "<<r.color.g<<" "<<r.color.b<<" "<<r.color.a<<"\\n";\n'
                  '} return 0; }\n')
    path = build / 'fog-math.cpp'
    binary = build / 'fog-math'
    path.write_text(''.join(source))
    subprocess.run(['c++', '-std=c++20', '-O2', '-ffp-contract=off', str(path), '-o', str(binary)], check=True)
    return binary


def oracle(variant, values):
    mode, projection, use_range, use_texture_depth = variant
    prev, color = values[:4], values[4:8]
    a, c, center, width = values[8:12]
    magnitude, divisor = values[12:14]
    table = values[14:24]
    scale, left = values[26:28]
    x, _, raster, _, texture = values[28:33]
    z = min(0xFFFFFF, max(0, math.floor((texture if use_texture_depth else raster) * (1 << 24))))
    denominator = int(magnitude) - (z >> int(math.log2(divisor)))
    if projection:
        ze = a * z / (1 << 24)
    else:
        ze = a * (1 << 24) / (denominator or 1)
    if use_range:
        offset = 2 * ((x * scale + left) / width) - 1 - center
        lookup = min(9, max(0, 9 - abs(offset) * 9))
        lower = int(lookup)
        upper = min(9, lower + 1)
        k = table[lower] * (1 - (lookup - lower)) + table[upper] * (lookup - lower)
        ze *= math.hypot(offset, k) / k
    factor = min(1, max(0, ze - c))
    if not projection and denominator == 0 and a != 0:
        factor = int(a > 0)
    if mode == 4:
        factor = 1 - 2 ** (-8 * factor)
    elif mode == 5:
        factor = 1 - 2 ** (-8 * factor * factor)
    elif mode == 6:
        factor = 2 ** (-8 * (1 - factor))
    elif mode == 7:
        factor = 2 ** (-8 * (1 - factor) ** 2)
    scaled = factor * 256
    weight = round(scaled)
    rgb = [math.floor((prev[i] * (256 - weight) + color[i] * weight) / 256) for i in range(3)]
    return [weight, *rgb, prev[3]], scaled


def vectors():
    random_source = random.Random(0xF06)
    for index, variant in enumerate(VARIANTS):
        _, projection, _, _ = variant
        for sample in range(1024):
            z = random_source.randrange(1 << 24)
            other_z = random_source.randrange(1 << 24)
            x = random_source.uniform(-960, 1920)
            a, c = (1, 0) if projection else (0.125, 0.125)
            magnitude, divisor = 0xC00000, 2
            if sample < 8:
                z = (0, 1, 0x800000, 0xFFFFFF, 0x8000, 0x18000, 0xFF8000, 0xFE8000)[sample]
                other_z = 0xFFFFFF - z
                x = (0, 480, 960, 240, 720, -960, 1920, 480)[sample]
            if sample == 8:
                magnitude = z >> 1
            if sample == 9:
                a = 0
                magnitude = z >> 1
            if sample == 10:
                a = -0.25
                magnitude = z >> 1
            if sample == 11:
                divisor = float(1 << 31)
            if sample == 12:
                c = -1
                a = 0
            if sample == 13:
                c = 1
                a = 0
            table = [4 + 0.5 * i for i in range(10)] + [0, 0]
            if sample >= 14:
                table = [random_source.randrange(1, 4096) / 64 for _ in range(10)] + [0, 0]
            values = [random_source.randrange(-1024, 1024) for _ in range(3)] + [117]
            values += [32, 64, 128, 0, a, c, 0, 640, magnitude, divisor]
            values += table + [640 / 960, 0, x, 0, z / (1 << 24), 1, other_z / (1 << 24)]
            values = list(map(float32, values))
            expected, scaled = oracle(variant, values)
            # Exact half-step tests remain; random samples near a float rounding boundary are excluded.
            distance = abs(scaled - math.floor(scaled) - 0.5)
            if sample >= 14 and distance < 0.002:
                continue
            yield index, values, expected
    for texture_depth in (0, 1):
        variant = (2, 1, 0, texture_depth)
        index = VARIANTS.index(variant)
        factors = [step / 256 for step in range(257)]
        factors += [(step + 0.5) / 256 for step in range(256)]
        for factor in factors:
            values = [7, 127, -1024, 117, 32, 64, 128, 0,
                      1, 0, 0, 640, 0xC00000, 2]
            values += [4] * 10 + [0, 0, 640 / 960, 0, 480, 0, factor, 1, factor]
            values = list(map(float32, values))
            expected, _ = oracle(variant, values)
            yield index, values, expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    args = parser.parse_args()
    binary = compile_generated(args.build.resolve())
    cases = list(vectors())
    inputs = ''.join(str(index) + ' ' + ' '.join(format(value, '.9g') for value in values) + '\n'
                     for index, values, _ in cases)
    result = subprocess.run([str(binary)], input=inputs, text=True, capture_output=True, check=True)
    output = result.stdout.splitlines()
    if len(output) != len(cases):
        raise RuntimeError('generated fog arithmetic lost cases')
    for line, (index, values, expected) in zip(output, cases):
        actual = list(map(float, line.split()))
        if actual != expected:
            raise RuntimeError(f'fog={VARIANTS[index]} input={values} actual={actual} expected={expected}')
    print(f'generated fog math matches {len(cases)} oracle cases across 40 shader variants; alpha preserved')


if __name__ == '__main__':
    main()
