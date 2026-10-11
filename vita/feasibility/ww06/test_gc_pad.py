"""Tests for scripts/vita/ww06/gc_pad.py: the standard-pad SI reference model.

The model pins the transfer encoding a real standard GameCube pad answers with
behind the SI seam, as the desktop host already serves it to this game
(runtime/host/src/pad_wire.c and main.c's host_si_complete_pad_transfer) and
as the pinned Dolphin reference documents it. The tests pin the exact bytes:
the game's own first SI write in the real WW05 evidence (COMCSR C0010301: one
output byte, three input bytes, channel 0) is the status probe whose answer
must be 0x09 0x00 0x00.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/ww06"))

from gc_pad import (  # noqa: E402
    COMMAND_DIRECT,
    COMMAND_ORIGIN,
    COMMAND_RECALIBRATE,
    COMMAND_STATUS,
    PAD_BUTTON_A,
    PAD_BUTTON_R,
    PadProtocolError,
    PadState,
    direct_response,
    origin_response,
    pad_data_words,
    respond,
    status_response,
)


class GcPadTest(unittest.TestCase):
    def test_the_status_probe_answers_the_device_type(self):
        self.assertEqual(status_response(), b"\x09\x00\x00")
        self.assertEqual(respond(COMMAND_STATUS, 3, PadState()),
                         b"\x09\x00\x00")

    def test_a_neutral_pad_encodes_centred_sticks(self):
        data0, data1 = pad_data_words(PadState())
        self.assertEqual(data0, 0x00008080)
        self.assertEqual(data1, 0x80800000)
        self.assertEqual(direct_response(PadState()),
                         b"\x00\x00\x80\x80\x80\x80\x00\x00")

    def test_a_deflected_pad_encodes_buttons_sticks_and_triggers(self):
        pad = PadState(buttons=0x1234, stick_x=-12, stick_y=24, sub_x=5, sub_y=-7,
                       trigger_l=0x30, trigger_r=0x40)
        data0, data1 = pad_data_words(pad)
        self.assertEqual(data0, 0x12347498)
        self.assertEqual(data1, 0x85793040)

    def test_the_extreme_stick_bytes_saturate(self):
        data0, _ = pad_data_words(PadState(stick_x=-128, stick_y=127))
        self.assertEqual(data0, 0x000000FF)

    def test_the_origin_transaction_answers_neutral_origins(self):
        self.assertEqual(origin_response(), b"\x09\x00\x80\x80\x80\x80\x00\x00")
        for command in (COMMAND_ORIGIN, COMMAND_RECALIBRATE):
            self.assertEqual(respond(command, 8, PadState()), origin_response())

    def test_a_direct_read_answers_the_pad_data(self):
        pad = PadState(buttons=PAD_BUTTON_A)
        self.assertEqual(respond(COMMAND_DIRECT, 8, pad), direct_response(pad))

    def test_an_absent_device_has_no_response(self):
        for command in (COMMAND_STATUS, COMMAND_DIRECT, COMMAND_ORIGIN):
            self.assertIsNone(respond(command, 8, None))

    def test_an_unknown_command_is_refused_not_invented(self):
        with self.assertRaises(PadProtocolError):
            respond(0x77, 8, PadState())

    def test_a_short_read_is_the_status_probe_whatever_it_asks(self):
        self.assertEqual(respond(0x40, 3, PadState()), status_response())

    def test_the_digital_r_click_rule_is_the_hosts_not_the_devices(self):
        clicked = PadState(buttons=PAD_BUTTON_R, trigger_r=0x00)
        _, data1 = pad_data_words(clicked)
        self.assertEqual(data1 & 0xFF, 0x00)


if __name__ == "__main__":
    unittest.main()
