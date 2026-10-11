#!/usr/bin/env python3
"""The VITA-WW-06 five-minute Outset scenario check.

Reads parsed acceptance events and answers one question per acceptance
requirement of the ticket: movement, camera, audio, transition, save/reload.
Each answer is observed (True), failed (False) or not measured (None); a
family whose markers never appear is not measured, while a family that appears
but does not show the required behaviour has failed. The overall verdict is
"acceptance met" only when every requirement is observed, "acceptance not met"
when any has failed, and "not measured" otherwise.

What each requirement means in the evidence:

- movement: a sustained channel-0 left-stick deflection (at least min_frames
  consecutive frames at or beyond the deadzone) with at least one
  [input-consumed] record change inside that span. It proves the pad state was
  served and the game's own controller code read it; Link's displacement
  itself is a visual check on the hardware path.
- camera: the same for the substick (the C stick), which drives the camera.
- audio: at least one [audio-active] and the last audio event is not a
  [audio-silent] that never returned. Audibility itself is a hardware check;
  this proves the guest's audio stream ran.
- transition: at least two distinct [scene-load] names - a load that is not
  the starting scene.
- save/reload: an earlier session wrote a save ([save-write], bytes > 0) and
  a later session read it back ([save-read]); a [save-error] anywhere fails
  the requirement loudly. One session alone is not measured: the reload needs
  its own session.
"""

from __future__ import annotations

DEFAULT_DEADZONE = 30
DEFAULT_MIN_FRAMES = 30

REQUIREMENTS = ("movement", "camera", "audio", "transition", "save_reload")


def evaluate_scenario(runs, deadzone: int = DEFAULT_DEADZONE,
                      min_frames: int = DEFAULT_MIN_FRAMES) -> dict:
    requirement_results = {
        "movement": _axis_requirement(runs, deadzone, min_frames, "stick",
                                      "left stick"),
        "camera": _axis_requirement(runs, deadzone, min_frames, "sub",
                                    "C stick"),
        "audio": _audio_requirement(runs),
        "transition": _transition_requirement(runs),
        "save_reload": _save_requirement(runs),
    }
    observed = [result["observed"] for result in requirement_results.values()]
    if any(value is False for value in observed):
        verdict = "acceptance not met"
    elif all(value is True for value in observed):
        verdict = "acceptance met"
    else:
        verdict = "not measured"
    return {"verdict": verdict, "requirements": requirement_results,
            "sessions": len(runs)}


def _deflected(frame, deadzone: int, axis: str) -> bool:
    if axis == "stick":
        x, y = frame.stick_x, frame.stick_y
    else:
        x, y = frame.sub_x, frame.sub_y
    return x * x + y * y >= deadzone * deadzone


def _axis_requirement(runs, deadzone: int, min_frames: int, axis: str,
                      label: str) -> dict:
    frames = [frame for _, _, run in runs for frame in run.input_frames]
    if not frames:
        return _result(None, "not measured: no input-frame markers in the evidence")
    channel_frames = [frame for frame in frames if frame.channel == 0]
    if not channel_frames:
        return _result(False, "input frames exist but none on channel 0; the player's pad was not polled")
    longest = None
    for run in _runs_of(channel_frames, deadzone, axis):
        if longest is None or len(run) > len(longest):
            longest = run
    if longest is None or len(longest) < min_frames:
        length = len(longest) if longest is not None else 0
        return _result(False, f"longest channel-0 {label} deflection is {length} frames, "
                              f"below the {min_frames}-frame minimum")
    first, last = longest[0], longest[-1]
    consumed = [record for _, _, run in runs for record in run.input_consumed
                if first.us <= record.us <= last.us]
    if not consumed:
        return _result(False, f"{label} deflected {len(longest)} frames but the guest's own "
                              f"pad record did not change inside the span (not consumed)")
    return _result(True, f"channel-0 {label} deflected {len(longest)} frames "
                         f"(retrace {first.retrace}..{last.retrace}, "
                         f"{last.us - first.us} us), {len(consumed)} consumed record change(s) inside")


def _runs_of(frames, deadzone: int, axis: str) -> list:
    runs = []
    current = []
    for frame in frames:
        if _deflected(frame, deadzone, axis):
            current.append(frame)
        elif current:
            runs.append(current)
            current = []
    if current:
        runs.append(current)
    return runs


def _audio_requirement(runs) -> dict:
    active = [event for _, _, run in runs for event in run.audio_active]
    silent = [event for _, _, run in runs for event in run.audio_silent]
    if not active and not silent:
        return _result(None, "not measured: no audio markers in the evidence")
    if not active:
        return _result(False, "the audio stream was never active")
    last = max([*active, *silent], key=lambda event: event.us)
    if any(last is event for event in silent):
        return _result(False, f"audio became silent at us={last.us} and never returned")
    if silent:
        return _result(True, f"audio active at {active[0].rate} Hz from us={active[0].us}, "
                             f"{len(silent)} silent span(s)")
    return _result(True, f"audio active at {active[0].rate} Hz from us={active[0].us}")


def _transition_requirement(runs) -> dict:
    loads = [load for _, _, run in runs for load in run.scene_loads]
    if not loads:
        return _result(None, "not measured: no scene-load markers in the evidence")
    names = []
    for load in loads:
        if load.name not in names:
            names.append(load.name)
    if len(names) < 2:
        return _result(False, f"only one scene name in the evidence: {names[0]}")
    return _result(True, "scene sequence: " + " -> ".join(names)
                         + f" ({len(loads)} load(s))")


def _save_requirement(runs) -> dict:
    writes = [(index, event) for index, (_, _, run) in enumerate(runs)
              for event in run.save_writes]
    reads = [(index, event) for index, (_, _, run) in enumerate(runs)
             for event in run.save_reads]
    errors = [(index, event) for index, (_, _, run) in enumerate(runs)
              for event in run.save_errors]
    if errors:
        index, error = errors[0]
        return _result(False, f"card I/O error in session {index}: {error.op} failed "
                              f"({error.reason}) at us={error.us}")
    if not writes and not reads:
        return _result(None, "not measured: no save markers in the evidence")
    if writes and not reads:
        write_index, write = writes[0]
        if len(runs) <= write_index + 1:
            return _result(None, f"save written in session {write_index} ({write.bytes} bytes) "
                                 f"but no second session to reload in")
        return _result(False, f"save written in session {write_index} but a later session "
                              f"shows no save read")
    if reads and not writes:
        read_index, read = reads[0]
        return _result(False, f"save read in session {read_index} ({read.bytes} bytes) "
                              f"but no save write in an earlier session")
    write_index, write = writes[0]
    for read_index, read in reads:
        if read_index > write_index:
            return _result(True, f"save written in session {write_index} ({write.bytes} bytes), "
                                 f"read back in session {read_index} ({read.bytes} bytes)")
    return _result(False, "a save was written but no later session read it back")


def _result(observed, detail: str) -> dict:
    return {"observed": observed, "detail": detail}
