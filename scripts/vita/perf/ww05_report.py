#!/usr/bin/env python3
"""Boot-frontier evidence from WW05 runs, for the VITA-WW-12 harness.

Each run of the boot fixture writes one report with a STATE line carrying the
guest's edge count and absolute cycles at the stop, and a verdict line that
today always says rendered=0: no frame has ever been presented. This parser
extracts those numbers for a run series. It does not certify the frontier;
scripts/vita/ww05/report.py does that. The report shapes are the contract in
that validator: the complete form (eight lines) and the short allocation
failure (three lines).

What the counters mean: edges is how many times the host edge service ran,
cycles is the cycle domain's absolute cycle count when the guest stopped. They
measure boot progress in guest time, not frames, and not wall time.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
import statistics

HEADER = re.compile(
    r"^WW05 ([0-9a-f]{32}) ([0-9a-f]{40}) ([0-9a-f]{64}) "
    r"method=startup-gx-v1 phase=guest platform=(\S+)$")
STATE = re.compile(
    r"^STATE entry=([0-9A-F]{8}) pc=([0-9A-F]{8}) sp=([0-9A-F]{8}) "
    r"edges=(\d+) exception=([0-9A-F]{8}) cycles=(\d+)$")
FRONTIER = re.compile(r"^GX_FRONTIER complete=([01]) opcode=([0-9A-F]{2}) bytes=(\d+) ")
STOP = re.compile(r"^STOP reason=(.*?) address=([0-9A-F]{8}) pc=([0-9A-F]{8})$")
STOP_SHORT = re.compile(r"^STOP reason=([^=]+)$")
END = re.compile(r"^END frontier=(PASS|FAIL) live=(\d+) rendered=(\d+)$")


class BootReportError(ValueError):
    """The text is not a WW05 report this harness can read."""


@dataclass(frozen=True)
class BootReport:
    token: str
    source: str
    package: str
    platform: str
    verdict: str
    rendered: int
    reason: str
    edges: int | None
    cycles: int | None
    exception: int | None
    frontier_complete: bool
    frontier_opcode: int | None


def parse_boot_report(text: str) -> BootReport:
    lines = text.splitlines()
    header = HEADER.match(lines[0]) if lines else None
    if header is None:
        raise BootReportError("not a WW05 report header: " + (lines[0] if lines else "<empty>"))
    verdict_line = END.match(lines[-1]) if lines else None
    if verdict_line is None:
        raise BootReportError("WW05 report has no closing verdict line")
    state = next((STATE.match(line) for line in lines if STATE.match(line)), None)
    frontier = next((FRONTIER.match(line) for line in lines if FRONTIER.match(line)), None)
    stop = next((STOP.match(line) or STOP_SHORT.match(line) for line in lines
                 if STOP.match(line) or STOP_SHORT.match(line)), None)
    if state is None and stop is None:
        raise BootReportError("WW05 report has neither a STATE nor a STOP line")
    if state is not None and frontier is None:
        raise BootReportError("WW05 report has a STATE line but no GX_FRONTIER line")
    return BootReport(
        token=header.group(1), source=header.group(2), package=header.group(3),
        platform=header.group(4),
        verdict=verdict_line.group(1), rendered=int(verdict_line.group(3)),
        reason=stop.group(1) if stop is not None else "",
        edges=int(state.group(4)) if state is not None else None,
        cycles=int(state.group(6)) if state is not None else None,
        exception=int(state.group(5), 16) if state is not None else None,
        frontier_complete=frontier is not None and frontier.group(1) == "1",
        frontier_opcode=int(frontier.group(2), 16) if frontier is not None else None,
    )


def boot_series(reports: list) -> dict:
    if not reports:
        raise BootReportError("no reports: nothing to summarize")
    edges = [report.edges for report in reports if report.edges is not None]
    cycles = [report.cycles for report in reports if report.cycles is not None]
    return {
        "runs": len(reports),
        "pass": sum(1 for report in reports if report.verdict == "PASS"),
        "fail": sum(1 for report in reports if report.verdict == "FAIL"),
        "rendered_frames": sum(report.rendered for report in reports),
        "sources": sorted({report.source for report in reports}),
        "edges": _stats(edges),
        "cycles": _stats(cycles),
    }


def _stats(values: list) -> dict | None:
    if not values:
        return None
    return {"min": min(values), "median": float(statistics.median(values)),
            "max": max(values)}
