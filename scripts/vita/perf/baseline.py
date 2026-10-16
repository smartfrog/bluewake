#!/usr/bin/env python3
"""The VITA-WW-12 baseline document: one JSON file plus a human summary.

A baseline says what was measured, with which frame source, over which window,
at which build source; a run with no presented frames says so instead of
carrying numbers it does not have. The verdict is against the ticket's target
(60 fps) and floor (30 fps), requiring both median and minimum window FPS.
A marker series with exactly one event is refused:
one stamp cannot produce a rate, and reporting it as unmeasured would hide a
cut stream.
"""

from __future__ import annotations

import json
import math
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
    minimum = metrics["min_window_fps"]
    if median >= target_fps and minimum >= target_fps:
        return f"meets the {target_fps} fps target"
    if median >= floor_fps and minimum >= floor_fps:
        return f"meets the {floor_fps} fps floor, below the {target_fps} fps target"
    return f"below the {floor_fps} fps floor"


def _series_metrics(times_us: list, window_s: float, name: str, end_us: int | None = None) -> dict | None:
    if not times_us:
        return None
    if len(times_us) < 2:
        raise FpsError(name + " has one stamp: the stream looks cut")
    return frame_rate_summary(times_us, window_s, end_us)


def build_baseline(*, label: str, source: str, window_s: float, events=None,
                   boot: dict | None = None, target_fps: int = TARGET_FPS,
                   floor_fps: int = FLOOR_FPS, device: dict | None = None,
                   scene: dict | None = None, notes: list | None = None,
                   end_us: int | None = None) -> dict:
    presents = events.present_times_us() if events is not None else []
    retraces = events.retrace_times_us() if events is not None else []
    metrics = _series_metrics(presents, window_s, "the presented-frame stream", end_us)
    retrace_metrics = _series_metrics(retraces, window_s, "the retrace stream", end_us)
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
        "observation_start_us": presents[0] if presents else None,
        "observation_end_us": end_us,
        "device": device,
        "scene": scene,
        "metrics": metrics,
        "retrace_metrics": retrace_metrics,
        "boot_evidence": boot,
        "verdict": fps_verdict(metrics, target_fps, floor_fps),
        "caveats": list(CAVEATS),
        "notes": notes or [],
    }


def _text(value) -> bool:
    return isinstance(value, str) and bool(value.strip())


def _number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _status(missing: list, failed: list) -> str:
    return "incomplete" if missing else "fail" if failed else "pass"


def _gameplay_passage(baseline: dict) -> dict:
    missing, failed, fps_missing, fps_failed = [], [], [], []
    scene = baseline.get("scene") or {}
    device = baseline.get("device") or {}
    for key in ("passage_id", "context", "control_evidence", "visual_evidence",
                "observation_evidence"):
        if not _text(scene.get(key)):
            missing.append(f"missing scene.{key}")
    if scene.get("phase") != "gameplay":
        missing.append("explicit gameplay phase required")
    for key in ("controlled", "visually_correct"):
        if scene.get(key) is False:
            failed.append(f"scene.{key} is false")
        elif scene.get(key) is not True:
            missing.append(f"explicit scene.{key} observation required")
    if not _text(baseline.get("source")):
        missing.append("missing build source identity")
    if not all(_text(device.get(key)) for key in ("name", "kind")):
        missing.append("missing device name or kind (physical/emulator)")

    start, end = baseline.get("observation_start_us"), baseline.get("observation_end_us")
    duration = (end - start) / 1e6 if _number(start) and _number(end) else None
    metrics = baseline.get("metrics") or {}
    if baseline.get("kind") != KIND or baseline.get("frame_source") != FRAME_SOURCE:
        fps_missing.append("presented-frame baseline required")
    if duration is None or duration < 60:
        fps_missing.append("explicit observation start/end spanning at least 60 s required")
    if baseline.get("window_seconds") != 10 or metrics.get("window_seconds") != 10:
        fps_missing.append("10 s windows required")
    windows = metrics.get("windows")
    if (not _number(windows) or windows < 6 or duration is None
            or windows != int(duration / 10)):
        fps_missing.append("at least six complete windows through the observation end required")
    span = metrics.get("span_seconds")
    if not _number(span) or duration is None or not 0 < span < duration:
        fps_missing.append("observation end must follow the last presented frame")
    for key in ("median_window_fps", "min_window_fps"):
        rate = metrics.get(key)
        if not _number(rate) or rate < 0:
            fps_missing.append(f"missing or invalid {key}")
        elif rate < FLOOR_FPS:
            fps_failed.append(f"{key} below {FLOOR_FPS} fps")
    return {
        "passage_id": scene.get("passage_id"),
        "observation_seconds": duration,
        "fps_status": _status(fps_missing, fps_failed),
        "status": _status(missing + fps_missing, failed + fps_failed),
        "missing": missing + fps_missing,
        "failed": failed + fps_failed,
    }


def build_gameplay_acceptance(passages: list[dict]) -> dict:
    """Aggregate three declared gameplay baselines; see the scene schema below.

    Each baseline needs device.name/kind, source, and an explicit observation
    end. scene contains phase="gameplay", a distinct passage_id, context,
    controlled/visually_correct booleans, and nonempty control_evidence,
    visual_evidence and observation_evidence references. These are observer
    declarations, not facts inferred from frame stamps or scene labels.

    Exactly three passages from the same declared device/build are required.
    Each spans >=60 s with >=6 complete 10 s windows, median AND minimum >=30.
    Windows remain anchored at the first presented frame; frame_rate_summary
    includes complete stalled-tail windows through the declared endpoint.
    A final partial window is excluded, as in the generic baseline.
    Missing evidence is incomplete, observed failure is fail, otherwise pass.
    Generic boot/phase baselines and the existing CLI never invoke this gate.
    """
    results = [_gameplay_passage(passage) for passage in passages]
    missing = []
    ids = [result["passage_id"] for result in results]
    if len(ids) != 3 or not all(_text(value) for value in ids) or len(set(ids)) != 3:
        missing.append("exactly three distinct identified gameplay passages required")
    if passages and any(passage.get(key) != passages[0].get(key)
                        for passage in passages for key in ("source", "device")):
        missing.append("passages must share a declared source and device")
    return {
        "kind": "gameplay-acceptance",
        "status": _status(missing or [r for r in results if r["status"] == "incomplete"],
                          [r for r in results if r["status"] == "fail"]),
        "missing": missing,
        "passages": results,
        "baselines": passages,
        "caveats": list(CAVEATS) + [
            "control, visual correctness, observation and device/source identity are declared "
            "evidence; this report does not independently verify those declarations"],
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
