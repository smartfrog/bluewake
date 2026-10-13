#!/usr/bin/env python3
"""Per-module compiled REL footprint from the ww05 link map.

The translator emits each REL chunk as `chunk_NNNN_rel1_<BASE>.c.obj`; the
link map records what each chunk actually occupies in the linked module. A
module's chunks sit at its guest code base and spill into the following slots
when there are more than four (each chunk is 16 KiB), so the owning module is
the one with the greatest base not after the chunk - not the base rounded to
64 KiB. Reading the map is what makes the demand-loading projection an
arithmetic statement about this build instead of a ticket estimate.

The raw `.rel` sizes come from the verified importer output (415 files, checked
byte-identical). No game bytes are stored here - only ids, names, addresses and
sizes.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import bisect
import re

_CHUNK = re.compile(
    r"^\s+\.(text|rodata|data|bss)\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)\s+"
    r"\S+\(chunk_(\d+)_rel1_([0-9a-fA-F]+)\.c\.obj\)\s*$")
_SECTION_LIKE = re.compile(r"^\s+\.(text|rodata|data|bss)\s+0x")
_MODULE = re.compile(r"^\s+module (\d+): (\S+) -> base 0x([0-9A-Fa-f]+)\s*$")


class RelFootprintError(ValueError):
    """The text is not a build map this harness can read."""


@dataclass(frozen=True)
class ChunkSection:
    chunk_index: int
    base: int
    section: str
    bytes: int


@dataclass(frozen=True)
class ModuleFootprint:
    module_id: int
    name: str
    base: int
    raw_bytes: int
    text_bytes: int
    rodata_bytes: int
    chunks: int

    @property
    def compiled_bytes(self) -> int:
        return self.text_bytes + self.rodata_bytes


@dataclass(frozen=True)
class Catalog:
    modules: tuple

    @property
    def compiled_bytes(self) -> int:
        return sum(module.compiled_bytes for module in self.modules)

    @property
    def raw_bytes(self) -> int:
        return sum(module.raw_bytes for module in self.modules)

    def largest(self, count: int) -> list:
        return sorted(self.modules, key=lambda module: module.compiled_bytes,
                      reverse=True)[:count]


def parse_chunk_sections(text: str) -> list:
    sections = []
    for line in text.splitlines():
        if "_rel1_" not in line or not _SECTION_LIKE.match(line):
            continue
        match = _CHUNK.match(line)
        if match is None:
            raise RelFootprintError("malformed chunk section line: " + line)
        sections.append(ChunkSection(chunk_index=int(match.group(3)),
                                     base=int(match.group(4), 16),
                                     section=match.group(1),
                                     bytes=int(match.group(2), 16)))
    return sections


def _parse_modules(text: str) -> list:
    modules = []
    seen = set()
    for line in text.splitlines():
        match = _MODULE.match(line)
        if match is None:
            continue
        module_id = int(match.group(1))
        base = int(match.group(3), 16)
        if module_id in seen:
            raise RelFootprintError("duplicate module id " + str(module_id))
        if base in seen:
            raise RelFootprintError("duplicate module base " + hex(base))
        seen.add(module_id)
        seen.add(base)
        modules.append((module_id, Path(match.group(2)).stem, base))
    return sorted(modules, key=lambda entry: entry[2])


def build_catalog(translate_text: str, map_text: str,
                  raw_sizes: dict) -> Catalog:
    modules = _parse_modules(translate_text)
    if not modules:
        raise RelFootprintError("the translator log lists no modules")
    bases = [entry[2] for entry in modules]
    text_bytes = {entry[0]: 0 for entry in modules}
    rodata_bytes = {entry[0]: 0 for entry in modules}
    chunks = {entry[0]: set() for entry in modules}
    for section in parse_chunk_sections(map_text):
        index = bisect.bisect_right(bases, section.base) - 1
        if index < 0:
            raise RelFootprintError(
                "chunk at " + hex(section.base) + " precedes every module base")
        module_id = modules[index][0]
        if section.section == "text":
            text_bytes[module_id] += section.bytes
        elif section.section == "rodata":
            rodata_bytes[module_id] += section.bytes
        chunks[module_id].add((section.chunk_index, section.base))
    footprints = []
    for module_id, name, base in modules:
        if name not in raw_sizes:
            raise RelFootprintError("module " + name + " has no raw REL size")
        footprints.append(ModuleFootprint(
            module_id=module_id, name=name, base=base,
            raw_bytes=int(raw_sizes[name]), text_bytes=text_bytes[module_id],
            rodata_bytes=rodata_bytes[module_id], chunks=len(chunks[module_id])))
    return Catalog(modules=tuple(footprints))
