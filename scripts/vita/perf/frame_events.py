#!/usr/bin/env python3
"""Frame-event streams for the VITA-WW-12 performance harness.

The harness never measures the emulator's own present: it reads frame events
the guest runtime emits. A stream is a log's lines; two markers matter, searched
anywhere in a line so that interleaved logger output cannot hide a stamp (the
desktop bench's scripts/bench_report.py reads [frame-timing] the same way):

    [frame-timing] retrace=N us=T   one VI retrace, host wall microseconds
    [frame-present] frame=N us=T    one presented game frame: a GX display copy
                                    (GXCopyDisp) that reached the display

Everything else in the log is ignored. A marker line whose numbers do not
parse, a repeated index or a wall time that goes backwards is an error: a
broken stream is not a measurement.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Iterable

RETRACE_MARKER = "[frame-timing]"
PRESENT_MARKER = "[frame-present]"

_RETRACE = re.compile(r"\[frame-timing\] retrace=(\d+) us=(\d+)")
_PRESENT = re.compile(r"\[frame-present\] frame=(\d+) us=(\d+)")


class FrameEventError(ValueError):
    """The stream contains a marker that is not a usable measurement."""


@dataclass(frozen=True)
class FrameEvent:
    index: int
    us: int


@dataclass(frozen=True)
class FrameEvents:
    retraces: list
    presents: list

    def present_times_us(self) -> list:
        return [event.us for event in self.presents]

    def retrace_times_us(self) -> list:
        return [event.us for event in self.retraces]


def _check_marker(line: str, pattern: re.Pattern, marker: str) -> FrameEvent | None:
    if marker not in line:
        return None
    match = pattern.search(line)
    if match is None:
        raise FrameEventError("malformed " + marker + " stamp: " + line)
    return FrameEvent(int(match.group(1)), int(match.group(2)))


def _check_series(events: list, marker: str) -> None:
    for previous, current in zip(events, events[1:]):
        if current.index <= previous.index:
            raise FrameEventError(
                marker + " index repeats or goes backwards: "
                + str(previous.index) + " then " + str(current.index))
        if current.us <= previous.us:
            raise FrameEventError(
                marker + " wall time does not advance: "
                + str(previous.us) + " then " + str(current.us))


def parse_frame_events(lines: Iterable[str]) -> FrameEvents:
    if isinstance(lines, str):
        lines = lines.splitlines()
    retraces, presents = [], []
    for line in lines:
        retrace = _check_marker(line, _RETRACE, RETRACE_MARKER)
        if retrace is not None:
            retraces.append(retrace)
            continue
        present = _check_marker(line, _PRESENT, PRESENT_MARKER)
        if present is not None:
            presents.append(present)
    _check_series(retraces, RETRACE_MARKER)
    _check_series(presents, PRESENT_MARKER)
    return FrameEvents(retraces, presents)
