import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))
source = os.environ.get("WW_FP_LAUNCH_SOURCE", ROOT / "scripts/vita/armv7/fp_launch.py")
spec = importlib.util.spec_from_file_location("review_launcher", source)
launch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launch)


class LauncherReviewTest(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "build-vita-ww-03/review-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        self.root = self.output / "run"
        launch.create_run(self.root)

    def fake(self, body):
        executable = self.root / "runtime/Fake"
        executable.write_text("#!/usr/bin/python3\n" + body)
        executable.chmod(0o700)
        return launch.sandbox_command(self.root, "Fake", [])

    def fake_inputs(self):
        from fp_build import sha256
        runtime = self.output / "fake-runtime"
        runtime.mkdir()
        shutil.copyfile("/usr/bin/true", runtime / "FakeElf")
        for name in ("data", "icons", "shaders-builtin", "translations"):
            (runtime / name).mkdir()
        (self.output / "vita-fp").mkdir()
        vpk = self.output / "vita-fp/ww_fp.vpk"
        self.write_package(vpk, b"first fake package")
        evidence = self.output / "build.json"
        evidence.write_text(json.dumps({"source": "a" * 40, "artifacts": {
            str(vpk): {"sha256": sha256(vpk)}}}))
        argv = ["fp_launch", "--runtime", str(runtime / "FakeElf"), "--runtime-sha256",
                sha256(runtime / "FakeElf"), "--runtime-source-revision", "fake",
                "--libraries", str(runtime), "--build-evidence", str(evidence), "--out", str(self.output)]
        return runtime, vpk, argv

    @staticmethod
    def write_package(path, payload):
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("eboot.bin", payload)
            archive.writestr("sce_sys/param.sfo", b"BWWF00003\0")

    def test_sandbox_cannot_redirect_result_or_modify_plan(self):
        sentinel = self.output / "sentinel"
        sentinel.write_text("untouched")
        plan = self.root / "evidence/plan.json"
        plan.write_text("original plan")
        command = self.fake(
            "import os\nfrom pathlib import Path\n"
            "try:\n"
            f" os.symlink({str(sentinel)!r}, '/run/ww03/evidence/result.json')\n"
            "except OSError: pass\n"
            "try:\n Path('/run/ww03/evidence/plan.json').write_text('tampered')\n"
            "except OSError: pass\n"
            "print('preopened log works')\n")
        status = launch.run_owned(command, self.root, 5)
        self.assertEqual(status, 0)
        self.assertNotEqual(launch.collect_result(self.root, "run", "source", "package", status), 0)
        for path, expected in ((sentinel, "untouched"), (plan, "original plan")):
            with self.subTest(path=path.name):
                self.assertEqual(path.read_text(), expected)
        self.assertIn("preopened log works", (self.root / "evidence/process.log").read_text())

    def test_host_result_rejects_preexisting_symlink(self):
        sentinel = self.output / "sentinel"
        sentinel.write_text("untouched")
        (self.root / "evidence/result.json").symlink_to(sentinel)
        with self.assertRaises(OSError):
            launch.collect_result(self.root, "run", "source", "package", 0)
        self.assertEqual(sentinel.read_text(), "untouched")

    def test_host_result_rejects_symlink_parent(self):
        destination = self.output / "redirected-evidence"
        destination.mkdir()
        sentinel = destination / "result.json"
        sentinel.write_text("untouched")
        (self.root / "evidence").rename(self.root / "original-evidence")
        (self.root / "evidence").symlink_to(destination)
        with self.assertRaises(OSError):
            launch.collect_result(self.root, "run", "source", "package", 0)
        self.assertEqual(sentinel.read_text(), "untouched")

    def test_fake_receives_eof_instead_of_caller_stdin(self):
        sentinel = self.output / "stdin-sentinel"
        sentinel.write_text("caller input must stay private")
        command = self.fake("import sys\nfrom pathlib import Path\n"
                            "Path('/run/ww03/data/stdin').write_bytes(sys.stdin.buffer.read())\n")
        saved = os.dup(0)
        try:
            with sentinel.open("rb") as stream:
                os.dup2(stream.fileno(), 0)
                status = launch.run_owned(command, self.root, 5)
        finally:
            os.dup2(saved, 0)
            os.close(saved)
        self.assertEqual(status, 0)
        self.assertEqual((self.root / "data/stdin").read_bytes(), b"")

    def test_replaced_source_package_is_rejected_after_staging(self):
        _, vpk, argv = self.fake_inputs()
        stage_runtime = launch.stage_runtime

        def replace_during_stage(*args):
            assets = stage_runtime(*args)
            self.write_package(vpk, b"replacement fake package")
            return assets

        with mock.patch.object(sys, "argv", argv), \
                mock.patch.object(launch, "build_directory", return_value=self.output), \
                mock.patch.object(launch, "stage_runtime", side_effect=replace_during_stage):
            with self.assertRaisesRegex(ValueError, "VPK.*build evidence"):
                launch.main()
        run, = self.output.glob("vita3k-run-*")
        self.assertFalse((run / launch.REPORT_DIRECTORY / "fp.request").exists())

    def run_fake_library_case(self, mutate):
        from fp_vectors import cases
        runtime, _, argv = self.fake_inputs()
        library = runtime / "public.so"
        library.write_bytes(b"first library")
        (runtime / "alias.so").symlink_to("public.so")
        stage_runtime = launch.stage_runtime
        sandbox_command = launch.sandbox_command
        run_owned = launch.run_owned
        lines = [f"CASE {case['id']} mode={case['mode']} expected={case['expected']} "
                 f"actual={case['expected']} path=1 PASS" for case in cases()]
        lines.append(f"END PASS cases={len(cases())} failures=0")

        def stage_fake(*args):
            assets = stage_runtime(*args)
            script = args[0] / "runtime/Fake"
            script.write_text("#!/usr/bin/python3\nfrom pathlib import Path\n"
                              f"directory=Path('/run/ww03/{launch.REPORT_DIRECTORY}')\n"
                              "identity=(directory/'fp.request').read_text().strip()\n"
                              f"text='RUN '+identity+'\\n'+{chr(10).join(lines) + chr(10)!r}\n"
                              "(directory/'fp.report').write_text(text)\n")
            script.chmod(0o700)
            return assets

        def finish_with_mutation(*args):
            status = run_owned(*args)
            self.assertEqual(status, 0)
            if mutate:
                library.write_bytes(b"changed library")
            return status

        with mock.patch.object(sys, "argv", [*argv, "--execute-reviewed"]), \
                mock.patch.object(launch, "build_directory", return_value=self.output), \
                mock.patch.object(launch, "stage_runtime", side_effect=stage_fake), \
                mock.patch.object(launch, "sandbox_command", side_effect=lambda root, exe, arguments, libs:
                                  sandbox_command(root, "Fake", [], libs)), \
                mock.patch.object(launch, "run_owned", side_effect=finish_with_mutation):
            status = launch.main()
        run, = self.output.glob("vita3k-run-*")
        return status, run, library

    def test_library_mutation_invalidates_otherwise_passing_fake_run(self):
        status, run, library = self.run_fake_library_case(True)
        self.assertNotEqual(status, 0)
        record = json.loads((run / "evidence/result.json").read_text())
        self.assertEqual(record["completion"], "INCOMPLETE")
        self.assertEqual(record["reported_semantic"], "PASS")
        self.assertIn("library tree changed", record["identity_error"])
        before = json.loads((run / "evidence/libraries-before.json").read_text())
        after = json.loads((run / "evidence/libraries-after.json").read_text())
        self.assertNotEqual(before["sha256"], after["sha256"])
        self.assertEqual(before["entries"]["alias.so"]["resolved"], str(library))
        self.assertEqual(before["entries"]["alias.so"]["sha256"], before["entries"]["public.so"]["sha256"])

    def test_unchanged_libraries_allow_complete_fake_run(self):
        status, run, _ = self.run_fake_library_case(False)
        self.assertEqual(status, 0)
        record = json.loads((run / "evidence/result.json").read_text())
        self.assertEqual(record["completion"], "COMPLETE")
        self.assertEqual(record["semantic"], "PASS")
        before = json.loads((run / "evidence/libraries-before.json").read_text())
        after = json.loads((run / "evidence/libraries-after.json").read_text())
        self.assertEqual(before, after)

    def test_fifo_report_does_not_block_collection(self):
        report = self.root / launch.REPORT_DIRECTORY / "fp.report"
        report.parent.mkdir(parents=True)
        command = self.fake("import os\n"
                            f"os.mkfifo('/run/ww03/{launch.REPORT_DIRECTORY}/fp.report')\n")
        self.assertEqual(launch.run_owned(command, self.root, 5), 0)
        collector = (
            "import importlib.util,sys\nfrom pathlib import Path\n"
            f"sys.path.insert(0, {str(ROOT / 'scripts/vita/armv7')!r})\n"
            f"spec=importlib.util.spec_from_file_location('launcher', {str(source)!r})\n"
            "module=importlib.util.module_from_spec(spec)\nspec.loader.exec_module(module)\n"
            f"assert module.collect_result(Path({str(self.root)!r}), 'run', 'source', 'package', 0) != 0\n")
        try:
            result = subprocess.run([sys.executable, "-c", collector], capture_output=True, text=True, timeout=2)
        except subprocess.TimeoutExpired:
            self.fail("collector blocked on the fake FIFO after process completion")
        self.assertEqual(result.returncode, 0, result.stderr)
        record = json.loads((self.root / "evidence/result.json").read_text())
        self.assertEqual(record["completion"], "INCOMPLETE")

    def test_abnormal_exit_does_not_accept_a_passing_report(self):
        from fp_vectors import cases
        lines = ["RUN run source package"]
        for case in cases():
            lines.append(f"CASE {case['id']} mode={case['mode']} expected={case['expected']} "
                         f"actual={case['expected']} path=1 PASS")
        lines.append(f"END PASS cases={len(cases())} failures=0")
        for process_status in (124, -11, 7):
            with self.subTest(process_status=process_status):
                root = self.output / f"exit-{process_status}"
                launch.create_run(root)
                report = root / launch.REPORT_DIRECTORY / "fp.report"
                report.parent.mkdir(parents=True)
                report.write_text("\n".join(lines) + "\n")
                self.assertEqual(launch.collect_result(root, "run", "source", "package", process_status),
                                 process_status)
                record = json.loads((root / "evidence/result.json").read_text())
                self.assertEqual(record["semantic"], "UNAVAILABLE")
                self.assertEqual(record["completion"], "INCOMPLETE")

    def test_oversized_report_is_rejected_before_parsing(self):
        report = self.root / launch.REPORT_DIRECTORY / "fp.report"
        report.parent.mkdir(parents=True)
        with report.open("wb") as stream:
            stream.truncate(65537)
        self.assertNotEqual(launch.collect_result(self.root, "run", "source", "package", 0), 0)
        record = json.loads((self.root / "evidence/result.json").read_text())
        self.assertIn("size limit", record["report_error"])

    def test_report_type_change_after_open_does_not_change_read_descriptor(self):
        report = self.root / launch.REPORT_DIRECTORY / "fp.report"
        report.parent.mkdir(parents=True)
        report.write_bytes(b"original bytes")
        open_file = os.open

        def replace_after_open(path, flags, *args, **kwargs):
            descriptor = open_file(path, flags, *args, **kwargs)
            if path == "fp.report":
                report.rename(report.with_name("original.report"))
                os.mkfifo(report)
            return descriptor

        with mock.patch.object(launch.os, "open", side_effect=replace_after_open):
            self.assertEqual(launch.read_report(self.root), b"original bytes")

    def test_library_symlink_retarget_changes_identity_with_equal_file_bytes(self):
        from fp_libraries import library_identity
        library = self.output / "libraries"
        library.mkdir()
        for name in ("first.so", "second.so"):
            (library / name).write_bytes(b"identical bytes")
        alias = library / "alias.so"
        alias.symlink_to("first.so")
        before = library_identity(library)
        alias.unlink()
        alias.symlink_to("second.so")
        after = library_identity(library)
        self.assertNotEqual(before["sha256"], after["sha256"])
        self.assertEqual(before["entries"]["alias.so"]["resolved"], str(library / "first.so"))
        self.assertEqual(after["entries"]["alias.so"]["resolved"], str(library / "second.so"))


if __name__ == "__main__":
    unittest.main()
