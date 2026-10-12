#!/usr/bin/env python3
"""Execute emitted Cg indirect-coordinate arithmetic against a signed integer oracle."""

import argparse
import itertools
import math
from pathlib import Path
import random
import re
import struct
import subprocess


VARIANTS = list(itertools.product(range(4), range(8), range(4)))


def float32(value):
    return struct.unpack('f', struct.pack('f', value))[0]


def signed(value, bits):
    return ((value + (1 << (bits - 1))) % (1 << bits)) - (1 << (bits - 1))


def compile_generated(build):
    shaders = build / 'indirect-shaders'
    subprocess.run([str(build / 'gx-host/gx_cg_tests'), '--write-indirect-shaders', str(shaders)], check=True)
    source = ['#include <cmath>\n#include <iostream>\nusing std::floor;\n',
              'struct Float2 {float x,y;}; struct Float3 {float x,y,z;}; struct Float4 {float x,y,z,w;};\n',
              'Float3 operator/(Float3 v,float s) {return {v.x/s,v.y/s,v.z/s};}\n',
              'Float3 floor(Float3 v) {return {std::floor(v.x),std::floor(v.y),std::floor(v.z)};}\n',
              'using float3=Float3;\nstruct Result {float s,t,bump,normalized,u,v;};\n']
    signature = '(Float2 base, Float3 raw, Float2 previous, float alphabump, Float4* indirect_matrices)'
    for index, variant in enumerate(VARIANTS):
        shader = (shaders / ('-'.join(map(str, variant)) + '.cg')).read_text()
        if index == 0:
            helpers = shader[:shader.index('float4 main')]
            source.append(re.sub(r'\b\d+\.\d+\b', lambda m: m[0] + 'f', helpers))
        statements = shader[shader.index('  float indirect_s0'):shader.index('  tex =')]
        statements = re.sub(r'\b\d+\.\d+\b', lambda m: m[0] + 'f', statements)
        source.append(f'Result indirect_{index}{signature} {{\n'
                      'Float2 fixpoint0=base, previous_coord=previous, tevcoord{};\n'
                      'Float3 ind_raw0=raw,ind_raw1=raw,ind_raw2=raw,ind_raw3=raw;\n'
                      'Float4 texture_dimensions[8]{}; texture_dimensions[0]={4,4,4,4};\n'
                      + statements + 'return {tevcoord.x,tevcoord.y,alphabump,\n'
                      'alphabump+floor(alphabump/32.f),indirect_u0,indirect_v0};}\n')
    source.append('using Fn=Result(*)' + signature + ';\nFn variants[]={')
    source.append(','.join(f'indirect_{index}' for index in range(len(VARIANTS))))
    source.append('};\nint main(){ int index; while(std::cin>>index){\n'
                  'Float2 base,previous; Float3 raw; Float4 matrices[6]; float bump;\n'
                  'std::cin>>base.x>>base.y>>raw.x>>raw.y>>raw.z>>previous.x>>previous.y>>bump;\n'
                  'for(auto& m:matrices) std::cin>>m.x>>m.y>>m.z>>m.w;\n'
                  'if(!std::cin) return 2; auto r=variants[index](base,raw,previous,bump,matrices);\n'
                  'std::cout.precision(9);\n'
                  'std::cout<<r.s<<" "<<r.t<<" "<<r.bump<<" "<<r.normalized<<" "<<r.u<<" "<<r.v<<"\\n";\n'
                  '} return 0;}\n')
    path = build / 'indirect-math.cpp'
    binary = build / 'indirect-math'
    path.write_text(''.join(source))
    subprocess.run(['c++', '-std=c++20', '-O2', '-ffp-contract=off', str(path), '-o', str(binary)], check=True)
    return binary


def oracle(variant, values):
    format_, bias, matrix = variant
    base = list(map(int, values[:2]))
    raw = list(map(int, values[2:5]))
    previous = list(map(int, values[5:7]))
    bump = int(values[7])
    selector = bias % 4
    if selector:
        bump = (raw[selector - 1] << (0, 5, 4, 3)[format_]) & 248
    input_ = [value >> (0, 3, 4, 5)[format_] for value in raw]
    for component in range(3):
        if bias & (1 << component):
            input_[component] += -128 if format_ == 0 else 1
    rows = [values[offset:offset + 4] for offset in range(8, 32, 4)]
    selected = 2 * (bias % 3)
    shift = -int(math.log2(rows[selected][3]))
    translation = [0, 0]
    if matrix != 3:
        for axis in range(2):
            if matrix == 0:
                translation[axis] = sum(int(rows[selected + axis][c]) * input_[c] for c in range(3)) >> 3
            else:
                translation[axis] = signed(base[axis] * input_[matrix - 1], 32) >> 8
            translation[axis] = (translation[axis] >> shift) if shift >= 0 else signed(translation[axis] << -shift, 32)
    result = []
    for axis, wrap in enumerate((bias, (bias + 3) % 8)):
        value = base[axis] if wrap == 0 else 0 if wrap >= 6 else base[axis] & (((256 >> (wrap - 1)) << 7) - 1)
        value += translation[axis]
        if bias % 2:
            value += previous[axis]
        result.append(signed(value, 24))
    return [*result, bump, bump | (bump >> 5), result[0] / 512, result[1] / 512]


def vectors():
    generator = random.Random(0x1D1)
    edges = [-2147483392, -8388608, -1, 0, 1, 8388607, 2147483392]
    for index, variant in enumerate(VARIANTS):
        for sample in range(1024):
            base = [generator.randrange(-2147483392, 2147483393) for _ in range(2)]
            if sample < len(edges):
                base = [edges[sample], edges[-1 - sample]]
            raw = [generator.randrange(256) for _ in range(3)]
            if sample < 4:
                raw = [[0, 0, 0], [255, 255, 255], [128, 127, 8], [3, 15, 31]][sample]
            values = base + raw + [generator.randrange(-8388608, 8388608) for _ in range(2)]
            values.append(generator.randrange(32) * 8)
            for matrix in range(3):
                scale = 2 ** (14 - (sample + matrix) % 32)
                for row in range(2):
                    values += [generator.randrange(-1024, 1024) for _ in range(3)] + [scale]
            values = list(map(float32, values))
            yield index, values, oracle(variant, values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    args = parser.parse_args()
    binary = compile_generated(args.build.resolve())
    cases = list(vectors())
    inputs = ''.join(str(index) + ' ' + ' '.join(format(value, '.9g') for value in values) + '\n'
                     for index, values, _ in cases)
    result = subprocess.run([str(binary)], input=inputs, capture_output=True, text=True, check=True)
    lines = result.stdout.splitlines()
    if len(lines) != len(cases):
        raise RuntimeError('generated indirect arithmetic lost cases')
    for line, (index, values, expected) in zip(lines, cases):
        actual = list(map(float32, map(float, line.split())))
        if actual != expected:
            raise RuntimeError(f'indirect={VARIANTS[index]} input={values} actual={actual} expected={expected}')
    print(f'generated indirect math matches {len(cases)} integer-oracle cases across 128 shader variants')


if __name__ == '__main__':
    main()
