#!/usr/bin/env python3
"""VITA-WW-02 PNG capture writer tests.

Runs the host writer and parses its file independently: signature, chunk
CRCs, IHDR, the zlib stream (stored deflate blocks) and every pixel.

Environment:
  WW02_BUILD  build directory (default: <main checkout>/build-f2/ww02)
"""

import os
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD = ROOT / 'build-f2/ww02'
SOURCES = [
    ROOT / "vita/feasibility/gx/png_capture.c",
    ROOT / "vita/feasibility/gx/png_capture.h",
    ROOT / "vita/feasibility/gx/png_capture_tests.c",
]
PATTERN = bytes(range(1, 25))  # 3x2 RGBA8


def check_binary(binary):
    if not binary.is_file():
        raise unittest.SkipTest(f"{binary.name} not built; run scripts/vita/gx/build.py")
    newest = max(source.stat().st_mtime for source in SOURCES if source.is_file())
    if binary.stat().st_mtime < newest:
        raise AssertionError(f"stale binary {binary}; run scripts/vita/gx/build.py")


def find_tests():
    build = Path(os.environ.get("WW02_BUILD", DEFAULT_BUILD))
    return build / "gx-host" / "png_capture_tests"


def parse_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("bad PNG signature")
    pos = 8
    chunks = []
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        ctype = data[pos + 4:pos + 8]
        cdata = data[pos + 8:pos + 8 + length]
        crc = int.from_bytes(data[pos + 8 + length:pos + 12 + length], "big")
        if zlib.crc32(ctype + cdata) & 0xFFFFFFFF != crc:
            raise ValueError(f"bad CRC on {ctype!r}")
        chunks.append((ctype, cdata))
        pos += 12 + length
    if chunks[0][0] != b"IHDR" or chunks[-1][0] != b"IEND":
        raise ValueError("chunk order")
    ihdr = chunks[0][1]
    width, height, depth, colour = struct.unpack(">IIBB", ihdr[:10])
    idat = b"".join(cdata for ctype, cdata in chunks if ctype == b"IDAT")
    raw = zlib.decompress(idat)
    row_bytes = width * 4
    rows = []
    for y in range(height):
        row = raw[y * (row_bytes + 1):(y + 1) * (row_bytes + 1)]
        if row[0] != 0:
            raise ValueError("filter byte")
        rows.append(row[1:])
    return width, height, depth, colour, b"".join(rows)


class PngCaptureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        check_binary(find_tests())

    def test_written_png_matches_the_pattern(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "capture.png"
            result = subprocess.run([str(find_tests()), str(output)],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            width, height, depth, colour, pixels = parse_png(output)
            self.assertEqual((width, height), (3, 2))
            self.assertEqual((depth, colour), (8, 6))
            self.assertEqual(pixels, PATTERN)


if __name__ == "__main__":
    unittest.main()
