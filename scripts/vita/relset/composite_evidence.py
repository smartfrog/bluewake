#!/usr/bin/env python3
"""Static REL-catalog evidence from the ww05 lane's real build logs.

The active-set harness measures what the host reports at run time; this module
reads what the build already proves. The translator prints one line per REL
with its authentic module id, name and linked base; the composite generator
prints the chunk and module totals. The parser refuses a module line that does
not match the documented shape: a broken log is not evidence.

No game bytes and no private paths are stored - only the id, the module name
and the linked base the logs carry.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

_MODULE_CANDIDATE = re.compile(r"^\s+module \S+:")
_MODULE = re.compile(r"^\s+module (\d+): (\S+) -> base 0x([0-9A-Fa-f]+)\s*$")
_TABLE_ID = re.compile(r"^\s*\{(\d+)u, \d+u, 0x[0-9A-Fa-f]+u, 0x[0-9A-Fa-f]+u\},")
_DOL = re.compile(r"^DOL: (\d+) chunks, (\d+) ranges$")
_RELS = re.compile(r"^RELs: (\d+) modules$")
_COMPOSITE = re.compile(r"^Composite: (\d+) chunks, (\d+) code ranges$")


class CompositeEvidenceError(ValueError):
    """The text is not a build log this harness can read."""


@dataclass(frozen=True)
class RelModule:
    module_id: int
    name: str
    base: int


@dataclass(frozen=True)
class CompositeSummary:
    dol_chunks: int
    rel_modules: int
    composite_chunks: int
    code_ranges: int

    @property
    def rel_chunks(self) -> int:
        return self.composite_chunks - self.dol_chunks


def parse_rel_module_lines(text: str) -> list:
    entries = []
    for line in text.splitlines():
        if _MODULE_CANDIDATE.match(line) is None:
            continue
        match = _MODULE.match(line)
        if match is None:
            raise CompositeEvidenceError("malformed module line: " + line)
        entries.append(RelModule(module_id=int(match.group(1)),
                                 name=Path(match.group(2)).stem,
                                 base=int(match.group(3), 16)))
    return entries


def parse_composite_log(text: str) -> CompositeSummary:
    dol = rels = composite = None
    for line in text.splitlines():
        match = _DOL.match(line)
        if match is not None:
            dol = int(match.group(1))
            continue
        match = _RELS.match(line)
        if match is not None:
            rels = int(match.group(1))
            continue
        match = _COMPOSITE.match(line)
        if match is not None:
            composite = (int(match.group(1)), int(match.group(2)))
    if dol is None or rels is None or composite is None:
        raise CompositeEvidenceError(
            "the composite log has no DOL/RELs/Composite summary lines")
    return CompositeSummary(dol_chunks=dol, rel_modules=rels,
                            composite_chunks=composite[0],
                            code_ranges=composite[1])


def parse_rel_module_table(text: str) -> set:
    ids = set()
    for line in text.splitlines():
        match = _TABLE_ID.match(line)
        if match is not None:
            ids.add(int(match.group(1)))
    return ids
