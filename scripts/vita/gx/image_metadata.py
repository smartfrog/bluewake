"""Exact PNG hashes and pixel statistics, shared by live and replay capture."""

from collections import Counter
import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'vita/feasibility/gx'))
from test_png_capture import parse_png


def png_metadata(path):
    width, height, depth, color, pixels = parse_png(path)
    if (depth, color) != (8, 6) or len(pixels) != width * height * 4:
        raise ValueError('capture is not complete RGBA8 PNG data')
    colors = Counter(pixels[n:n + 4] for n in range(0, len(pixels), 4))
    return dict(path=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(), width=width, height=height,
                row_order='top-first', distinct_rgba=len(colors),
                alpha_nonzero=sum(count for rgba, count in colors.items() if rgba[3]),
                most_common=[dict(rgba=list(rgba), pixels=count) for rgba, count in colors.most_common(8)],
                pixel_status='uniform-readback' if len(colors) == 1 else 'nonuniform-readback')


def verify_display_rgb(raw, display):
    raw_pixels = parse_png(raw)[-1]
    display_pixels = parse_png(display)[-1]
    if len(raw_pixels) != len(display_pixels) or any(raw_pixels[channel::4] != display_pixels[channel::4]
                                                   for channel in range(3)):
        raise ValueError('display PNG RGB differs from raw readback')
    if any(alpha != 255 for alpha in display_pixels[3::4]):
        raise ValueError('display PNG does not contain the declared opaque alpha')
