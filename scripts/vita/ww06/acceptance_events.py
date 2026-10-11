#!/usr/bin/env python3
"""Acceptance-event streams for the VITA-WW-06 Outset scenario harness.

The harness reads what the host reports, never the emulator's own state. A
stream is a session log's lines; these markers matter, searched anywhere in a
line so that interleaved logger output cannot hide a stamp (the frame and REL
harnesses read their markers the same way):

    [input-frame] retrace=N channel=C buttons=0xHHHH stick=X,Y sub=X,Y us=T
        one pad-data transfer the SI seam served on channel C, decoded: the
        button mask and the signed stick/substick bytes the game was given.
    [input-consumed] retrace=N hold=0xHHHH trig=0xHHHH axes=0xHHHHHHHH us=T
        the game's own pad record (GZLE01 0x803A4E20..0x803A4E27) changed.
        hold and trig are the two 16-bit words the desktop input-chain probe
        reads there; axes is the raw 32-bit word at 0x803A4E24, where the
        stick bytes are read from in the game's record (printed raw on
        purpose until the record's inner layout is pinned from the decomp).
        Emitted on change, so its presence after a deflection is guest-side
        evidence that the game's controller code read the state.
    [audio-active] us=T rate=HZ   the AI stream is running and producing
                                  output at a nonzero sample rate
    [audio-silent] us=T           the stream stopped or its output went silent
    [scene-load] name=NAME us=T   a scene transition completed; NAME is the
                                  destination scene, without spaces
    [save-write] bytes=B us=T     the guest's card write completed, B > 0
    [save-read] bytes=B us=T      the guest read the saved file, B > 0
    [save-error] op=read|write reason=REASON us=T   a card I/O failure

Everything else in the log is ignored. A malformed marker, a wall time that
does not advance across timed events, a zero-byte save (a zero-byte write is
not a save), an out-of-range value, an empty scene name or an unknown
save-error operation is an error: a broken stream is not a measurement.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import re
from typing import Iterable

INPUT_FRAME_MARKER = "[input-frame]"
INPUT_CONSUMED_MARKER = "[input-consumed]"
AUDIO_ACTIVE_MARKER = "[audio-active]"
AUDIO_SILENT_MARKER = "[audio-silent]"
SCENE_LOAD_MARKER = "[scene-load]"
SAVE_WRITE_MARKER = "[save-write]"
SAVE_READ_MARKER = "[save-read]"
SAVE_ERROR_MARKER = "[save-error]"

_INPUT_FRAME = re.compile(
    r"\[input-frame\]\s+retrace=(\d+)\s+channel=(\d)\s+buttons=0x([0-9A-Fa-f]{1,4})"
    r"\s+stick=(-?\d+),(-?\d+)\s+sub=(-?\d+),(-?\d+)\s+us=(\d+)")
_INPUT_CONSUMED = re.compile(
    r"\[input-consumed\]\s+retrace=(\d+)\s+hold=0x([0-9A-Fa-f]{1,4})"
    r"\s+trig=0x([0-9A-Fa-f]{1,4})\s+axes=0x([0-9A-Fa-f]{1,8})\s+us=(\d+)")
_AUDIO_ACTIVE = re.compile(r"\[audio-active\]\s+us=(\d+)\s+rate=(\d+)")
_AUDIO_SILENT = re.compile(r"\[audio-silent\]\s+us=(\d+)")
_SCENE_LOAD = re.compile(r"\[scene-load\]\s+name=(\S+)\s+us=(\d+)")
_SAVE_WRITE = re.compile(r"\[save-write\]\s+bytes=(\d+)\s+us=(\d+)")
_SAVE_READ = re.compile(r"\[save-read\]\s+bytes=(\d+)\s+us=(\d+)")
_SAVE_ERROR = re.compile(r"\[save-error\]\s+op=(\S+)\s+reason=(\S+)\s+us=(\d+)")


class AcceptanceEventError(ValueError):
    """The stream contains a marker that is not a usable measurement."""


@dataclass(frozen=True)
class InputFrame:
    retrace: int
    channel: int
    buttons: int
    stick_x: int
    stick_y: int
    sub_x: int
    sub_y: int
    us: int


@dataclass(frozen=True)
class InputConsumed:
    retrace: int
    hold: int
    trig: int
    axes: int
    us: int


@dataclass(frozen=True)
class AudioActive:
    rate: int
    us: int


@dataclass(frozen=True)
class AudioSilent:
    us: int


@dataclass(frozen=True)
class SceneLoad:
    name: str
    us: int


@dataclass(frozen=True)
class SaveWrite:
    bytes: int
    us: int


@dataclass(frozen=True)
class SaveRead:
    bytes: int
    us: int


@dataclass(frozen=True)
class SaveError:
    op: str
    reason: str
    us: int


@dataclass
class AcceptanceEvents:
    input_frames: list = field(default_factory=list)
    input_consumed: list = field(default_factory=list)
    audio_active: list = field(default_factory=list)
    audio_silent: list = field(default_factory=list)
    scene_loads: list = field(default_factory=list)
    save_writes: list = field(default_factory=list)
    save_reads: list = field(default_factory=list)
    save_errors: list = field(default_factory=list)

    def is_empty(self) -> bool:
        return not any((self.input_frames, self.input_consumed, self.audio_active,
                        self.audio_silent, self.scene_loads, self.save_writes,
                        self.save_reads, self.save_errors))


def parse_acceptance_events(lines: Iterable[str]) -> AcceptanceEvents:
    if isinstance(lines, str):
        lines = lines.splitlines()
    events = AcceptanceEvents()
    previous_us = None
    previous_retrace = None
    for line in lines:
        if INPUT_FRAME_MARKER in line:
            match = _INPUT_FRAME.search(line)
            if match is None:
                raise AcceptanceEventError("malformed " + INPUT_FRAME_MARKER + " stamp: " + line)
            channel = int(match.group(2))
            if channel > 3:
                raise AcceptanceEventError("input frame channel outside 0..3: " + line)
            stick = (_signed_byte(match.group(4), line), _signed_byte(match.group(5), line))
            sub = (_signed_byte(match.group(6), line), _signed_byte(match.group(7), line))
            retrace = int(match.group(1))
            us = int(match.group(8))
            previous_us = _check_us(us, previous_us, line)
            previous_retrace = _check_retrace(retrace, previous_retrace, line)
            events.input_frames.append(InputFrame(
                retrace=retrace, channel=channel, buttons=int(match.group(3), 16),
                stick_x=stick[0], stick_y=stick[1], sub_x=sub[0], sub_y=sub[1], us=us))
            continue
        if INPUT_CONSUMED_MARKER in line:
            match = _INPUT_CONSUMED.search(line)
            if match is None:
                raise AcceptanceEventError("malformed " + INPUT_CONSUMED_MARKER + " stamp: " + line)
            retrace = int(match.group(1))
            us = int(match.group(5))
            previous_us = _check_us(us, previous_us, line)
            previous_retrace = _check_retrace(retrace, previous_retrace, line)
            events.input_consumed.append(InputConsumed(
                retrace=retrace, hold=int(match.group(2), 16), trig=int(match.group(3), 16),
                axes=int(match.group(4), 16), us=us))
            continue
        if AUDIO_ACTIVE_MARKER in line:
            match = _AUDIO_ACTIVE.search(line)
            if match is None:
                raise AcceptanceEventError("malformed " + AUDIO_ACTIVE_MARKER + " stamp: " + line)
            us = int(match.group(1))
            rate = int(match.group(2))
            if rate <= 0:
                raise AcceptanceEventError("audio active with a zero rate: " + line)
            previous_us = _check_us(us, previous_us, line)
            events.audio_active.append(AudioActive(rate=rate, us=us))
            continue
        if AUDIO_SILENT_MARKER in line:
            match = _AUDIO_SILENT.search(line)
            if match is None:
                raise AcceptanceEventError("malformed " + AUDIO_SILENT_MARKER + " stamp: " + line)
            us = int(match.group(1))
            previous_us = _check_us(us, previous_us, line)
            events.audio_silent.append(AudioSilent(us=us))
            continue
        if SCENE_LOAD_MARKER in line:
            match = _SCENE_LOAD.search(line)
            if match is None:
                raise AcceptanceEventError("malformed " + SCENE_LOAD_MARKER + " stamp: " + line)
            name = match.group(1)
            if "=" in name:
                raise AcceptanceEventError("scene name is malformed: " + line)
            us = int(match.group(2))
            previous_us = _check_us(us, previous_us, line)
            events.scene_loads.append(SceneLoad(name=name, us=us))
            continue
        if SAVE_WRITE_MARKER in line:
            match = _SAVE_WRITE.search(line)
            if match is None:
                raise AcceptanceEventError("malformed " + SAVE_WRITE_MARKER + " stamp: " + line)
            size = int(match.group(1))
            if size <= 0:
                raise AcceptanceEventError("save write of zero bytes is not a save: " + line)
            us = int(match.group(2))
            previous_us = _check_us(us, previous_us, line)
            events.save_writes.append(SaveWrite(bytes=size, us=us))
            continue
        if SAVE_READ_MARKER in line:
            match = _SAVE_READ.search(line)
            if match is None:
                raise AcceptanceEventError("malformed " + SAVE_READ_MARKER + " stamp: " + line)
            size = int(match.group(1))
            if size <= 0:
                raise AcceptanceEventError("save read of zero bytes is not a save: " + line)
            us = int(match.group(2))
            previous_us = _check_us(us, previous_us, line)
            events.save_reads.append(SaveRead(bytes=size, us=us))
            continue
        if SAVE_ERROR_MARKER in line:
            match = _SAVE_ERROR.search(line)
            if match is None:
                raise AcceptanceEventError("malformed " + SAVE_ERROR_MARKER + " stamp: " + line)
            op = match.group(1)
            if op not in ("read", "write"):
                raise AcceptanceEventError("unknown save-error operation: " + line)
            us = int(match.group(3))
            previous_us = _check_us(us, previous_us, line)
            events.save_errors.append(SaveError(op=op, reason=match.group(2), us=us))
    return events


def _signed_byte(text: str, line: str) -> int:
    value = int(text)
    if value < -128 or value > 127:
        raise AcceptanceEventError("stick value outside the signed byte: " + line)
    return value


def _check_us(us: int, previous_us: int | None, line: str) -> int:
    if previous_us is not None and us <= previous_us:
        raise AcceptanceEventError(
            "wall time does not advance: " + str(previous_us) + " then " + str(us))
    return us


def _check_retrace(retrace: int, previous_retrace: int | None, line: str) -> int:
    if previous_retrace is not None and retrace < previous_retrace:
        raise AcceptanceEventError(
            "retrace goes backwards: " + str(previous_retrace) + " then " + str(retrace))
    return retrace
