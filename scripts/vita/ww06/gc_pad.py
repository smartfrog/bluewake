#!/usr/bin/env python3
"""The standard GameCube pad's SI transfer encoding, as a checked reference.

This is the byte-level spec for the pad device model that plugs into the SI
seam (see docs/vita/VITA-WW-06.md). It mirrors what the desktop host already
serves this game - runtime/host/src/pad_wire.c's encoding and main.c's
host_si_complete_pad_transfer dispatch - and the pinned Dolphin reference
(HW/SI/SI_DeviceGCController.cpp, mode 0) documents the same data bytes:

    status probe (one output byte 0x00, three input bytes):
        09 00 00                    device type 0x09, status 0, rumble 0
    direct read (command 0x40, eight input bytes):
        buttons hi, buttons lo, stick X, stick Y,
        substick X, substick Y, trigger L, trigger R
        (sticks are signed bytes + 128; the button mask is the 16-bit GC mask)
    origin / recalibrate (commands 0x41 / 0x42, eight input bytes):
        09 00 80 80 80 80 00 00     neutral origins

The real game's first SI write in the WW05 evidence is COMCSR 0xC0010301:
TSTART, channel 0, OUTLNGTH 1, INLNGTH 3 - the status probe, whose answer
above is what makes the game see a connected standard pad instead of the
empty bus's COMERR/NOREP.

What this model does not do: it does not invent a device (an absent channel
answers nothing), it does not answer an unknown command, and it does not
apply the host's keyboard-to-pad conveniences (the desktop's digital R click
implies full analog travel before encoding; that mapping belongs to the input
source, not the device). Dolphin's own SOrigin is ten bytes and starts with a
u16 button word; the desktop's game-validated model answers eight bytes as
above, and that is what is pinned here.
"""

from __future__ import annotations

from dataclasses import dataclass

COMMAND_STATUS = 0x00
COMMAND_DIRECT = 0x40
COMMAND_ORIGIN = 0x41
COMMAND_RECALIBRATE = 0x42

PAD_BUTTON_LEFT = 0x0001
PAD_BUTTON_RIGHT = 0x0002
PAD_BUTTON_DOWN = 0x0004
PAD_BUTTON_UP = 0x0008
PAD_BUTTON_Z = 0x0010
PAD_BUTTON_R = 0x0020
PAD_BUTTON_L = 0x0040
PAD_BUTTON_A = 0x0100
PAD_BUTTON_B = 0x0200
PAD_BUTTON_X = 0x0400
PAD_BUTTON_Y = 0x0800
PAD_BUTTON_START = 0x1000

DEVICE_TYPE_STANDARD = 0x09

_ORIGIN_RESPONSE = bytes([DEVICE_TYPE_STANDARD, 0x00, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00])
_STATUS_RESPONSE = bytes([DEVICE_TYPE_STANDARD, 0x00, 0x00])


class PadProtocolError(ValueError):
    """The transfer asks for a command a standard pad does not answer."""


@dataclass(frozen=True)
class PadState:
    buttons: int = 0
    stick_x: int = 0
    stick_y: int = 0
    sub_x: int = 0
    sub_y: int = 0
    trigger_l: int = 0
    trigger_r: int = 0


def _wire_byte(value: int) -> int:
    return (value + 128) & 0xFF


def pad_data_words(pad: PadState) -> tuple:
    """The two big-endian words the SI channel response registers carry."""
    data0 = ((pad.buttons & 0xFFFF) << 16) | (_wire_byte(pad.stick_x) << 8) | _wire_byte(pad.stick_y)
    data1 = ((_wire_byte(pad.sub_x) << 24) | (_wire_byte(pad.sub_y) << 16)
             | ((pad.trigger_l & 0xFF) << 8) | (pad.trigger_r & 0xFF))
    return data0, data1


def direct_response(pad: PadState) -> bytes:
    data0, data1 = pad_data_words(pad)
    return data0.to_bytes(4, "big") + data1.to_bytes(4, "big")


def status_response() -> bytes:
    return _STATUS_RESPONSE


def origin_response() -> bytes:
    return _ORIGIN_RESPONSE


def respond(command: int, in_length: int, pad: PadState | None) -> bytes | None:
    """One SI transfer's response, or None when the channel has no device.

    The dispatch mirrors the desktop host's game-validated model: a read
    shorter than eight bytes is the status probe; origin and recalibrate
    answer neutral origins; a direct read answers the pad data. An unknown
    command is refused, never answered with invented data.
    """
    if pad is None:
        return None
    if in_length < 8:
        return _STATUS_RESPONSE
    if command in (COMMAND_ORIGIN, COMMAND_RECALIBRATE):
        return _ORIGIN_RESPONSE
    if command == COMMAND_DIRECT:
        return direct_response(pad)
    raise PadProtocolError(f"standard pad does not answer command 0x{command:02X}")
