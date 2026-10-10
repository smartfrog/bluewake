import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))

from fp_launch import create_run, run_owned, sandbox_command
from fp_profile import FIRMWARE_DIRECTORY, stage_firmware, verify_firmware


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
