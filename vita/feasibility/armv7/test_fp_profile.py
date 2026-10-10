import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))

from fp_launch import create_run, run_owned, sandbox_command
import fp_profile as profile

if source := os.environ.get("WW_FP_PROFILE_SOURCE"):
    spec = importlib.util.spec_from_file_location("review_profile", source)
    profile = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(profile)

FIRMWARE_DIRECTORY = profile.FIRMWARE_DIRECTORY
stage_firmware = profile.stage_firmware
verify_firmware = profile.verify_firmware


class ProfileTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="build-fp-profile-", dir=ROOT)
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        self.source = self.output / "firmware"
        for name in ("vs0", "sa0"):
            (self.source / name).mkdir(parents=True)
            (self.source / name / "system.bin").write_bytes(name.encode())
        self.root = self.output / "run"
        create_run(self.root)

    def test_complete_schema_has_all_physical_keyboard_bindings(self):
        stage_firmware(self.root, self.source)
        lines = (self.root / "config/Vita3K/config.yml").read_text().splitlines()
        values = dict(line.split(": ", 1) for line in lines if ": " in line and not line.startswith(" "))
        buttons = "select start up right down left l1 r1 l2 r2 l3 r3 triangle circle cross square psbutton".split()
        keys = ["keyboard-button-" + name for name in buttons]
        keys += [f"keyboard-{stick}stick-{direction}" for stick in ("left", "right")
                 for direction in ("left", "right", "up", "down")]
        keys += ["keyboard-" + name for name in ("gui-fullscreen", "gui-toggle-touch",
                 "toggle-texture-replacement", "take-screenshot", "pinch-modifier",
                 "alternate-pinch-in", "alternate-pinch-out")]
        keys += [key + "-alt" for key in keys]
        self.assertEqual(set(key for key in values if key.startswith("keyboard-")), set(keys))
        for key in keys:
            self.assertRegex(values[key], r"^(ShiftRight|Enter|Arrow(Up|Right|Down|Left)|Key[A-Z]|F11|Unbound)$")
        self.assertEqual(values["keyboard-button-select"], "ShiftRight")
        top_keys = [line.split(":", 1)[0] for line in lines if ":" in line and not line.startswith(" ")]
        self.assertEqual(len(top_keys), 154)
        self.assertEqual(len(top_keys), len(set(top_keys)))

    def test_staged_configuration_is_neutral_and_private(self):
        stage_firmware(self.root, self.source)
        text = (self.root / "config/Vita3K/config.yml").read_text()
        for line in ('user-id: ""', 'user-auto-connect: false', 'backend-renderer: OpenGL',
                     'pref-path: /run/ww03/data/Vita3K/Vita3K', 'show-welcome: false',
                     'warn-missing-firmware: true', 'check-for-updates-mode: 0'):
            self.assertIn(line + "\n", text)
        self.assertFalse((self.root / FIRMWARE_DIRECTORY / "ux0").exists())
        self.assertEqual(profile.config_identity(self.root)["sha256"],
                         "c11fa8feaa4240186f652672643ee1c8d4cbdecbc09b62ac841a936272b67f25")

    def test_polluted_template_is_rejected_before_config_creation(self):
        template = (ROOT / "scripts/vita/armv7/fp_config.yml").read_text()
        changes = {
            "unknown": template + "credential: unexpected\n",
            "duplicate": template + "show-welcome: false\n",
            "alias": template.replace("user-lang: \"\"", "user-lang: *profile"),
            "anchor": template.replace("user-lang: \"\"", "user-lang: &profile unexpected"),
            "tag": template.replace("user-lang: \"\"", "user-lang: !!str unexpected"),
            "legacy-keyboard": template.replace("keyboard-button-select: ShiftRight", "keyboard-button-select: 229"),
            "shared-path": template.replace("/run/ww03/data/Vita3K/Vita3K", "/home/player/shared/profile"),
            "game-path": template.replace('front-camera-image: ""', 'front-camera-image: /games/image.png'),
            "personal-user": template.replace('user-id: ""', 'user-id: player'),
            "autologin": template.replace("user-auto-connect: false", "user-auto-connect: true"),
            "wrong-type": template.replace("warn-missing-firmware: true", 'warn-missing-firmware: "true"'),
        }
        for name, content in changes.items():
            with self.subTest(name=name):
                path = self.output / (name + ".yml")
                path.write_text(content)
                with mock.patch.object(profile, "CONFIG_TEMPLATE", path):
                    with self.assertRaises(ValueError):
                        stage_firmware(self.root, self.source)
                self.assertFalse((self.root / "config/Vita3K").exists())

    def test_template_symlink_or_fifo_is_rejected_without_reading(self):
        for kind in ("symlink", "fifo"):
            with self.subTest(kind=kind):
                path = self.output / kind
                if kind == "symlink":
                    path.symlink_to(ROOT / "scripts/vita/armv7/fp_config.yml")
                else:
                    os.mkfifo(path)
                with mock.patch.object(profile, "CONFIG_TEMPLATE", path):
                    with self.assertRaises((OSError, ValueError)):
                        stage_firmware(self.root, self.source)
                self.assertFalse((self.root / "config/Vita3K").exists())

    def test_config_symlink_ancestor_cannot_redirect_creation(self):
        outside = self.output / "outside-profile"
        outside.mkdir()
        (self.root / "config").rename(self.root / "original-config")
        (self.root / "config").symlink_to(outside)
        with self.assertRaises(OSError):
            stage_firmware(self.root, self.source)
        self.assertFalse((outside / "Vita3K").exists())

    def test_existing_configuration_is_never_overwritten(self):
        config = self.root / "config/Vita3K/config.yml"
        config.parent.mkdir()
        config.write_text("untouched private configuration")
        with self.assertRaises(FileExistsError):
            stage_firmware(self.root, self.source)
        self.assertEqual(config.read_text(), "untouched private configuration")

    def test_sandbox_reads_exact_config_and_firmware_without_shared_profile(self):
        stage_firmware(self.root, self.source)
        shared = self.output / "shared-profile-sentinel"
        shared.write_text("untouched")
        fake = self.root / "runtime/Fake"
        fake.write_text("#!/usr/bin/python3\nimport hashlib,os\nfrom pathlib import Path\n"
                        "config=Path(os.environ['XDG_CONFIG_HOME'])/'Vita3K/config.yml'\n"
                        f"assert hashlib.sha256(config.read_bytes()).hexdigest() == {profile.CONFIG_SHA256!r}\n"
                        "assert 'warn-missing-firmware: true\\n' in config.read_text()\n"
                        f"assert not Path({str(shared)!r}).exists()\n"
                        "for name in ('vs0','sa0'):\n"
                        f" p=Path('/run/ww03/{FIRMWARE_DIRECTORY}')/name/'system.bin'\n"
                        " assert p.read_bytes() == name.encode()\n"
                        " try: p.write_bytes(b'bad')\n"
                        " except OSError: pass\n else: raise AssertionError('writable firmware')\n")
        fake.chmod(0o700)
        self.assertEqual(run_owned(sandbox_command(self.root, "Fake", []), self.root, 5), 0)
        self.assertEqual(shared.read_text(), "untouched")

    def test_config_log_inspection_is_bounded(self):
        log = self.root / "evidence/process.log"
        with log.open("xb") as stream:
            stream.truncate(1024 * 1024 + 1)
        with self.assertRaises(ValueError):
            profile.verify_config_log(self.root)

    def test_invalid_system_tree_does_not_disable_welcome(self):
        for kind in ("empty", "symlink", "fifo"):
            with self.subTest(kind=kind):
                file = self.source / "vs0/system.bin"
                file.unlink()
                if kind == "symlink":
                    file.symlink_to(self.source / "sa0/system.bin")
                elif kind == "fifo":
                    os.mkfifo(file)
                with self.assertRaises(ValueError):
                    stage_firmware(self.root, self.source)
                self.assertFalse((self.root / "config/Vita3K/config.yml").exists())
                if not file.exists() and not file.is_symlink():
                    file.write_bytes(b"reset owned fixture")

    def test_existing_system_file_is_never_overwritten(self):
        directory = self.root / FIRMWARE_DIRECTORY / "vs0"
        directory.mkdir(parents=True)
        sentinel = directory / "system.bin"
        sentinel.write_bytes(b"untouched")
        with self.assertRaises(FileExistsError):
            stage_firmware(self.root, self.source)
        self.assertEqual(sentinel.read_bytes(), b"untouched")

    def test_source_or_staged_mutation_invalidates_firmware_identity(self):
        for location in ("source", "staged"):
            with self.subTest(location=location):
                root = self.output / location
                create_run(root)
                records = stage_firmware(root, self.source)
                file = Path(records["vs0"][location]["root"]) / "system.bin"
                original = file.read_bytes()
                file.write_bytes(b"changed owned firmware fixture")
                with self.assertRaisesRegex(ValueError, f"{location} firmware changed"):
                    verify_firmware(records)
                file.write_bytes(original)

    def test_sandbox_can_read_but_cannot_modify_staged_system_firmware(self):
        records = stage_firmware(self.root, self.source)
        fake = self.root / "runtime/Fake"
        fake.write_text("#!/usr/bin/python3\nfrom pathlib import Path\n"
                        "for name in ('vs0','sa0'):\n"
                        f" p=Path('/run/ww03/{FIRMWARE_DIRECTORY}')/name/'system.bin'\n"
                        " assert p.read_bytes() == name.encode()\n"
                        " try: p.write_bytes(b'tampered')\n"
                        " except OSError: pass\n else: raise AssertionError('writable firmware')\n")
        fake.chmod(0o700)
        self.assertEqual(run_owned(sandbox_command(self.root, "Fake", []), self.root, 5), 0)
        verify_firmware(records)


if __name__ == "__main__":
    unittest.main()
