#!/usr/bin/env python3
"""The active-set tracker for the VITA-WW-14 measurement harness.

It replays a REL-event stream and answers the ticket's question: which modules
are resident at the same time, and how big that set gets. Loads add a module,
unloads remove it; the active set is the difference. The report carries the
state after every event (the residency timeline), the high-water mark in bytes
and in module count, each module's residency span, and the largest modules.

A sequence that cannot describe a real active set - a load of an already
loaded module, an unload of one that is not loaded - is refused: the host
emits a load only when a module becomes resident and an unload only when it is
released, so such a stream is broken, not a measurement.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional

from rel_events import RelEvent, RelLoad


class ActiveSetError(ValueError):
    """The event sequence cannot describe a real active set."""


@dataclass(frozen=True)
class TimelineStep:
    seq: int
    kind: str
    module_id: int
    us: Optional[int]
    active_modules: int
    active_bytes: int


@dataclass
class Residency:
    module_id: int
    name: str
    bytes: int
    loads: int = 0
    unloads: int = 0
    first_load_us: Optional[int] = None
    last_unload_us: Optional[int] = None
    open_at_end: bool = False


@dataclass(frozen=True)
class TopModule:
    module_id: int
    name: str
    bytes: int


@dataclass
class ActiveSetReport:
    timeline: List[TimelineStep] = field(default_factory=list)
    peak_bytes: int = 0
    peak_bytes_seq: int = 0
    peak_modules: int = 0
    peak_modules_seq: int = 0
    modules: Dict[int, Residency] = field(default_factory=dict)
    top_modules: List[TopModule] = field(default_factory=list)
    total_loads: int = 0
    total_unloads: int = 0
    unique_modules: int = 0
    cumulative_bytes_loaded: int = 0
    active_modules: int = 0
    active_bytes: int = 0
    active_at_end: Dict[int, str] = field(default_factory=dict)


def track_active_set(events: List[RelEvent]) -> ActiveSetReport:
    report = ActiveSetReport()
    active: Dict[int, RelLoad] = {}
    for seq, event in enumerate(events, start=1):
        if isinstance(event, RelLoad):
            if event.module_id in active:
                raise ActiveSetError(
                    "module " + str(event.module_id)
                    + " loaded while already active at event " + str(seq))
            active[event.module_id] = event
            residency = report.modules.get(event.module_id)
            if residency is None:
                residency = Residency(module_id=event.module_id, name=event.name,
                                      bytes=event.bytes)
                report.modules[event.module_id] = residency
            residency.loads += 1
            if residency.first_load_us is None:
                residency.first_load_us = event.us
            report.total_loads += 1
            report.cumulative_bytes_loaded += event.bytes
        else:
            if event.module_id not in active:
                raise ActiveSetError(
                    "module " + str(event.module_id)
                    + " unloaded while not active at event " + str(seq))
            del active[event.module_id]
            report.modules[event.module_id].unloads += 1
            report.modules[event.module_id].last_unload_us = event.us
            report.total_unloads += 1

        active_bytes = sum(load.bytes for load in active.values())
        if active_bytes > report.peak_bytes:
            report.peak_bytes = active_bytes
            report.peak_bytes_seq = seq
        if len(active) > report.peak_modules:
            report.peak_modules = len(active)
            report.peak_modules_seq = seq
        report.timeline.append(TimelineStep(
            seq=seq, kind="load" if isinstance(event, RelLoad) else "unload",
            module_id=event.module_id,
            us=event.us,
            active_modules=len(active), active_bytes=active_bytes))

    for module_id in active:
        report.modules[module_id].open_at_end = True
    report.unique_modules = len(report.modules)
    report.active_modules = len(active)
    report.active_bytes = sum(load.bytes for load in active.values())
    report.active_at_end = {module_id: load.name
                            for module_id, load in active.items()}
    report.top_modules = [
        TopModule(module_id=residency.module_id, name=residency.name,
                  bytes=residency.bytes)
        for residency in sorted(report.modules.values(),
                                key=lambda entry: (-entry.bytes, entry.module_id))]
    return report
