#!/usr/bin/env python3
"""What one boot run proves about REL residency.

The host lane reports three markers today, all on stderr and all in the run's
`ww05.log`:

    [rel-frontier]     name=NAME object=OBJECT module=MODULE pc=PC
    [rel-materialized] module=ID name=NAME bytes=B guest=ADDR owner=OBJECT
                       source=SOURCE backend=BACKEND
    [rel-link]         module=ID owner=OBJECT guest=ADDR profile=ADDR
                       bytes=B contract=CONTRACT

`[rel-frontier]` is the guest asking for a module at the archived
materialization edge; `module=0` means the object has no module yet.
`[rel-materialized]` is the raw image copied into owned scratch. `[rel-link]`
is the published link contract. None of them is the ticket-14 `[rel-load]` /
`[rel-unload]` pair, which the host does not emit yet: a resident set derived
here is "materialized and never reported released", and the summary says the
contract markers are absent instead of pretending they are loads.

Each marker is bounded by the recorded `cycles=` values around it in the same
log: the host stamps cycles on progress/edge lines, not on the REL lines, so
the load cycle is an interval, not a point. A malformed REL marker is an error:
a broken log is not a measurement.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Optional

FRONTIER_MARKER = "[rel-frontier]"
MATERIALIZED_MARKER = "[rel-materialized]"
LINK_MARKER = "[rel-link]"
LOAD_MARKER = "[rel-load]"
UNLOAD_MARKER = "[rel-unload]"

_REL_MARKERS = (FRONTIER_MARKER, MATERIALIZED_MARKER, LINK_MARKER)


class BootResidencyError(ValueError):
    """The run evidence is not something this miner can read."""


@dataclass(frozen=True)
class RelRequest:
    kind: str
    name: str
    object: int
    module: int
    pc: int
    before_cycles: Optional[int]
    after_cycles: Optional[int]


@dataclass(frozen=True)
class RelMaterialized:
    kind: str
    module_id: int
    name: str
    bytes: int
    guest: int
    owner: int
    source: str
    before_cycles: Optional[int]
    after_cycles: Optional[int]

    @property
    def capacity(self) -> int:
        return (self.bytes + 31) & ~31


@dataclass(frozen=True)
class RelLink:
    kind: str
    module_id: int
    owner: int
    guest: int
    profile: int
    bytes: int
    contract: str
    before_cycles: Optional[int]
    after_cycles: Optional[int]


def _fields(text: str) -> dict:
    fields = {}
    for token in text.split():
        if "=" in token:
            key, _, value = token.partition("=")
            fields[key] = value
    return fields


def _hex(fields: dict, key: str) -> int:
    try:
        return int(fields[key], 16)
    except (KeyError, ValueError) as error:
        raise BootResidencyError("marker has no usable " + key) from error


def _int(fields: dict, key: str) -> int:
    try:
        return int(fields[key])
    except (KeyError, ValueError) as error:
        raise BootResidencyError("marker has no usable " + key) from error


def parse_residency_events(lines: Iterable[str]) -> list:
    if isinstance(lines, str):
        lines = lines.splitlines()
    lines = list(lines)
    cycle_of = []
    for line in lines:
        cycle = None
        marker = line.find("cycles=")
        if marker >= 0:
            digits = line[marker + len("cycles="):].split()[0]
            if digits.isdigit():
                cycle = int(digits)
        cycle_of.append(cycle)
    events = []
    for index, line in enumerate(lines):
        if not any(marker in line for marker in _REL_MARKERS):
            continue
        before = next((cycle_of[position] for position in range(index - 1, -1, -1)
                       if cycle_of[position] is not None), None)
        after = next((cycle_of[position] for position in range(index + 1, len(lines))
                      if cycle_of[position] is not None), None)
        events.append(_parse_marker(line, before, after))
    return events


def _parse_marker(line: str, before: Optional[int], after: Optional[int]):
    fields = _fields(line)
    if FRONTIER_MARKER in line:
        try:
            name = fields["name"]
        except KeyError as error:
            raise BootResidencyError("malformed frontier marker: " + line) from error
        return RelRequest(kind="request", name=name, object=_hex(fields, "object"),
                          module=_hex(fields, "module"), pc=_hex(fields, "pc"),
                          before_cycles=before, after_cycles=after)
    if MATERIALIZED_MARKER in line:
        try:
            name = fields["name"]
            source = fields["source"]
        except KeyError as error:
            raise BootResidencyError("malformed materialized marker: " + line) from error
        return RelMaterialized(
            kind="materialized", module_id=_int(fields, "module"), name=name,
            bytes=_int(fields, "bytes"), guest=_hex(fields, "guest"),
            owner=_hex(fields, "owner"), source=source,
            before_cycles=before, after_cycles=after)
    if LINK_MARKER in line:
        try:
            contract = fields["contract"]
        except KeyError as error:
            raise BootResidencyError("malformed link marker: " + line) from error
        return RelLink(kind="link", module_id=_int(fields, "module"),
                       owner=_hex(fields, "owner"), guest=_hex(fields, "guest"),
                       profile=_hex(fields, "profile"), bytes=_int(fields, "bytes"),
                       contract=contract, before_cycles=before, after_cycles=after)
    raise BootResidencyError("unreadable REL marker: " + line)


def _parse_kv_line(line: str, prefix: str) -> dict:
    fields = {}
    for token in line[len(prefix):].split():
        if "=" in token:
            key, _, value = token.partition("=")
            try:
                fields[key] = int(value, 0)
            except ValueError:
                fields[key] = value
    return fields


def parse_report(text: str) -> dict:
    report = {"alloc": None, "state": None, "gx_stream": None, "render": None,
              "stop": None, "end": None}
    for line in text.splitlines():
        if line.startswith("ALLOC "):
            report["alloc"] = _parse_kv_line(line, "ALLOC ")
        elif line.startswith("STATE "):
            report["state"] = _parse_kv_line(line, "STATE ")
        elif line.startswith("GX_STREAM "):
            report["gx_stream"] = _parse_kv_line(line, "GX_STREAM ")
        elif line.startswith("RENDER "):
            report["render"] = _parse_kv_line(line, "RENDER ")
        elif line.startswith("STOP "):
            report["stop"] = line[len("STOP "):].removeprefix("reason=")
        elif line.startswith("END "):
            report["end"] = _parse_kv_line(line, "END ")
    if report["state"] is None:
        raise BootResidencyError("the report has no STATE line")
    report["cycles"] = report["state"].get("cycles")
    return report


def _event_dict(event) -> dict:
    entry = {"kind": event.kind, "before_cycles": event.before_cycles,
             "after_cycles": event.after_cycles}
    for field in ("module_id", "name", "bytes", "guest", "owner", "source",
                  "profile", "contract", "object", "module", "pc"):
        value = getattr(event, field, None)
        if value is not None:
            entry[field] = value
    return entry


def summarize(log_text: str, report_text: str) -> dict:
    lines = log_text.splitlines()
    events = parse_residency_events(lines)
    resident = []
    for event in events:
        if isinstance(event, RelMaterialized):
            resident.append({"module_id": event.module_id, "name": event.name,
                             "bytes": event.bytes, "capacity": event.capacity,
                             "guest": event.guest,
                             "loaded_between_cycles": [event.before_cycles,
                                                       event.after_cycles]})
    return {
        "events": [_event_dict(event) for event in events],
        "resident": resident,
        "contract": {
            "rel_load_marker": any(LOAD_MARKER in line for line in lines),
            "rel_unload_marker": any(UNLOAD_MARKER in line for line in lines),
        },
        "report": parse_report(report_text),
    }
