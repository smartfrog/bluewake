"""Private diagnostic profile setup from installed system firmware only."""

from pathlib import Path
import shutil

from fp_libraries import library_identity

FIRMWARE_DIRECTORY = Path("data/Vita3K/Vita3K")


def stage_firmware(root, source):
    sources = {name: library_identity(source / name, allow_symlinks=False) for name in ("vs0", "sa0")}
    for name, identity in sources.items():
        if not any(entry["type"] == "file" for entry in identity["entries"].values()):
            raise ValueError(f"empty system firmware tree: {name}")
        if (root / FIRMWARE_DIRECTORY / name).exists():
            raise FileExistsError(root / FIRMWARE_DIRECTORY / name)
    records = {}
    for name, identity in sources.items():
        target = root / FIRMWARE_DIRECTORY / name
        shutil.copytree(source / name, target, symlinks=True)
        staged = library_identity(target, allow_symlinks=False)
        if staged["entries"] != identity["entries"]:
            raise ValueError(f"staged firmware mismatch: {name}")
        records[name] = {"source": identity, "staged": staged}
    verify_firmware(records)
    config = root / "config/Vita3K/config.yml"
    config.parent.mkdir()
    with config.open("x") as stream:
        stream.write("pref-path: /run/ww03/data/Vita3K/Vita3K\n"
                     "show-welcome: false\nwarn-missing-firmware: true\ncheck-for-updates-mode: 0\n")
    return records


def verify_firmware(records):
    for name, trees in records.items():
        for location, expected in trees.items():
            actual = library_identity(Path(expected["root"]), allow_symlinks=False)
            if actual != expected:
                raise ValueError(f"{location} firmware changed: {name}")
