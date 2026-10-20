#!/usr/bin/env python3
"""VITA-WW-13 host probe tests: the pinned dvd.c against the private dump.

The disc image is private player data: it is never committed, copied into the
repository or printed. Every expected value is re-derived here from the image
itself by an independent FST reader, so the probe's answers are checked against
a second implementation of the documented DVDFS layout, not against itself.
Without the image the image-dependent tests skip.

Environment:
  WW13_ISO    disc image path (default: the main checkout's private USA image)
  WW13_BUILD  build directory (default: <main checkout>/build-f13/ww13)
"""

import os
import struct
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MAIN_CHECKOUT = ROOT
DEFAULT_ISO = MAIN_CHECKOUT / "Legend of Zelda, The - The Wind Waker (USA).iso"
DEFAULT_BUILD = MAIN_CHECKOUT / "build-f13" / "ww13"

# The game's first disc paths, resolved against the real private image: the
# boot framework and the REL archive at the root, then ordinary resources.
BOOT_PATHS = [
    "/framework.str",
    "/RELS.arc",
    "/res/Msg/bmgres.arc",
    "/res/Menu/Menu1.dat",
]
MISSING_PATH = "/nonexistent-ww13-probe.xyz"
DIGEST_BYTES = 4096

FST_ENTRY_SIZE = 12
DISC_MAGIC_OFFSET = 0x1C
DISC_MAGIC = 0xC2339F3D
DISC_FST_OFFSET = 0x424
DISC_FST_SIZE = 0x428


def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


class FstImage:
    """Independent reader of the documented DVDFS FST, case-insensitive."""

    def __init__(self, path):
        with open(path, "rb") as stream:
            header = stream.read(0x440)
            if len(header) != 0x440:
                raise ValueError("image shorter than its header")
            if struct.unpack_from(">I", header, DISC_MAGIC_OFFSET)[0] != DISC_MAGIC:
                raise ValueError("not a GameCube disc image")
            fst_offset = struct.unpack_from(">I", header, DISC_FST_OFFSET)[0]
            fst_size = struct.unpack_from(">I", header, DISC_FST_SIZE)[0]
            stream.seek(fst_offset)
            self.fst = stream.read(fst_size)
            if len(self.fst) != fst_size:
                raise ValueError("short FST read")
        self.entries = struct.unpack_from(">I", self.fst, 8)[0]
        self.strings = self.fst[self.entries * FST_ENTRY_SIZE:]

    def _entry(self, index):
        base = index * FST_ENTRY_SIZE
        word, second, third = struct.unpack_from(">III", self.fst, base)
        is_dir = (word & 0xFF000000) != 0
        return is_dir, word & 0x00FFFFFF, second, third

    def _name(self, index):
        offset = self._entry(index)[1]
        end = self.strings.index(b"\0", offset)
        return self.strings[offset:end].decode("ascii")

    def resolve(self, path):
        directory = 0
        while True:
            if path == "":
                return directory
            if path.startswith("/"):
                directory = 0
                path = path[1:]
                continue
            if path == "." or path.startswith("./"):
                path = path[2:] if path.startswith("./") else ""
                continue
            if path.startswith("../"):
                directory = self._entry(directory)[2]
                path = path[3:]
                continue
            name, separator, rest = path.partition("/")
            want_dir = bool(separator)
            next_index = self._entry(directory)[3]
            found = None
            index = directory + 1
            while index < next_index:
                is_dir, _, _, _ = self._entry(index)
                if (not is_dir or want_dir) and self._name(index).lower() == name.lower():
                    found = index
                    break
                index = self._entry(index)[3] if is_dir else index + 1
            if found is None:
                return -1
            if not want_dir:
                return found
            directory = found
            path = rest

    def info(self, entry):
        is_dir, _, start, length = self._entry(entry)
        if is_dir:
            return None
        return start, length


def find_iso():
    candidates = [
        os.environ.get("WW13_ISO"),
        ROOT / DEFAULT_ISO.name,
        DEFAULT_ISO,
    ]
    for candidate in candidates:
        if candidate is not None and Path(candidate).is_file():
            return Path(candidate)
    return None


def find_probe():
    build = Path(os.environ.get("WW13_BUILD", DEFAULT_BUILD))
    return build / "disc-host" / "disc_probe"


def run_probe(iso, paths):
    result = subprocess.run(
        [str(find_probe()), str(iso), *paths],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise AssertionError(f"probe failed ({result.returncode}): {result.stderr}")
    return result.stdout


def parse_probe(output):
    lines = {}
    for line in output.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "entries":
            lines["entries"] = line
        elif parts[0] in ("resolve", "digest") and len(parts) >= 2:
            lines[f"{parts[0]} {parts[1]}"] = line
        elif parts[0] == "guest_ok":
            lines["guest_ok"] = line
    return lines


class DiscProbeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.iso = find_iso()
        if cls.iso is None:
            raise unittest.SkipTest("private disc image not present")
        if not find_probe().is_file():
            raise unittest.SkipTest("disc_probe not built; run scripts/vita/disc/build.py")

    def test_probe_agrees_with_independent_fst_reader(self):
        image = FstImage(self.iso)
        output = run_probe(self.iso, [*BOOT_PATHS, MISSING_PATH])
        lines = parse_probe(output)

        self.assertEqual(int(lines["entries"].split()[1]), image.entries)
        for path in BOOT_PATHS:
            with self.subTest(path=path):
                entry = image.resolve(path)
                self.assertNotEqual(entry, -1, f"{path} must exist in the private image")
                start, length = image.info(entry)
                self.assertEqual(
                    lines[f"resolve {path}"],
                    f"resolve {path} {entry} {start} {length}")
        self.assertEqual(
            lines[f"resolve {MISSING_PATH}"],
            f"resolve {MISSING_PATH} -1 0 0")

    def test_probe_digests_match_independent_reads(self):
        image = FstImage(self.iso)
        output = run_probe(self.iso, BOOT_PATHS)
        lines = parse_probe(output)
        with open(self.iso, "rb") as stream:
            for path in BOOT_PATHS:
                with self.subTest(path=path):
                    start, length = image.info(image.resolve(path))
                    stream.seek(start)
                    data = stream.read(min(length, DIGEST_BYTES))
                    self.assertEqual(
                        lines[f"digest {path}"],
                        f"digest {path} {fnv1a64(data):016x} {len(data)}")

    def test_probe_guest_copy_is_byte_identical(self):
        output = run_probe(self.iso, BOOT_PATHS)
        lines = parse_probe(output)
        self.assertEqual(lines["guest_ok"], "guest_ok 1")


if __name__ == "__main__":
    unittest.main()
