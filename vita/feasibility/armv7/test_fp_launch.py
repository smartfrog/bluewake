import os
import json
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))


class LauncherTest(unittest.TestCase):
    def test_cli_stages_fake_runtime_without_execution(self):
        from fp_build import sha256
        from fp_launch import REPORT_DIRECTORY
        with tempfile.TemporaryDirectory(prefix="build-fp-launch-test-", dir=ROOT) as directory:
            root = Path(directory)
            runtime = root / "fake-runtime"
            runtime.mkdir()
            shutil.copyfile("/usr/bin/true", runtime / "FakeElf")
            for name in ("data", "icons", "shaders-builtin", "translations"):
                (runtime / name).mkdir()
                (runtime / name / "public-fixture.txt").write_text("fake public asset")
            (root / "vita-fp").mkdir()
            vpk = root / "vita-fp/ww_fp.vpk"
            with zipfile.ZipFile(vpk, "w") as archive:
                archive.writestr("eboot.bin", b"fake diagnostic")
                archive.writestr("sce_sys/param.sfo", b"BWWF00003\0")
            evidence = root / "build.json"
            evidence.write_text(json.dumps({"source": "a" * 40, "artifacts": {
                str(vpk): {"sha256": sha256(vpk)}}}))
            result = subprocess.run([sys.executable, ROOT / "scripts/vita/armv7/fp_launch.py",
                                     "--runtime", runtime / "FakeElf", "--runtime-sha256",
                                     sha256(runtime / "FakeElf"), "--runtime-source-revision", "fake",
                                     "--libraries", runtime, "--build-evidence", evidence, "--out", root],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("STAGED ONLY", result.stdout)
            run, = root.glob("vita3k-run-*")
            self.assertFalse((run / "evidence/process.log").exists())
            self.assertTrue((run / REPORT_DIRECTORY / "fp.request").exists())
            plan = json.loads((run / "evidence/plan.json").read_text())
            self.assertFalse(plan["real_execution"])
            self.assertEqual(plan["vpk_sha256"], sha256(vpk))

    def test_fake_report_failure_is_not_lost_when_emulator_exits_zero(self):
        from fp_launch import REPORT_DIRECTORY, collect_result, create_run, sandbox_command, run_owned
        from fp_vectors import cases
        parent = ROOT / "build-vita-ww-03/launcher-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            root = Path(directory) / "run"
            create_run(root)
            report = root / REPORT_DIRECTORY
            report.mkdir(parents=True)
            lines = ["RUN fake source package"]
            for i, case in enumerate(cases()):
                actual = "0000000000000000" if i == 0 else case["expected"]
                verdict = "FAIL" if i == 0 else "PASS"
                lines.append(f"CASE {case['id']} mode={case['mode']} expected={case['expected']} "
                             f"actual={actual} path=1 {verdict}")
            lines.append(f"END FAIL cases={len(cases())} failures=1")
            text = "\n".join(lines) + "\n"
            script = root / "runtime/Fake"
            script.write_text("#!/usr/bin/python3\nfrom pathlib import Path\n"
                              f"Path('/run/ww03/{REPORT_DIRECTORY}/fp.report').write_text({text!r})\n")
            script.chmod(0o700)
            status = run_owned(sandbox_command(root, "Fake", []), root, 5)
            self.assertEqual(status, 0)
            self.assertEqual(collect_result(root, "fake", "source", "package", status), 1)
            self.assertEqual(json.loads((root / "evidence/result.json").read_text())["semantic"], "FAIL")

    def test_missing_report_is_failure(self):
        from fp_launch import collect_result, create_run
        parent = ROOT / "build-vita-ww-03/launcher-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            root = Path(directory) / "run"
            create_run(root)
            self.assertEqual(collect_result(root, "fake", "source", "package", 0), 2)

    def test_timeout_cleans_detached_owned_child_only(self):
        from fp_launch import create_run, sandbox_command, run_owned
        parent = ROOT / "build-vita-ww-03/launcher-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            root = Path(directory) / "run"
            create_run(root)
            script = root / "runtime/Fake"
            script.write_text("#!/usr/bin/python3\nimport os,time,signal\n"
                              "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
                              "if os.fork() == 0:\n os.setsid()\n"
                              " while True:\n"
                              "  with open('/run/ww03/data/heartbeat','w') as f: f.write(str(time.time_ns()))\n"
                              "  time.sleep(.01)\n"
                              "else:\n time.sleep(60)\n")
            script.chmod(0o700)
            with subprocess.Popen(["/usr/bin/sleep", "30"], start_new_session=True) as witness:
                try:
                    self.assertEqual(run_owned(sandbox_command(root, "Fake", []), root, .3), 124)
                    heartbeat = (root / "data/heartbeat").read_text()
                    time.sleep(.1)
                    self.assertEqual((root / "data/heartbeat").read_text(), heartbeat)
                    self.assertIsNone(witness.poll())
                finally:
                    witness.terminate()
                    witness.wait()

    def test_symlink_ancestor_is_rejected(self):
        from fp_launch import create_run
        parent = ROOT / "build-vita-ww-03/launcher-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            alias = Path(directory) / "alias"
            alias.symlink_to(directory)
            with self.assertRaisesRegex(ValueError, "symlink"):
                create_run(alias / "run")

    def test_fake_failure_exit_is_preserved(self):
        from fp_launch import create_run, sandbox_command, run_owned
        parent = ROOT / "build-vita-ww-03/launcher-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            root = Path(directory) / "run"
            create_run(root)
            script = root / "runtime/Fake"
            script.write_text("#!/usr/bin/python3\nraise SystemExit(7)\n")
            script.chmod(0o700)
            self.assertEqual(run_owned(sandbox_command(root, "Fake", []), root, 5), 7)

    def test_existing_run_root_is_rejected(self):
        from fp_launch import create_run
        parent = ROOT / "build-vita-ww-03/launcher-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            with self.assertRaises(FileExistsError):
                create_run(Path(directory))

    def test_fake_cannot_write_outside_private_root(self):
        from fp_launch import create_run, sandbox_command, run_owned
        parent = ROOT / "build-vita-ww-03/launcher-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            root = Path(directory) / "run"
            create_run(root)
            script = root / "runtime/Fake"
            script.write_text("#!/usr/bin/python3\nfrom pathlib import Path\nimport os\n"
                              "assert Path.cwd() == Path('/stack/vita3k')\n"
                              "assert os.environ['HOME'] == '/run/ww03/home'\n"
                              "assert 'APPIMAGE' not in os.environ and 'APPDIR' not in os.environ\n"
                              f"assert os.readlink('/proc/self/ns/net') != {os.readlink('/proc/self/ns/net')!r}\n"
                              "try:\n Path('/usr/ww03-forbidden').write_text('bad')\n"
                              "except OSError: pass\nelse: raise AssertionError('writable host')\n"
                              "try:\n Path('/stack/vita3k/forbidden').write_text('bad')\n"
                              "except OSError: pass\nelse: raise AssertionError('writable runtime')\n"
                              "try:\n Path('/run/ww03/runtime/forbidden').write_text('bad')\n"
                              "except OSError: pass\nelse: raise AssertionError('writable runtime alias')\n"
                              "Path(os.environ['XDG_DATA_HOME']+'/proof').write_text('private')\n")
            script.chmod(0o700)
            status = run_owned(sandbox_command(root, "Fake", []), root, 5)
            self.assertEqual(status, 0, (root / "evidence/process.log").read_text())
            self.assertEqual((root / "data/proof").read_text(), "private")


if __name__ == "__main__":
    unittest.main()
