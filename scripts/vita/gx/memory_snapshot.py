"""Validate a captured MEM1 snapshot and preserve inaccessible sparse holes."""

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re


MEM1_BYTES = 24 << 20
FORMAT = 'gx-mem1-snapshot-v1'
MAX_REGIONS = 65536


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require_hash(value, name):
    if not isinstance(value, str) or re.fullmatch(r'[0-9a-f]{64}', value) is None:
        raise ValueError(f'{name} must be a lowercase SHA-256')
    return value


def integer(value, name):
    if type(value) is not int or value < 0:
        raise ValueError(f'{name} must be a nonnegative integer')
    return value


def physical_address(value):
    if isinstance(value, str):
        if re.fullmatch(r'0x[0-9A-Fa-f]{1,8}', value) is None:
            raise ValueError('region address must be a physical integer or 0x hex address')
        value = int(value, 16)
    return integer(value, 'region address')


def coverage_text(ranges):
    return 'GX_MEM1_RANGES_V1\n' + ''.join(f'{address:08X} {size:08X}\n'
                                         for address, size in ranges)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate snapshot key: {key}')
        result[key] = value
    return result


@dataclass
class Snapshot:
    image: bytes
    ranges: list
    binary: bytes
    manifest_bytes: bytes
    manifest: dict
    metadata: dict

    def coverage_text(self):
        return coverage_text(self.ranges)

    def stage(self, directory):
        (directory / 'mem1.bin').write_bytes(self.image)
        (directory / 'mem1.ranges').write_bytes(self.coverage_text().encode())
        (directory / 'memory.bin').write_bytes(self.binary)
        (directory / 'memory.source.json').write_bytes(self.manifest_bytes)
        manifest = dict(self.manifest, binary='memory.bin')
        (directory / 'memory.json').write_text(json.dumps(manifest, indent=2) + '\n')


def load(path, fifo_hash):
    path = Path(path)
    if path.stat().st_size > 16 << 20:
        raise ValueError('snapshot manifest exceeds 16 MiB')
    manifest_bytes = path.read_bytes()
    manifest = json.loads(manifest_bytes, object_pairs_hook=unique_object)
    if not isinstance(manifest, dict) or manifest.get('format') != FORMAT:
        raise ValueError(f'snapshot format must be {FORMAT}')
    captured_hash = require_hash(manifest.get('fifo_sha256'), 'FIFO hash')
    if captured_hash != fifo_hash:
        raise ValueError('snapshot FIFO SHA-256 does not match captured commands')
    name = manifest.get('binary')
    if not isinstance(name, str) or name in ('', '.', '..') or Path(name).name != name:
        raise ValueError('snapshot binary path must be an adjacent file name')
    binary_path = (path.parent / name).resolve()
    if not binary_path.is_relative_to(path.parent.resolve()):
        raise ValueError('snapshot binary path escapes manifest directory')
    if binary_path.stat().st_size > MEM1_BYTES:
        raise ValueError('snapshot binary exceeds MEM1 size')
    binary = binary_path.read_bytes()
    expected_hash = require_hash(manifest.get('binary_sha256'), 'binary hash')
    if sha256(binary) != expected_hash:
        raise ValueError('snapshot binary SHA-256 mismatch')
    entries = manifest.get('regions')
    if not isinstance(entries, list) or not 1 <= len(entries) <= MAX_REGIONS:
        raise ValueError('snapshot regions must be a nonempty list of at most 65536 entries')
    regions = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError('snapshot region must be an object')
        address = physical_address(entry.get('address'))
        offset = integer(entry.get('offset'), 'region offset')
        size = integer(entry.get('size'), 'region size')
        if size == 0 or address >= MEM1_BYTES or size > MEM1_BYTES - address:
            raise ValueError('snapshot region exceeds physical MEM1 bounds or is empty')
        if offset > len(binary) or size > len(binary) - offset:
            raise ValueError('snapshot region exceeds binary bounds')
        regions.append((address, offset, size))
    regions.sort()
    for previous, current in zip(regions, regions[1:]):
        if previous[0] + previous[2] > current[0]:
            raise ValueError('snapshot physical regions overlap')
    payload_regions = sorted(regions, key=lambda region: region[1])
    for previous, current in zip(payload_regions, payload_regions[1:]):
        if previous[1] + previous[2] > current[1]:
            raise ValueError('snapshot binary regions overlap')
    image = bytearray(MEM1_BYTES)
    ranges = []
    for address, offset, size in regions:
        image[address:address + size] = binary[offset:offset + size]
        if ranges and ranges[-1][0] + ranges[-1][1] == address:
            ranges[-1] = (ranges[-1][0], ranges[-1][1] + size)
        else:
            ranges.append((address, size))
    metadata = dict(format=FORMAT, manifest_source=str(path.resolve()),
                    manifest_sha256=sha256(manifest_bytes), binary_source=str(binary_path),
                    binary_sha256=expected_hash, fifo_sha256=captured_hash,
                    captured_bytes=sum(size for _, _, size in regions),
                    regions=[dict(address=address, offset=offset, size=size)
                             for address, offset, size in regions],
                    ranges_sha256=sha256(coverage_text(ranges).encode()))
    return Snapshot(bytes(image), ranges, binary, manifest_bytes, manifest, metadata)
