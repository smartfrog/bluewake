"""Tests for scripts/vita/ww06/acceptance_events.py: the acceptance stream parser.

The parser reads a session log's lines and keeps only the VITA-WW-06
acceptance markers, searched anywhere in a line so interleaved logger output
cannot hide a stamp (the frame and REL harnesses read their markers the same
way). A malformed marker, a wall time that does not advance, a zero-byte save
or an out-of-range value is an error: a broken stream is not a measurement.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/ww06"))

from acceptance_events import (  # noqa: E402
    AcceptanceEventError,
    AudioActive,
    AudioSilent,
    InputConsumed,
    InputFrame,
    SaveError,
    SaveRead,
    SaveWrite,
    SceneLoad,
    parse_acceptance_events,
)


def log(*lines):
    return "\n".join(lines) + "\n"


class AcceptanceEventTest(unittest.TestCase):
    def test_parses_every_marker_and_ignores_everything_else(self):
        events = parse_acceptance_events(log(
            "[aurora:info] renderer ready",
            "[input-frame] retrace=100 channel=0 buttons=0x0000 stick=0,0 sub=0,0 us=1000",
            "[input-frame] retrace=101 channel=0 buttons=0x0100 stick=-40,96 sub=0,0 us=1016",
            "[input-consumed] retrace=101 hold=0x0100 trig=0x0000 axes=0xD8608000 us=1017",
            "[audio-active] us=2000 rate=48000",
            "[scene-load] name=Outset us=3000",
            "[save-write] bytes=8192 us=4000",
            "[audio-silent] us=5000",
            "[save-read] bytes=8192 us=6000",
            "[save-error] op=write reason=card_full us=7000",
            "[rel] archived resource materialized name=\"d_a_agbsw0\"",
        ))
        self.assertEqual(events.input_frames, [
            InputFrame(retrace=100, channel=0, buttons=0x0000,
                       stick_x=0, stick_y=0, sub_x=0, sub_y=0, us=1000),
            InputFrame(retrace=101, channel=0, buttons=0x0100,
                       stick_x=-40, stick_y=96, sub_x=0, sub_y=0, us=1016),
        ])
        self.assertEqual(events.input_consumed, [
            InputConsumed(retrace=101, hold=0x0100, trig=0x0000,
                          axes=0xD8608000, us=1017),
        ])
        self.assertEqual(events.audio_active, [AudioActive(rate=48000, us=2000)])
        self.assertEqual(events.audio_silent, [AudioSilent(us=5000)])
        self.assertEqual(events.scene_loads, [SceneLoad(name="Outset", us=3000)])
        self.assertEqual(events.save_writes, [SaveWrite(bytes=8192, us=4000)])
        self.assertEqual(events.save_reads, [SaveRead(bytes=8192, us=6000)])
        self.assertEqual(events.save_errors, [SaveError(op="write", reason="card_full", us=7000)])

    def test_recovers_a_marker_interleaved_with_logger_output(self):
        events = parse_acceptance_events(log(
            "[sdl] info: [input-frame] retrace=7 channel=3 buttons=0xFFFF "
            "stick=127,-128 sub=0,0 us=42",
        ))
        self.assertEqual(len(events.input_frames), 1)
        self.assertEqual(events.input_frames[0].channel, 3)
        self.assertEqual(events.input_frames[0].stick_x, 127)
        self.assertEqual(events.input_frames[0].stick_y, -128)

    def test_refuses_a_malformed_marker(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log(
                "[input-frame] retrace=7 channel=0 buttons=0x0000 us=42",
            ))

    def test_refuses_a_wall_time_that_does_not_advance(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log(
                "[input-frame] retrace=7 channel=0 buttons=0x0000 stick=0,0 sub=0,0 us=42",
                "[audio-active] us=42 rate=48000",
            ))

    def test_refuses_a_channel_outside_zero_to_three(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log(
                "[input-frame] retrace=7 channel=4 buttons=0x0000 stick=0,0 sub=0,0 us=42",
            ))

    def test_refuses_a_stick_value_outside_the_signed_byte(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log(
                "[input-frame] retrace=7 channel=0 buttons=0x0000 stick=128,0 sub=0,0 us=42",
            ))

    def test_refuses_a_retrace_that_goes_backwards(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log(
                "[input-frame] retrace=9 channel=0 buttons=0x0000 stick=0,0 sub=0,0 us=42",
                "[input-frame] retrace=8 channel=0 buttons=0x0000 stick=0,0 sub=0,0 us=43",
            ))

    def test_refuses_a_scene_with_an_empty_name(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log("[scene-load] name= us=42"))

    def test_refuses_a_scene_name_with_whitespace(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log("[scene-load] name=Outset Room us=42"))

    def test_refuses_a_nonpositive_audio_rate(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log("[audio-active] us=42 rate=0"))

    def test_refuses_a_zero_byte_save_write(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log("[save-write] bytes=0 us=42"))

    def test_refuses_a_zero_byte_save_read(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log("[save-read] bytes=0 us=42"))

    def test_refuses_an_unknown_save_error_operation(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log("[save-error] op=erase reason=none us=42"))

    def test_refuses_a_save_error_with_an_empty_reason(self):
        with self.assertRaises(AcceptanceEventError):
            parse_acceptance_events(log("[save-error] op=write reason= us=42"))


if __name__ == "__main__":
    unittest.main()
