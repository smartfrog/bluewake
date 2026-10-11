#!/usr/bin/env python3
"""The VITA-WW-12 baseline document: one JSON file plus a human summary.

A baseline says what was measured, with which frame source, over which window,
at which build source; a run with no presented frames says so instead of
carrying numbers it does not have. The verdict is against the ticket's target
(60 fps) and floor (30 fps). A marker series with exactly one event is refused:
one stamp cannot produce a rate, and reporting it as unmeasured would hide a
cut stream.
"""

from __future__ import annotations

import json
from pathlib import Path

from fps import FpsError, frame_rate_summary

TICKET = "VITA-WW-12"
KIND = "fps-baseline"
FRAME_SOURCE = "gx-display-copy"
TARGET_FPS = 60
FLOOR_FPS = 30

CAVEATS = [
    "Vita3K timing is not physical Vita timing: the guest runs on the host CPU, "
    "so an FPS here does not predict the 444 MHz handheld",
    "the presented-frame source is the runtime's own display-copy event; "
    "an emulator renderer's present count is not a game frame",
    "the scene, window and build source are part of the measurement: "
    "numbers without them are not comparable",
]

BASELINE_JSON = "baseline.json"
BASELINE_SUMMARY = "summary.txt"


def fps_verdict(metrics: dict | None, target_fps: int, floor_fps: int) -> str:
    if metrics is None:
        return "not measured: no presented frames in the evidence"
    median = metrics["median_window_fps"]
    if median >= target_fps:
        return f"meets the {target_fps} fps target"
    if median >= floor_fps:
        return f"meets the {floor_fps} fps floor, below the {target_fps} fps target"
    return f"below the {floor_fps} fps floor"


def _series_metrics(times_us: list, window_s: float, name: str) -> dict | None:
    if not times_us:
        return None
    if len(times_us) < 2:
        raise FpsError(name + " has one stamp: the stream looks cut")
    return frame_rate_summary(times_us, window_s)


def build_baseline(*, label: str, source: str, window_s: float, events=None,
                   boot: dict | None = None, target_fps: int = TARGET_FPS,
                   floor_fps: int = FLOOR_FPS, device: dict | None = None,
                   scene: dict | None = None, notes: list | None = None) -> dict:
    presents = events.present_times_us() if events is not None else []
    retraces = events.retrace_times_us() if events is not None else []
    metrics = _series_metrics(presents, window_s, "the presented-frame stream")
    retrace_metrics = _series_metrics(retraces, window_s, "the retrace stream")
    return {
        "ticket": TICKET,
        "kind": KIND,
        "label": label,
        "source": source,
        "measured": metrics is not None,
        "frame_source": FRAME_SOURCE,
        "target_fps": target_fps,
        "floor_fps": floor_fps,
        "window_seconds": window_s,
        "device": device,
        "scene": scene,
        "metrics": metrics,
        "retrace_metrics": retrace_metrics,
        "boot_evidence": boot,
        "verdict": fps_verdict(metrics, target_fps, floor_fps),
        "caveats": list(CAVEATS),
        "notes": notes or [],
    }


def _fmt(value: float) -> str:
    return f"{value:.2f}"


def _boot_line(boot: dict) -> str:
    parts = [f"runs={boot['runs']}", f"pass={boot['pass']}", f"fail={boot['fail']}",
             f"rendered={boot['rendered_frames']}"]
    if boot.get("sources"):
        parts.append(f"sources={len(boot['sources'])}")
    if boot["edges"] is not None:
        parts.append(f"edges={boot['edges']['min']}..{boot['edges']['max']}")
    if boot["cycles"] is not None:
        parts.append(f"cycles={boot['cycles']['min']}..{boot['cycles']['max']}")
    return " ".join(parts)


def render_summary(baseline: dict) -> str:
    lines = [f"{TICKET} baseline: {baseline['label']}",
             f"source            {baseline['source']}",
             f"frame source      {baseline['frame_source']}"]
    if baseline["metrics"] is not None:
        metrics = baseline["metrics"]
        lines.extend([
            f"frames            {metrics['frames']}",
            f"window            {baseline['window_seconds']} s",
            f"mean fps          {_fmt(metrics['mean_fps'])}",
            f"median window fps {_fmt(metrics['median_window_fps'])}",
            f"p95 window fps    {_fmt(metrics['p95_window_fps'])}",
            f"min window fps    {_fmt(metrics['min_window_fps'])}",
            f"p95 frame ms      {_fmt(metrics['p95_frame_ms'])}",
        ])
    else:
        lines.append("frames            none presented")
    if baseline["retrace_metrics"] is not None:
        lines.append(f"retrace fps       {_fmt(baseline['retrace_metrics']['mean_fps'])}")
    lines.append(f"target            {baseline['target_fps']} fps (floor {baseline['floor_fps']})")
    lines.append(f"verdict           {baseline['verdict']}")
    if baseline["boot_evidence"] is not None:
        lines.append(f"boot evidence     {_boot_line(baseline['boot_evidence'])}")
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
