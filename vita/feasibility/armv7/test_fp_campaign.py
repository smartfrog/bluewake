import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))
import fp_launch as launch
from fp_profile import CONFIG_PATH, CONFIG_TEMPLATE
from fp_vectors import cases


class CampaignTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="build-fp-campaign-", dir=ROOT)
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        self.runtime = self.output / "runtime"
        self.runtime.mkdir()
        shutil.copyfile("/usr/bin/true", self.runtime / "Vita3K")
        for name in ("data", "icons", "shaders-builtin", "translations"):
            (self.runtime / name).mkdir()
        self.libraries = self.output / "libraries"
        self.libraries.mkdir()
        self.vpk = self.output / "vita-fp/ww_fp.vpk"
        self.vpk.parent.mkdir()
        with zipfile.ZipFile(self.vpk, "w") as archive:
            archive.writestr("eboot.bin", b"synthetic SELF")
            archive.writestr("sce_sys/param.sfo", b"BWWF00003\0")
        build = self.output / "build.json"
        build.write_text(json.dumps({"source": "a" * 40, "artifacts": {
            str(self.vpk): {"sha256": launch.sha256(self.vpk)}}}))
        self.firmware = self.output / "firmware"
        for name in ("vs0", "sa0"):
            (self.firmware / name).mkdir(parents=True)
            (self.firmware / name / "system.bin").write_bytes(name.encode())
        self.argv = ["fp_launch", "--two-phase", "--runtime", str(self.runtime / "Vita3K"),
                     "--runtime-sha256", launch.sha256(self.runtime / "Vita3K"),
                     "--runtime-source-revision", "fake", "--libraries", str(self.libraries),
                     "--firmware", str(self.firmware), "--build-evidence", str(build),
                     "--out", str(self.output), "--timeout", "1"]

    def stage(self):
        with mock.patch.object(sys, "argv", self.argv), \
                mock.patch.object(launch, "build_directory", return_value=self.output):
            self.assertEqual(launch.main(), 0)
        root, = self.output.glob("vita3k-run-*")
        return root, json.loads((root / "evidence/plan.json").read_text())

    def test_stage_only_has_two_plans_and_no_guest_request_or_process(self):
        with mock.patch.object(launch.subprocess, "Popen", side_effect=AssertionError("unexpected process")):
            root, plan = self.stage()
        self.assertEqual(plan["process_budget"], 2)
        self.assertEqual(plan["phases"]["install"]["command"][-5:],
                         ["/stack/vita3k/Vita3K", "--console", "--backend-renderer", "OpenGL", "fp.vpk"])
        self.assertEqual(plan["phases"]["guest"]["command"][-5:],
                         ["/stack/vita3k/Vita3K", "--backend-renderer", "OpenGL", "--installed-path", "BWWF00003"])
        self.assertFalse((root / launch.REPORT_DIRECTORY / "fp.request").exists())
        self.assertFalse(list(root.rglob("process.log")))

    def execute_fake(self, installer_extra="", guest_extra="", installer_exit="time.sleep(60)",
                     after_install=None, failure=False):
        lines = []
        for index, case in enumerate(cases()):
            actual = "0000000000000000" if failure and index == 0 else case['expected']
            verdict = "FAIL" if actual != case['expected'] else "PASS"
            lines.append(f"CASE {case['id']} mode={case['mode']} expected={case['expected']} "
                         f"actual={actual} path=1 {verdict}")
        report = "\n".join([*lines, f"END {'FAIL' if failure else 'PASS'} cases=71 failures={int(failure)}"]) + "\n"
        script = ("#!/usr/bin/python3\nimport os,sys,time,zipfile\nfrom pathlib import Path\n"
                  "root=Path('/run/ww03')\n"
                  f"directory=root/{str(launch.REPORT_DIRECTORY)!r}\n"
                  f"config=root/{str(CONFIG_PATH)!r}\n"
                  "app=root/'data/Vita3K/Vita3K/ux0/app/BWWF00003'\n"
                  "if '--console' in sys.argv:\n"
                  " assert sys.argv[1:]==['--console','--backend-renderer','OpenGL','fp.vpk']\n"
                  " assert not (directory/'fp.request').exists()\n"
                  " app.mkdir(parents=True)\n"
                  " with zipfile.ZipFile('fp.vpk') as archive: archive.extractall(app)\n"
                  " config.write_text(config.read_text().replace('user-id: \"\"','user-id: 00'))\n"
                  " (root/'data/fresh-user').write_text('preserve private user')\n"
                  + "\n".join(" " + line for line in installer_extra.splitlines()) + "\n"
                  + " " + installer_exit + "\n"
                  "else:\n"
                  " assert sys.argv[1:]==['--backend-renderer','OpenGL','--installed-path','BWWF00003']\n"
                  " assert 'user-id: \"\"' in config.read_text()\n"
                  " assert (root/'data/fresh-user').read_text()=='preserve private user'\n"
                  " try: (root/'phases/install/evidence/plan.json').write_text('bad')\n"
                  " except OSError: pass\n"
                  " else: raise AssertionError('phase evidence writable')\n"
                  " identity=(directory/'fp.request').read_text().strip()\n"
                  f" (directory/'fp.report').write_text('RUN '+identity+'\\n'+{report!r})\n"
                  + "\n".join(" " + line for line in guest_extra.splitlines()) + "\n")
        stage_runtime, sandbox_command, run_owned = launch.stage_runtime, launch.sandbox_command, launch.run_owned
        popen = launch.subprocess.Popen
        calls = []

        def stage_fake(*args):
            assets = stage_runtime(*args)
            fake = args[0] / "runtime/Fake"
            fake.write_text(script)
            fake.chmod(0o700)
            return assets

        def owned(*args, **kwargs):
            if calls:
                self.assertFalse(Path('/proc', str(calls[-1]["pid"])).exists())
            status = run_owned(*args, **kwargs)
            evidence_root = kwargs.get("evidence_root", args[1])
            calls.append(json.loads((evidence_root / "evidence/process.json").read_text()))
            if len(calls) == 1 and after_install:
                after_install(args[1])
            return status

        with mock.patch.object(sys, "argv", [*self.argv, "--execute-reviewed"]), \
                mock.patch.object(launch, "build_directory", return_value=self.output), \
                mock.patch.object(launch, "stage_runtime", side_effect=stage_fake), \
                mock.patch.object(launch, "sandbox_command", side_effect=lambda root, exe, argv, libs, **kw:
                                  sandbox_command(root, "Fake", argv, libs, **kw)), \
                mock.patch.object(launch, "run_owned", side_effect=owned), \
                mock.patch.object(launch.subprocess, "Popen", wraps=popen) as processes:
            status = launch.main()
        self.assertEqual(processes.call_count, len(calls))
        self.assertLessEqual(processes.call_count, 2)
        root, = self.output.glob("vita3k-run-*")
        return status, root, calls

    def test_installer_timeout_can_prepare_before_a_separate_guest_process(self):
        status, root, calls = self.execute_fake()
        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[0]["stop_reason"], "timeout")
        self.assertTrue(calls[0]["waited"])
        self.assertEqual(status, 0)
        prepared = json.loads((root / "phases/install/evidence/preparation.json").read_text())
        self.assertEqual(prepared["readiness"], "VERIFIED")
        self.assertEqual(prepared["process_status"], 124)
        self.assertNotIn("semantic", prepared)
        self.assertEqual((root / CONFIG_PATH).read_bytes(), CONFIG_TEMPLATE.read_bytes())
        self.assertIn('user-id: 00', (root / "phases/install/evidence/config-emitted.yml").read_text())
        request = (root / launch.REPORT_DIRECTORY / "fp.request").read_text().split()
        self.assertNotEqual(request[0], root.name.removeprefix("vita3k-run-"))
        result = json.loads((root / "phases/guest/evidence/result.json").read_text())
        self.assertEqual(result["completion"], "COMPLETE")

    def test_failed_installer_cannot_start_guest(self):
        status, root, calls = self.execute_fake(installer_exit="sys.exit(7)")
        self.assertNotEqual(status, 0)
        self.assertEqual(len(calls), 1)
        self.assertFalse((root / launch.REPORT_DIRECTORY / "fp.request").exists())

    def assert_rejected_preparation(self, extra, message=None):
        status, root, calls = self.execute_fake(installer_extra=extra, installer_exit="sys.exit(0)")
        self.assertNotEqual(status, 0)
        self.assertEqual(len(calls), 1)
        prepared = json.loads((root / "phases/install/evidence/preparation.json").read_text())
        self.assertEqual(prepared["readiness"], "REJECTED")
        if message:
            self.assertIn(message, prepared["error"])
        self.assertFalse((root / "phases/guest/evidence/process.log").exists())
        return root

    def test_invalid_installed_payload_blocks_guest(self):
        changes = {
            "self-bits": "(app/'eboot.bin').write_bytes(b'wrong SELF')",
            "sfo-bits": "(app/'sce_sys/param.sfo').write_bytes(b'WRONG0001\\0')",
            "missing-sfo": "(app/'sce_sys/param.sfo').unlink()",
            "extra-file": "(app/'extra').write_text('unexpected')",
            "extra-title": "(app.parent/'OTHER0001').mkdir()",
            "self-symlink": "(app/'eboot.bin').rename(root/'data/payload'); (app/'eboot.bin').symlink_to(root/'data/payload')",
            "self-hardlink": "os.link(app/'eboot.bin', root/'data/linked-self')",
            "self-fifo": "(app/'eboot.bin').unlink(); os.mkfifo(app/'eboot.bin')",
            "self-directory": "(app/'eboot.bin').unlink(); (app/'eboot.bin').mkdir()",
            "parent-symlink": "(app/'sce_sys').rename(root/'data/redirect'); (app/'sce_sys').symlink_to(root/'data/redirect')",
        }
        for name, extra in changes.items():
            with self.subTest(name=name):
                root = self.assert_rejected_preparation(extra)
                self.assertFalse((root / launch.REPORT_DIRECTORY / "fp.request").exists())
                root.rename(self.output / name)

    def test_arbitrary_config_mutation_is_preserved_but_not_restored_or_run(self):
        root = self.assert_rejected_preparation(
            "config.write_text(config.read_text().replace('show-welcome: false','show-welcome: true'))",
            "configuration mutation")
        self.assertIn('show-welcome: true', (root / "phases/install/evidence/config-emitted.yml").read_text())
        self.assertIn('show-welcome: true', (root / CONFIG_PATH).read_text())

    def test_config_link_cannot_redirect_restoration(self):
        root = self.assert_rejected_preparation(
            "target=root/'data/config-sentinel'; target.write_bytes(config.read_bytes()); config.unlink(); config.symlink_to(target)")
        self.assertTrue((root / CONFIG_PATH).is_symlink())
        self.assertIn('user-id: 00', (root / "data/config-sentinel").read_text())

    def test_preexisting_restore_file_is_not_overwritten(self):
        root = self.assert_rejected_preparation("(config.parent/'config.restore.yml').write_text('untouched')")
        self.assertEqual((root / CONFIG_PATH.parent / "config.restore.yml").read_text(), "untouched")

    def test_stale_diagnostic_files_are_rejected_without_deletion(self):
        for name in ("fp.report", "fp.partial", "fp.request"):
            with self.subTest(name=name):
                root = self.assert_rejected_preparation(
                    f"directory.mkdir(parents=True); (directory/{name!r}).write_text('stale')", "preexisting")
                self.assertEqual((root / launch.REPORT_DIRECTORY / name).read_text(), "stale")
                root.rename(self.output / name)

    def test_config_error_blocks_guest_even_after_correct_installation(self):
        self.assert_rejected_preparation("print(\"Config file can't be loaded: Error: invalid node\")", "configuration")

    def test_cache_only_config_error_blocks_guest(self):
        self.assert_rejected_preparation(
            "log=root/'cache/Vita3K/vita3k.log'; log.parent.mkdir(); "
            "log.write_text(\"Config file can't be loaded: Error: invalid node\\n\")", "configuration")

    def test_mutated_inputs_block_guest(self):
        targets = {
            "library": lambda root: self.libraries / "new-library",
            "source-firmware": lambda root: self.firmware / "vs0/system.bin",
            "staged-firmware": lambda root: root / "data/Vita3K/Vita3K/sa0/system.bin",
            "staged-runtime": lambda root: root / "runtime/translations/changed",
            "source-assets": lambda root: self.runtime / "translations/changed",
        }
        for name, target in targets.items():
            with self.subTest(name=name):
                original = None

                def mutate(root):
                    nonlocal original
                    path = target(root)
                    original = path.read_bytes() if path.exists() else None
                    path.write_bytes(b'changed')

                status, root, calls = self.execute_fake(installer_exit="sys.exit(0)", after_install=mutate)
                self.assertNotEqual(status, 0)
                self.assertEqual(len(calls), 1)
                path = target(root)
                if original is None:
                    path.unlink()
                else:
                    path.write_bytes(original)
                root.rename(self.output / name)

    def test_guest_report_followed_by_forced_stop_remains_125_incomplete(self):
        status, root, calls = self.execute_fake(installer_exit="sys.exit(0)", guest_extra="time.sleep(60)")
        self.assertEqual(len(calls), 2)
        self.assertEqual(status, 125)
        result = json.loads((root / "phases/guest/evidence/result.json").read_text())
        self.assertEqual(result["reported_semantic"], "PASS")
        self.assertEqual(result["semantic"], "UNAVAILABLE")
        self.assertEqual(result["completion"], "INCOMPLETE")

    def test_guest_semantic_failure_is_nonzero(self):
        status, root, _ = self.execute_fake(installer_exit="sys.exit(0)", failure=True)
        self.assertEqual(status, 1)
        result = json.loads((root / "phases/guest/evidence/result.json").read_text())
        self.assertEqual(result["semantic"], "FAIL")

    def test_detached_installer_descendant_is_gone_before_guest(self):
        installer = ("heartbeat=root/'data/heartbeat'\n"
                     "if os.fork()==0:\n os.setsid()\n while True:\n"
                     "  heartbeat.write_text(str(time.time_ns())); time.sleep(.01)\n"
                     "while not heartbeat.exists(): time.sleep(.01)")
        guest = ("heartbeat=root/'data/heartbeat'\n"
                 "before=heartbeat.read_text(); time.sleep(.1)\n"
                 "assert heartbeat.read_text()==before")
        status, _, calls = self.execute_fake(installer_extra=installer, guest_extra=guest)
        self.assertEqual(status, 0)
        self.assertEqual(len(calls), 2)

    def test_unreaped_or_crashed_installer_record_blocks_guest(self):
        for change in ({"waited": False}, {"pid": os.getpid()}, {"exit": -11}):
            with self.subTest(change=change):
                def alter_record(root):
                    path = root / "phases/install/evidence/process.json"
                    record = json.loads(path.read_text())
                    record.update(change)
                    path.write_text(json.dumps(record))

                status, root, calls = self.execute_fake(installer_exit="sys.exit(0)", after_install=alter_record)
                self.assertNotEqual(status, 0)
                self.assertEqual(len(calls), 1)
                root.rename(self.output / ("rejected-" + next(iter(change))))

    def test_installer_crash_cannot_become_verified_preparation(self):
        status, root, calls = self.execute_fake(installer_exit="os.kill(os.getpid(), 9)")
        self.assertEqual(len(calls), 1)
        self.assertNotEqual(status, 0)
        record = json.loads((root / "phases/install/evidence/preparation.json").read_text())
        self.assertEqual(record["process"]["stop_reason"], "exit")
        self.assertEqual(record["readiness"], "REJECTED")

    def test_guest_configuration_error_cannot_accept_valid_bits(self):
        status, root, _ = self.execute_fake(installer_exit="sys.exit(0)",
            guest_extra="print(\"Config file can't be loaded: Error: invalid node\")")
        self.assertEqual(status, 2)
        record = json.loads((root / "phases/guest/evidence/result.json").read_text())
        self.assertEqual(record["reported_semantic"], "PASS")
        self.assertEqual(record["completion"], "INCOMPLETE")


if __name__ == "__main__":
    unittest.main()
