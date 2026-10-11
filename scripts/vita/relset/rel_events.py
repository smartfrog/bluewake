#!/usr/bin/env python3
"""REL lifecycle event streams for the VITA-WW-14 active-set harness.

The harness reads what the host reports, never the emulator's own module list.
A stream is a log's lines; two markers matter, searched anywhere in a line so
that interleaved logger output cannot hide a stamp:

    [rel-load] module=ID name=NAME bytes=B us=T   a REL became resident
    [rel-unload] module=ID                        a REL was released

ID is the authentic REL module id (the first big-endian word of the REL
header, the same id DolRecomp and the composite use). NAME is the module name
the guest requested, without spaces. B is the byte size of the materialized
REL image (the raw .rel size; the guest allocation is this rounded up to 32).
T is the host's monotonic wall clock in microseconds, the same base as the
frame markers when both exist.

Everything else in the log is ignored. A malformed marker or a load wall time
that does not advance is an error: a broken stream is not a measurement.
Whether the sequence makes sense as an active set (a load of an already-loaded
module, an unload of one that is not loaded) is the tracker's check.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Iterable, Union

LOAD_MARKER = "[rel-load]"
UNLOAD_MARKER = "[rel-unload]"

_LOAD = re.compile(r"\[rel-load\]\s+module=(\d+)\s+name=(\S+)\s+bytes=(\d+)\s+us=(\d+)")
_UNLOAD = re.compile(r"\[rel-unload\]\s+module=(\d+)")


class RelEventError(ValueError):
    """The stream contains a marker that is not a usable measurement."""


@dataclass(frozen=True)
class RelLoad:
    module_id: int
    name: str
    bytes: int
    us: int


@dataclass(frozen=True)
class RelUnload:
    module_id: int


RelEvent = Union[RelLoad, RelUnload]


def parse_rel_events(lines: Iterable[str]) -> list:
    if isinstance(lines, str):
        lines = lines.splitlines()
    events = []
    previous_us = None
    for line in lines:
        if LOAD_MARKER in line:
            match = _LOAD.search(line)
            if match is None:
                raise RelEventError("malformed " + LOAD_MARKER + " stamp: " + line)
            event = RelLoad(module_id=int(match.group(1)), name=match.group(2),
                            bytes=int(match.group(3)), us=int(match.group(4)))
            if event.bytes <= 0:
                raise RelEventError("load with zero bytes: " + line)
            if previous_us is not None and event.us <= previous_us:
                raise RelEventError(
                    "load wall time does not advance: "
                    + str(previous_us) + " then " + str(event.us))
            previous_us = event.us
            events.append(event)
            continue
        if UNLOAD_MARKER in line:
            match = _UNLOAD.search(line)
            if match is None:
                raise RelEventError("malformed " + UNLOAD_MARKER + " stamp: " + line)
            events.append(RelUnload(module_id=int(match.group(1))))
    return events
