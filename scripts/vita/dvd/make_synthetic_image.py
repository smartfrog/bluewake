#!/usr/bin/env python3
"""Game-free synthetic GameCube image for the VITA-WW-13 seam tests.

Builds the same tiny layout the C probe builds in memory: a valid header, a
5-entry FST with one root file and a subdirectory, and a deterministic byte
pattern. No game data is involved and the file is small enough to live in an
ignored build directory.
"""

import argparse
import struct
from pathlib import Path

DISC_SIZE = 0x1000
FST_OFFSET = 0x440
MAGIC = 0xC2339F3D

# (word0, word1, word2) per entry; word0 carries the directory bit and the
# string-table offset, word1/word2 are parent/next for directories and
# offset/length for files.
ENTRIES = (
    (0x01000000, 0, 5),
    (1, 0x500, 0x30),
    (0x01000000 | 10, 0, 5),
    (14, 0x600, 0x100),
    (24, 0x800, 0x40),
)
STRINGS = b"\0boot.bin\0res\0asset.bin\0leaf.bin"


def pattern(offset):
    return (offset * 7 + 3) & 0xFF


def build_image():
    image = bytearray(pattern(i) for i in range(DISC_SIZE))
    struct.pack_into(">I", image, 0x1C, MAGIC)
    struct.pack_into(">II", image, 0x424, FST_OFFSET, 5 * 12 + len(STRINGS))
    fst = FST_OFFSET
    for index, (word0, word1, word2) in enumerate(ENTRIES):
        struct.pack_into(">III", image, fst + index * 12, word0, word1, word2)
    image[fst + 5 * 12:fst + 5 * 12 + len(STRINGS)] = STRINGS
    return bytes(image)


def write_image(path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(build_image())
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = write_image(args.output)
    print(f"synthetic image: {output} ({DISC_SIZE} bytes)")


if __name__ == "__main__":
    raise SystemExit(main())
