"""Tests for scripts/vita/ww06/scenario.py: the five-minute Outset scenario check.

The checker answers one question per acceptance requirement - movement,
camera, audio, transition, save/reload - with observed, failed or not
measured. A family whose markers never appear is not measured; a family that
appears but does not show the required behaviour has failed.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/ww06"))

from acceptance_events import (  # noqa: E402
    AcceptanceEvents,
    AudioActive,
    AudioSilent,
    InputConsumed,
    InputFrame,
    SaveError,
    SaveRead,
    SaveWrite,
    SceneLoad,
)
from scenario import evaluate_scenario  # noqa: E402


def frames(count, *, start_us=1000, stick=(0, 0), sub=(0, 0), start_retrace=100):
    return [InputFrame(retrace=start_retrace + i, channel=0, buttons=0,
                       stick_x=stick[0], stick_y=stick[1],
                       sub_x=sub[0], sub_y=sub[1], us=start_us + i * 16000)
            for i in range(count)]


def complete_run():
    return AcceptanceEvents(
        input_frames=frames(60, stick=(0, 100)) + frames(60, start_us=2000000, sub=(100, 0)),
        input_consumed=[
            InputConsumed(retrace=130, hold=0x0000, trig=0x0000, axes=0x00648000, us=500000),
            InputConsumed(retrace=190, hold=0x0000, trig=0x0000, axes=0x80006400, us=2000000),
        ],
        audio_active=[AudioActive(rate=48000, us=500)],
        scene_loads=[SceneLoad(name="Outset", us=200),
                     SceneLoad(name="Ojhous", us=4000000)],
        save_writes=[SaveWrite(bytes=8192, us=5000000)],
    )


def reload_run():
    return AcceptanceEvents(
        save_reads=[SaveRead(bytes=8192, us=900)],
        scene_loads=[SceneLoad(name="Outset", us=2000)],
    )


class ScenarioTest(unittest.TestCase):
    def test_a_complete_synthetic_scenario_meets_every_requirement(self):
        result = evaluate_scenario([("save", complete_run()), ("reload", reload_run())])
        self.assertEqual(result["verdict"], "acceptance met")
        for requirement in ("movement", "camera", "audio", "transition", "save_reload"):
            self.assertIs(result["requirements"][requirement]["observed"], True,
                          requirement + ": " + result["requirements"][requirement]["detail"])

    def test_an_empty_stream_is_not_measured(self):
        result = evaluate_scenario([("empty", AcceptanceEvents())])
        self.assertEqual(result["verdict"], "not measured")
        for requirement in ("movement", "camera", "audio", "transition", "save_reload"):
            self.assertIsNone(result["requirements"][requirement]["observed"])
            self.assertIn("not measured", result["requirements"][requirement]["detail"])

    def test_a_short_deflection_fails_only_movement(self):
        events = complete_run()
        events.input_frames = frames(10, stick=(0, 100)) + frames(60, start_us=2000000, sub=(100, 0))
        result = evaluate_scenario([("save", events), ("reload", reload_run())])
        self.assertIs(result["requirements"]["movement"]["observed"], False)
        self.assertIn("10", result["requirements"]["movement"]["detail"])
        self.assertIs(result["requirements"]["camera"]["observed"], True)

    def test_a_deflection_the_guest_never_consumed_fails_movement(self):
        events = complete_run()
        events.input_consumed = []
        result = evaluate_scenario([("save", events), ("reload", reload_run())])
        self.assertIs(result["requirements"]["movement"]["observed"], False)
        self.assertIn("consumed", result["requirements"]["movement"]["detail"])

    def test_a_consumed_record_outside_the_deflection_span_does_not_count(self):
        events = complete_run()
        events.input_consumed = [
            InputConsumed(retrace=50, hold=0, trig=0, axes=0, us=100),
        ]
        result = evaluate_scenario([("save", events), ("reload", reload_run())])
        self.assertIs(result["requirements"]["movement"]["observed"], False)

    def test_audio_that_became_silent_fails(self):
        events = complete_run()
        events.audio_silent = [AudioSilent(us=3000000)]
        result = evaluate_scenario([("save", events), ("reload", reload_run())])
        self.assertIs(result["requirements"]["audio"]["observed"], False)
        self.assertIn("silent", result["requirements"]["audio"]["detail"])

    def test_a_single_scene_name_is_not_a_transition(self):
        events = complete_run()
        events.scene_loads = [SceneLoad(name="Outset", us=200)]
        result = evaluate_scenario([("save", events), ("reload", reload_run())])
        self.assertIs(result["requirements"]["transition"]["observed"], False)
        self.assertIn("Outset", result["requirements"]["transition"]["detail"])

    def test_a_save_without_a_reload_session_is_not_measured(self):
        result = evaluate_scenario([("save", complete_run())])
        self.assertIs(result["requirements"]["save_reload"]["observed"], None)
        self.assertIn("second session", result["requirements"]["save_reload"]["detail"])

    def test_a_reload_session_without_the_read_fails(self):
        result = evaluate_scenario([("save", complete_run()), ("reload", AcceptanceEvents(
            scene_loads=[SceneLoad(name="Outset", us=2000)]))])
        self.assertIs(result["requirements"]["save_reload"]["observed"], False)
        self.assertIn("no save read", result["requirements"]["save_reload"]["detail"])

    def test_a_save_io_error_is_surfaced_and_fails(self):
        events = complete_run()
        events.save_errors = [SaveError(op="write", reason="card_full", us=4000000)]
        result = evaluate_scenario([("save", events), ("reload", reload_run())])
        self.assertIs(result["requirements"]["save_reload"]["observed"], False)
        self.assertIn("card_full", result["requirements"]["save_reload"]["detail"])

    def test_a_read_without_an_earlier_write_fails(self):
        result = evaluate_scenario([("reload", reload_run())])
        self.assertIs(result["requirements"]["save_reload"]["observed"], False)
        self.assertIn("no save write", result["requirements"]["save_reload"]["detail"])

    def test_a_failed_requirement_makes_the_verdict_not_met(self):
        events = complete_run()
        events.scene_loads = [SceneLoad(name="Outset", us=200)] * 1
        result = evaluate_scenario([("save", events), ("reload", reload_run())])
        self.assertEqual(result["verdict"], "acceptance not met")

    def test_other_channels_are_not_the_players_pad(self):
        events = complete_run()
        events.input_frames = [
            InputFrame(retrace=100 + i, channel=1, buttons=0, stick_x=0, stick_y=100,
                       sub_x=0, sub_y=0, us=1000 + i * 16000) for i in range(60)]
        result = evaluate_scenario([("save", events), ("reload", reload_run())])
        self.assertIs(result["requirements"]["movement"]["observed"], False)


if __name__ == "__main__":
    unittest.main()
