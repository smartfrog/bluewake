import json
import os
from pathlib import Path
import struct
import sys
import unittest
from unittest import mock
import zipfile

import test_fp_campaign as fp_tests
from test_mem_report import target_report
from fp_profile import CONFIG_PATH

launch = fp_tests.launch


def sfo(title=b"BWWM00004\0"):
    return (struct.pack("<5I", 0x46535000, 0x101, 36, 48, 1) +
            struct.pack("<HHIII", 0, 0x204, len(title), 16, 0) +
            b"TITLE_ID\0\0\0\0" + title.ljust(16, b"\0"))


class MemoryLauncherTest(unittest.TestCase):
    def setUp(self):
        fp_tests.CampaignTest.setUp(self)
        self.vpk = self.output / "vita-mem/ww_mem.vpk"
        self.vpk.parent.mkdir()
        with zipfile.ZipFile(self.vpk, "w") as archive:
            archive.writestr("eboot.bin", b"SCE\0synthetic test SELF")
            archive.writestr("sce_sys/param.sfo", sfo())
        (self.output / "build.json").write_text(json.dumps({"source": "a" * 40,
            "method": "bounded-v1", "title": "BWWM00004", "artifacts": {
                str(self.vpk): {"sha256": launch.sha256(self.vpk)}}}))

    def stage(self):
        with mock.patch.object(sys, "argv", self.argv), \
                mock.patch.object(launch, "build_directory", return_value=self.output):
            self.assertEqual(launch.main(memory=True), 0)
        root, = self.output.glob("vita3k-run-*")
        return root, json.loads((root / "evidence/plan.json").read_text())

    def test_stage_only_protects_both_plans_without_request_or_process(self):
        with mock.patch.object(launch.subprocess, "Popen", side_effect=AssertionError("unexpected real process")):
            root, plan = self.stage()
        self.assertEqual(plan["process_budget_used"], 0)
        self.assertEqual(plan["phases"]["install"]["command"][-5:],
                         ["/stack/vita3k/Vita3K", "--console", "--backend-renderer", "OpenGL", "mem.vpk"])
        self.assertEqual(plan["phases"]["guest"]["command"][-5:],
                         ["/stack/vita3k/Vita3K", "--backend-renderer", "OpenGL", "--installed-path", "BWWM00004"])
        self.assertFalse((root / launch.REPORT_DIRECTORY / "mem.request").exists())
        self.assertFalse(list(root.rglob("process.log")))

    def execute_fake(self, installer="sys.exit(0)", guest="", stale=None):
        body = target_report().split("\n", 1)[1]
        script = ("#!/usr/bin/python3\nimport sys,time,zipfile\nfrom pathlib import Path\n"
                  "root=Path('/run/ww03')\n"
                  f"directory=root/{str(launch.REPORT_DIRECTORY)!r}\n"
                  f"config=root/{str(CONFIG_PATH)!r}\n"
                  "if '--console' in sys.argv:\n"
                  " assert sys.argv[1:]==['--console','--backend-renderer','OpenGL','mem.vpk']\n"
                  " assert not (directory/'mem.request').exists()\n"
                  " app=root/'data/Vita3K/Vita3K/ux0/app/BWWM00004'; app.mkdir(parents=True)\n"
                  " with zipfile.ZipFile('mem.vpk') as archive: archive.extractall(app)\n"
                  " config.write_text(config.read_text().replace('user-id: \"\"','user-id: 00'))\n"
                  " (root/'data/retained-user').write_text('private')\n" +
                  (f" directory.mkdir(parents=True); (directory/{stale!r}).write_text('stale')\n" if stale else "") +
                  " " + installer + "\n"
                  "else:\n"
                  " assert sys.argv[1:]==['--backend-renderer','OpenGL','--installed-path','BWWM00004']\n"
                  " assert 'user-id: \"\"' in config.read_text()\n"
                  " assert (root/'data/retained-user').read_text()=='private'\n"
                  " try: (root/'phases/install/evidence/plan.json').write_text('bad')\n"
                  " except OSError: pass\n"
                  " else: raise AssertionError('phase plan writable')\n"
                  " token,source,package=(directory/'mem.request').read_text().split()\n"
                  f" text='MEM '+token+' '+source+' '+package+' method=bounded-v1 phase=guest platform=VITA\\n'+{body!r}\n"
                  " (directory/'mem.partial').write_text(text); (directory/'mem.partial').rename(directory/'mem.report')\n" +
                  " " + (guest or "sys.exit(0)") + "\n")
        original_stage, sandbox, owned = launch.stage_runtime, launch.sandbox_command, launch.run_owned
        calls = []

        def stage_runtime(*args):
            result = original_stage(*args)
            path = args[0] / "runtime/Fake"
            path.write_text(script)
            path.chmod(0o700)
            return result

        def run_owned(*args, **kwargs):
            if calls:
                self.assertFalse(Path("/proc", str(calls[-1]["pid"])).exists())
            status = owned(*args, **kwargs)
            calls.append(json.loads((kwargs["evidence_root"] / "evidence/process.json").read_text()))
            return status

        with mock.patch.object(sys, "argv", [*self.argv, "--execute-reviewed"]), \
                mock.patch.object(launch, "build_directory", return_value=self.output), \
                mock.patch.object(launch, "stage_runtime", side_effect=stage_runtime), \
                mock.patch.object(launch, "sandbox_command", side_effect=lambda root, exe, argv, libs, **kw:
                                  sandbox(root, "Fake", argv, libs, **kw)), \
                mock.patch.object(launch, "run_owned", side_effect=run_owned):
            status = launch.main(memory=True)
        root, = self.output.glob("vita3k-run-*")
        return status, root, calls

    def test_fake_installer_timeout_preserves_124_preparation_then_runs_fresh_guest(self):
        status, root, calls = self.execute_fake(installer="time.sleep(60)")
        self.assertEqual(status, 0)
        self.assertEqual(len(calls), 2)
        prepared = json.loads((root / "phases/install/evidence/preparation.json").read_text())
        self.assertEqual((prepared["readiness"], prepared["process_status"]), ("VERIFIED", 124))
        self.assertNotEqual(prepared["guest_token"], root.name.removeprefix("vita3k-run-"))

    def test_failed_installer_stops_at_one_owned_process(self):
        status, root, calls = self.execute_fake(installer="sys.exit(7)")
        self.assertEqual(len(calls), 1)
        self.assertNotEqual(status, 0)
        self.assertFalse((root / launch.REPORT_DIRECTORY / "mem.request").exists())

    def test_stale_mem_outputs_are_preserved_and_block_guest(self):
        for name in ("mem.request", "mem.partial", "mem.report"):
            with self.subTest(name=name):
                status, root, calls = self.execute_fake(stale=name)
                self.assertNotEqual(status, 0)
                self.assertEqual(len(calls), 1)
                self.assertEqual((root / launch.REPORT_DIRECTORY / name).read_text(), "stale")
                root.rename(self.output / name)

    def test_forced_stop_after_valid_report_remains_125_incomplete(self):
        status, root, calls = self.execute_fake(guest="time.sleep(60)")
        self.assertEqual(status, 125)
        self.assertEqual(len(calls), 2)
        result = json.loads((root / "phases/guest/evidence/result.json").read_text())
        self.assertEqual((result["completion"], result["reported_semantic"]), ("INCOMPLETE", "PASS"))

    def test_exact_sfo_title_rejects_substring_identity(self):
        with zipfile.ZipFile(self.vpk, "w") as archive:
            archive.writestr("eboot.bin", b"SCE\0synthetic")
            archive.writestr("sce_sys/param.sfo", sfo(b"XBWWM00004\0"))
        with self.assertRaisesRegex(ValueError, "exact memory"):
            launch.verify_package(self.vpk, launch.sha256(self.vpk), memory=True)

    def test_memory_report_hardlink_is_rejected(self):
        root, _ = self.stage()
        directory = root / launch.REPORT_DIRECTORY
        directory.mkdir(parents=True)
        report = directory / "mem.report"
        report.write_text(target_report())
        os.link(report, self.output / "linked-report")
        with self.assertRaisesRegex(ValueError, "owner/link"):
            launch.read_report(root, memory=True)


if __name__ == "__main__":
    unittest.main()
