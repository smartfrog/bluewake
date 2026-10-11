#!/usr/bin/env python3
"""REL lifecycle event streams for the VITA-WW-14 active-set harness.

The harness reads what the host reports, never the emulator's own module list.
A stream is a log's lines; two markers matter, searched anywhere in a line so
that interleaved logger output cannot hide a stamp:

    [rel-load] module=ID name=NAME bytes=B us=T   a REL became resident
    [rel-unload] module=ID [us=T]                 a REL was released

ID is the authentic REL module id (the first big-endian word of the REL
header, the same id DolRecomp and the composite use). NAME is the module name
the guest requested, without spaces. B is the byte size of the materialized
REL image (the raw .rel size; the guest allocation is this rounded up to 32).
T is the host's monotonic wall clock in microseconds, the same base as the
frame markers when both exist; the load carries it always, the unload carries
it when the host can stamp the release (recommended: it fixes the residency
span end).

Everything else in the log is ignored. A malformed marker or a wall time that
does not advance across timed events is an error: a broken stream is not a
measurement. Whether the sequence makes sense as an active set (a load of an
already-loaded module, an unload of one that is not loaded) is the tracker's
check.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Iterable, Optional, Union

LOAD_MARKER = "[rel-load]"
UNLOAD_MARKER = "[rel-unload]"

_LOAD = re.compile(r"\[rel-load\]\s+module=(\d+)\s+name=(\S+)\s+bytes=(\d+)\s+us=(\d+)")
_UNLOAD = re.compile(r"\[rel-unload\]\s+module=(\d+)(?:\s+us=(\d+))?")


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
    us: Optional[int] = None


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
            previous_us = _check_us(event.us, previous_us, line)
            events.append(event)
            continue
        if UNLOAD_MARKER in line:
            match = _UNLOAD.search(line)
            if match is None:
                raise RelEventError("malformed " + UNLOAD_MARKER + " stamp: " + line)
            us = int(match.group(2)) if match.group(2) is not None else None
            if us is not None:
                previous_us = _check_us(us, previous_us, line)
            events.append(RelUnload(module_id=int(match.group(1)), us=us))
    return events


def _check_us(us: int, previous_us: Optional[int], line: str) -> int:
    if previous_us is not None and us <= previous_us:
        raise RelEventError(
            "wall time does not advance: " + str(previous_us) + " then " + str(us))
    return us
