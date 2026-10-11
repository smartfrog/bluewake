#!/usr/bin/env python3
"""The VITA-WW-14 baseline document: one JSON file plus a human summary.

A baseline says what was measured, over which scene, at which build source,
and what the static REL catalog is known to hold today. A run with no REL
events says so instead of carrying numbers it does not have: "not measured" is
the only honest verdict before the host lane emits the markers. A measured
run's verdict compares its peak active set against the ticket's combined
ceiling (~215 MiB for active RELs + GPU + misc); fitting under it is a
necessary condition, not a proof that the demand-loading design works.
"""

from __future__ import annotations

import json
from pathlib import Path

from active_set import track_active_set

TICKET = "VITA-WW-14"
KIND = "rel-active-set-baseline"
ACTIVE_SET_CEILING_MIB = 215
MIB = 1024 * 1024

KNOWN_PICTURE = {
    "modules": 415,
    "rel_chunks": 542,
    "rel_code_mib": 183.1,
    "source": "VITA-WW-14 ticket and the integration notes",
}

CAVEATS = [
    "the markers are the host's report of guest module events; a missing "
    "unload overstates residency and the harness cannot detect one",
    "the active set covers REL modules only: the resident composite, the GPU, "
    "the heap and other allocations are outside it",
    "wall times are host microseconds; in Vita3K they are not physical-Vita time",
    "the 183.1 MiB REL-code figure is the known integration picture, not a "
    "measurement of this run",
]

BASELINE_JSON = "baseline.json"
BASELINE_SUMMARY = "summary.txt"


def active_set_verdict(metrics: dict | None) -> str:
    if metrics is None:
        return "not measured: no REL events in the evidence"
    peak = metrics["peak_mib"]
    if peak >= ACTIVE_SET_CEILING_MIB:
        return (f"peak active REL set {peak:.2f} MiB reaches the ticket's "
                f"~{ACTIVE_SET_CEILING_MIB} MiB combined ceiling "
                "(RELs + GPU + misc)")
    return (f"peak active REL set {peak:.2f} MiB, under the ticket's "
            f"~{ACTIVE_SET_CEILING_MIB} MiB combined ceiling "
            "(RELs + GPU + misc); a necessary condition only")


def _metrics(report) -> dict:
    return {
        "peak_bytes": report.peak_bytes,
        "peak_mib": round(report.peak_bytes / MIB, 2),
        "peak_modules": report.peak_modules,
        "peak_bytes_seq": report.peak_bytes_seq,
        "peak_modules_seq": report.peak_modules_seq,
        "active_at_end_modules": report.active_modules,
        "active_at_end_bytes": report.active_bytes,
        "total_loads": report.total_loads,
        "total_unloads": report.total_unloads,
        "unique_modules": report.unique_modules,
        "cumulative_bytes_loaded": report.cumulative_bytes_loaded,
        "top_modules": [
            {"module_id": entry.module_id, "name": entry.name, "bytes": entry.bytes}
            for entry in report.top_modules],
        "modules": {
            str(residency.module_id): {
                "name": residency.name,
                "bytes": residency.bytes,
                "loads": residency.loads,
                "unloads": residency.unloads,
                "first_load_us": residency.first_load_us,
                "last_unload_us": residency.last_unload_us,
                "open_at_end": residency.open_at_end,
            }
            for residency in report.modules.values()},
        "timeline": [
            {"seq": step.seq, "kind": step.kind, "module_id": step.module_id,
             "us": step.us, "active_modules": step.active_modules,
             "active_bytes": step.active_bytes}
            for step in report.timeline],
    }


def build_baseline(*, label: str, source: str, scene: str | None = None,
                   events=None, static: dict | None = None,
                   notes: list | None = None) -> dict:
    report = track_active_set(events) if events is not None else None
    metrics = _metrics(report) if report is not None else None
    return {
        "ticket": TICKET,
        "kind": KIND,
        "label": label,
        "source": source,
        "scene": scene,
        "measured": metrics is not None,
        "metrics": metrics,
        "static": {"known_picture": KNOWN_PICTURE, "evidence": static},
        "verdict": active_set_verdict(metrics),
        "caveats": list(CAVEATS),
        "notes": notes or [],
    }


def render_summary(baseline: dict) -> str:
    lines = [f"{TICKET} baseline: {baseline['label']}",
             f"source            {baseline['source']}",
             f"scene             {baseline['scene'] or 'unrecorded'}",
             f"measured          {'yes' if baseline['measured'] else 'no'}"]
    metrics = baseline["metrics"]
    if metrics is not None:
        lines.extend([
            f"peak active       {metrics['peak_mib']:.2f} MiB / "
            f"{metrics['peak_modules']} modules "
            f"(bytes at event {metrics['peak_bytes_seq']}, "
            f"modules at event {metrics['peak_modules_seq']})",
            f"active at end     {metrics['active_at_end_modules']} modules / "
            f"{metrics['active_at_end_bytes']} bytes",
            f"loads/unloads     {metrics['total_loads']} / {metrics['total_unloads']}",
            f"unique modules    {metrics['unique_modules']}",
            f"bytes loaded      {metrics['cumulative_bytes_loaded']}",
        ])
        for entry in metrics["top_modules"][:5]:
            lines.append(f"top module        {entry['name']} "
                         f"module={entry['module_id']} bytes={entry['bytes']}")
    known = baseline["static"]["known_picture"]
    lines.append(f"known picture     {known['modules']} modules, "
                 f"{known['rel_chunks']} REL chunks, {known['rel_code_mib']} MiB "
                 f"of REL code ({known['source']})")
    evidence = baseline["static"]["evidence"]
    if evidence is not None:
        lines.append(f"static evidence   dol_chunks={evidence.get('dol_chunks')} "
                     f"rel_modules={evidence.get('rel_modules')} "
                     f"rel_chunks={evidence.get('rel_chunks')} "
                     f"code_ranges={evidence.get('code_ranges')}")
    lines.append(f"verdict           {baseline['verdict']}")
    for caveat in baseline["caveats"]:
        lines.append(f"caveat            {caveat}")
    for note in baseline["notes"]:
        lines.append(f"note              {note}")
    return "\n".join(lines) + "\n"


def write_baseline(out_dir: Path, baseline: dict) -> tuple:
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path = out_dir / BASELINE_JSON
    summary_path = out_dir / BASELINE_SUMMARY
    json_path.write_text(json.dumps(baseline, indent=2) + "\n")
    summary_path.write_text(render_summary(baseline))
    return json_path, summary_path
