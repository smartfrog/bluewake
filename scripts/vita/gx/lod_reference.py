#!/usr/bin/env python3
"""Bounded Dolphin LOD/mip evidence; not the Vita renderer or a hardware oracle."""

import argparse
import json
import math
import struct


def sampler(mode0, mode1):
    for value in (mode0, mode1):
        if type(value) is not int or not 0 <= value <= 0xFFFFFF:
            raise ValueError('sampler registers must be unsigned 24-bit integers')
    bias = (mode0 >> 9) & 255
    return dict(wrap_s=mode0 & 3, wrap_t=(mode0 >> 2) & 3,
                mag_linear=bool(mode0 & (1 << 4)), mip_mode=(mode0 >> 5) & 3,
                min_linear=bool(mode0 & (1 << 7)), diagonal=bool(mode0 & (1 << 8)),
                bias_s2_5=bias - 256 if bias >= 128 else bias,
                anisotropy=(mode0 >> 19) & 3, bias_clamp=bool(mode0 & (1 << 21)),
                min_lod=mode1 & 255, max_lod=(mode1 >> 8) & 255)


def select(dx, dy, state):
    if len(dx) != 2 or len(dy) != 2 or not all(math.isfinite(x) for x in (*dx, *dy)):
        raise ValueError('gradients must have two finite texel-fixed-point components')
    if state['bias_clamp']:
        raise ValueError('bias-clamp behavior is unimplemented in the pinned references')
    if state['anisotropy']:
        raise ValueError('anisotropy is outside this reference model')
    if state['mip_mode'] == 3:
        raise ValueError('reserved mip mode is outside this reference model')
    minimum = min(state['min_lod'], state['max_lod'])
    maximum = state['max_lod']
    bias = state['bias_s2_5']
    if state['mip_mode'] == 0:
        minimum = maximum = bias = 0
    delta = [abs(dx[i]) + abs(dy[i]) if state['diagonal']
             else max(abs(dx[i]), abs(dy[i])) for i in range(2)]
    rho = max(delta) / 128
    raw = minimum if rho == 0 else math.floor(math.log2(rho) * 16) + (bias // 2)
    linear = state['min_linear'] if raw > 0 else state['mag_linear']
    bounded = min(max(raw, minimum), maximum)
    fraction = bounded & 15
    level = bounded >> 4
    if state['mip_mode'] == 1 and fraction >= 8:
        level += 1
    return dict(raw_lod=raw, lod=bounded, level=level, fraction=fraction, linear=linear)


def software_fixed_log2(value):
    if not math.isfinite(value) or value <= 0:
        raise ValueError('software log model requires a finite positive footprint')
    bits = int.from_bytes(struct.pack('>f', value), 'big')
    return ((bits & 0x7F800000) >> 19) - 2032 + ((bits & 0x007FFFFF) >> 19)


GX_TILES = {
    0: (8, 8, 32), 1: (8, 4, 32), 2: (8, 4, 32),
    3: (4, 4, 32), 4: (4, 4, 32), 5: (4, 4, 32), 6: (4, 4, 64),
    8: (8, 8, 32), 9: (8, 4, 32), 10: (4, 4, 32), 14: (8, 8, 32),
}


def mip_layout(format_, width, height, count):
    if type(format_) is not int or format_ not in GX_TILES:
        raise ValueError('unsupported GX texture format')
    for size in (width, height):
        if type(size) is not int or not 1 <= size <= 1024 or size & (size - 1):
            raise ValueError('bounded mip model requires power-of-two GX dimensions up to 1024')
    if type(count) is not int or not 1 <= count <= max(width, height).bit_length():
        raise ValueError('mip level count exceeds the natural chain')
    tile_w, tile_h, tile_bytes = GX_TILES[format_]
    result = []
    offset = 0
    for level in range(count):
        size = ((width + tile_w - 1) // tile_w) * ((height + tile_h - 1) // tile_h) * tile_bytes
        result.append(dict(level=level, width=width, height=height, offset=offset, size=size))
        offset += size
        width, height = max(width >> 1, 1), max(height >> 1, 1)
    return result


def captured_levels(payload, layout):
    levels = []
    for entry in layout:
        start, size = entry['offset'], entry['size']
        if start > len(payload) or size > len(payload) - start:
            raise ValueError('missing captured mip bytes; never generate replacement levels')
        levels.append(payload[start:start + size])
    return levels


def blend_mips(first, second, fraction):
    if any(type(value) is not int for value in (first, second, fraction)):
        raise ValueError('mip blend inputs must be integer bytes and a sixteenths fraction')
    if not 0 <= first <= 255 or not 0 <= second <= 255 or not 0 <= fraction <= 15:
        raise ValueError('mip blend inputs exceed byte/fraction bounds')
    return (first * (16 - fraction) + second * fraction) >> 4


def verify():
    gradients = 0
    for diagonal in (False, True):
        for bin_ in range(-48, 161):
            derivative = 128 * 2 ** ((bin_ + 0.25) / 16)
            for encoded_bias in range(256):
                state = sampler((1 << 5) | (1 << 7) | (int(diagonal) << 8) | (encoded_bias << 9), 0xA010)
                signed_bias = encoded_bias if encoded_bias < 128 else encoded_bias - 256
                raw = bin_ + (16 if diagonal else 0) + math.floor(signed_bias / 2)
                bounded = min(max(raw, 16), 160)
                expected = dict(raw_lod=raw, lod=bounded,
                                level=(bounded + 8) // 16, fraction=bounded % 16, linear=raw > 0)
                actual = select((derivative, 0), (-derivative, 0), state)
                if actual != expected:
                    raise ValueError(f'gradient reference mismatch: {actual} != {expected}')
                gradients += 1
    blends = 0
    for first in range(256):
        for second in range(256):
            for fraction in range(16):
                expected = math.floor(first + (second - first) * fraction / 16)
                if blend_mips(first, second, fraction) != expected:
                    raise ValueError('mip blend reference mismatch')
                blends += 1
    return dict(gradient_cases=gradients, mip_blend_cases=blends,
                evidence='reference-model-only', renderer_original_lod='refused')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    print(json.dumps(verify(), sort_keys=True))


if __name__ == '__main__':
    main()
