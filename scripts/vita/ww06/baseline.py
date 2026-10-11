#!/usr/bin/env python3
"""The VITA-WW-06 acceptance baseline: one JSON file plus a human summary.

A baseline says which sessions were read, what each acceptance requirement
showed, and at which build source. A run with no acceptance events says so
instead of carrying verdicts it does not have: "not measured" is the only
honest verdict before the host lane emits the markers.
"""

from __future__ import annotations

import json
from pathlib import Path

from scenario import evaluate_scenario

TICKET = "VITA-WW-06"
KIND = "acceptance-baseline"

CAVEATS = [
    "the markers are the host's report; movement and camera are proven as "
    "input served and consumed by the game's controller code, not as Link's "
    "displacement - that is a visual check on the hardware path",
    "Vita3K cannot read back the picture: rendered output and audibility are "
    "hardware-path checks, not log checks",
    "wall times are host microseconds; in Vita3K they are not physical-Vita "
    "time, and the five-minute session is a protocol, not a wall-clock gate",
    "a scripted pad schedule is a measurement instrument; it is not a human "
    "session",
    "save/reload needs two sessions in order: the save session first, the "
    "reload session after it",
    "the acceptance channel is SI channel 0 (player one); other channels are "
    "informational",
]

BASELINE_JSON = "baseline.json"
BASELINE_SUMMARY = "summary.txt"


def _session_record(name: str, path: str, events) -> dict:
    stamps = []
    for group in (events.input_frames, events.input_consumed, events.audio_active,
                  events.audio_silent, events.scene_loads, events.save_writes,
                  events.save_reads, events.save_errors):
        stamps.extend(event.us for event in group)
    return {
        "name": name,
        "path": path,
        "empty": events.is_empty(),
        "span_us": (max(stamps) - min(stamps)) if stamps else 0,
        "input_frames": len(events.input_frames),
        "input_consumed": len(events.input_consumed),
        "audio_active": len(events.audio_active),
        "audio_silent": len(events.audio_silent),
        "scene_loads": len(events.scene_loads),
        "save_writes": len(events.save_writes),
        "save_reads": len(events.save_reads),
        "save_errors": len(events.save_errors),
    }


def build_baseline(*, label: str, source: str, sessions, deadzone: int,
                   min_frames: int, scene: str | None = None,
                   notes: list | None = None) -> dict:
    result = evaluate_scenario(sessions, deadzone=deadzone, min_frames=min_frames)
    records = [_session_record(name, path, events) for name, path, events in sessions]
    return {
        "ticket": TICKET,
        "kind": KIND,
        "label": label,
        "source": source,
        "scene": scene,
        "measured": any(not record["empty"] for record in records),
        "sessions": records,
        "requirements": result["requirements"],
        "verdict": result["verdict"],
        "caveats": list(CAVEATS),
        "notes": notes or [],
    }


def render_summary(baseline: dict) -> str:
    lines = [f"{TICKET} acceptance baseline: {baseline['label']}",
             f"source            {baseline['source']}",
             f"scene             {baseline['scene'] or 'unrecorded'}",
             f"sessions          {len(baseline['sessions'])}"]
    for record in baseline["sessions"]:
        lines.append(
            f"{record['name']:<17} frames={record['input_frames']} "
            f"consumed={record['input_consumed']} "
            f"audio={record['audio_active']}/{record['audio_silent']} "
            f"scenes={record['scene_loads']} "
            f"saves={record['save_writes']}/{record['save_reads']} "
            f"errors={record['save_errors']} span={record['span_us']} us")
    for name, requirement in baseline["requirements"].items():
        observed = requirement["observed"]
        state = "not measured" if observed is None else ("observed" if observed else "FAILED")
        lines.append(f"{name:<17} {state}: {requirement['detail']}")
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
