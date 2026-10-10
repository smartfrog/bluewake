import json
import socket
import subprocess
from pathlib import Path
import sys
import unittest
from unittest import mock
import zipfile

import test_fp_campaign as fp_tests
from test_arm_report import report
from test_mem_launch import sfo
from fp_profile import CONFIG_PATH

launch = fp_tests.launch


class ArmLauncherTest(unittest.TestCase):
    def setUp(self):
        fp_tests.CampaignTest.setUp(self)
        self.vpk = self.output / "vita-arm/ww_arm.vpk"
        self.vpk.parent.mkdir()
        with zipfile.ZipFile(self.vpk, "w") as archive:
            archive.writestr("eboot.bin", b"SCE\0synthetic unit test SELF")
            archive.writestr("sce_sys/param.sfo", sfo(b"BWWA00008\0"))
        (self.output / "build.json").write_text(json.dumps({"source": "a" * 40,
            "method": "scalar-vfp-v1", "title": "BWWA00008", "artifacts": {
                str(self.vpk): {"sha256": launch.sha256(self.vpk)}}}))

    def stage(self):
        with mock.patch.object(sys, "argv", self.argv), \
                mock.patch.object(launch, "build_directory", return_value=self.output):
            self.assertEqual(launch.main(control=True), 0)
        root, = self.output.glob("vita3k-run-*")
        return root, json.loads((root / "evidence/plan.json").read_text())

    def test_stage_only_has_exact_baseline_pair_and_no_request_or_process(self):
        with mock.patch.object(launch.subprocess, "Popen", side_effect=AssertionError("unexpected process")):
            root, plan = self.stage()
        self.assertEqual((plan["process_budget_used"], plan["ticket_process_budget"]), (0, 4))
        self.assertEqual(plan["phases"]["guest"]["command"][-5:],
                         ["/stack/vita3k/Vita3K", "--backend-renderer", "OpenGL", "--installed-path", "BWWA00008"])
        self.assertFalse((root / launch.REPORT_DIRECTORY / "arm.request").exists())
        self.assertFalse(list(root.rglob("process.log")))

    def fake_campaign(self, installer="sys.exit(0)", guest="sys.exit(0)", stale=False, execute=None):
        body = report(runtime_failure=True).split("\n", 1)[1]
        script = ("#!/usr/bin/python3\nimport sys,time,zipfile\nfrom pathlib import Path\n"
                  "root=Path('/run/ww03')\n"
                  f"directory=root/{str(launch.REPORT_DIRECTORY)!r}\n"
                  f"config=root/{str(CONFIG_PATH)!r}\n"
                  "if '--console' in sys.argv:\n"
                  " assert not (directory/'arm.request').exists()\n"
                  " app=root/'data/Vita3K/Vita3K/ux0/app/BWWA00008'; app.mkdir(parents=True)\n"
                  " with zipfile.ZipFile('arm.vpk') as archive: archive.extractall(app)\n"
                  " config.write_text(config.read_text().replace('user-id: \"\"','user-id: 00'))\n" +
                  (" directory.mkdir(parents=True); (directory/'arm.report').write_text('stale')\n" if stale else "") +
                  " " + installer + "\nelse:\n"
                  " assert sys.argv[1:]==['--backend-renderer','OpenGL','--installed-path','BWWA00008']\n"
                  " assert 'user-id: \"\"' in config.read_text()\n"
                  " try: (root/'phases/guest/evidence/plan.json').write_text('bad')\n"
                  " except OSError: pass\n else: raise AssertionError('writable plan')\n"
                  " token,source,package=(directory/'arm.request').read_text().split()\n"
                  f" text='ARM08 '+token+' '+source+' '+package+' method=scalar-vfp-v1 phase=guest platform=VITA\\n'+{body!r}\n"
                  " (directory/'arm.partial').write_text(text); (directory/'arm.partial').rename(directory/'arm.report')\n"
                  " " + guest + "\n")
        original_stage, sandbox, owned = launch.stage_runtime, launch.sandbox_command, launch.run_owned
        calls = []

        def stage_runtime(*args):
            result = original_stage(*args)
            fake = args[0] / "runtime/Fake"
            fake.write_text(script)
            fake.chmod(0o700)
            return result

        def run_owned(*args, **kwargs):
            self.assertLess(len(calls), 2)
            status = owned(*args, **kwargs)
            calls.append(status)
            return status

        with mock.patch.object(launch, "stage_runtime", side_effect=stage_runtime), \
                mock.patch.object(launch, "sandbox_command", side_effect=lambda root, exe, argv, libs, **kw:
                                  sandbox(root, "Fake", argv, libs, **kw)):
            root, plan = self.stage()
        from fp_campaign import execute_campaign
        with mock.patch.object(launch, "run_owned", side_effect=run_owned):
            status = (execute or execute_campaign)(root, plan)
        return status, root, calls

    def source_checkpoint(self):
        (self.output / ".gitignore").write_text("*\n!.gitignore\n!checkpoint.txt\n")
        (self.output / "checkpoint.txt").write_text("Fake-only coordinator checkpoint\n")
        for arguments in (("init",), ("add", ".gitignore", "checkpoint.txt"),
                          ("-c", "user.name=Fred", "-c", "user.email=297854+fdematos@users.noreply.github.com",
                           "commit", "-m", "Create isolated launch test checkpoint")):
            subprocess.run(["git", "-C", self.output, *arguments], check=True, capture_output=True)
        source = subprocess.check_output(["git", "-C", self.output, "rev-parse", "HEAD"], text=True).strip()
        evidence = self.output / "build.json"
        build = json.loads(evidence.read_text())
        build["source"] = source
        evidence.write_text(json.dumps(build))
        self.assertEqual(subprocess.check_output(["git", "-C", self.output, "status", "--porcelain"], text=True), "")

    def test_coherent_socket_and_three_plan_tamper_is_rejected_before_reservation_or_subprocess(self):
        import arm_execute as executor
        self.source_checkpoint()
        display = socket.socket(socket.AF_UNIX)
        self.addCleanup(display.close)
        display_path = self.output / "display"
        display.bind(str(display_path))
        sandbox = launch.sandbox_command

        def execute(root, plan):
            path = root / "evidence/plan.json"
            reviewed_hash = launch.sha256(path)
            authorization = plan["authorization"]
            plan["wayland_socket"] = str(display_path)
            for name, arguments in (("install", ["--console", "--backend-renderer", "OpenGL", "arm.vpk"]),
                                    ("guest", ["--backend-renderer", "OpenGL", "--installed-path", "BWWA00008"])):
                phase = plan["phases"][name]
                phase["command"] = sandbox(root, "Fake", arguments, self.libraries, wayland_socket=display_path)
                (root / "phases" / name / "evidence/plan.json").write_text(json.dumps(phase) + "\n")
            plan["command"] = plan["phases"]["install"]["command"]
            path.write_text(json.dumps(plan) + "\n")
            self.assertNotEqual(launch.sha256(path), reviewed_hash)
            with mock.patch.object(executor, "ROOT", self.output), \
                    mock.patch.object(executor, "sandbox_command", side_effect=lambda root, exe, argv, libs, **kw:
                                      sandbox(root, "Fake", argv, libs, **kw)), \
                    mock.patch.object(executor, "reserve_pair", wraps=executor.reserve_pair) as reservation, \
                    mock.patch.object(executor.subprocess, "Popen", wraps=subprocess.Popen) as processes:
                with self.assertRaisesRegex(ValueError, "reviewed plan"):
                    executor.execute(path, authorization, reviewed_hash)
                reservation.assert_not_called()
                processes.assert_not_called()
                self.assertFalse((self.output / "build-ww08-baseline-reservation.json").exists())
            return 0

        self.fake_campaign(execute=execute)

    def test_unchanged_plan_and_external_hash_pass_all_guards_to_fake_pair(self):
        import arm_execute as executor
        self.source_checkpoint()
        sandbox = launch.sandbox_command

        def execute(root, plan):
            path = root / "evidence/plan.json"
            reviewed_hash = launch.sha256(path)
            with mock.patch.object(executor, "ROOT", self.output), \
                    mock.patch.object(executor, "sandbox_command", side_effect=lambda root, exe, argv, libs, **kw:
                                      sandbox(root, "Fake", argv, libs, **kw)), \
                    mock.patch.object(executor, "read_profile_file", wraps=executor.read_profile_file) as reads:
                status = executor.execute(path, plan["authorization"], reviewed_hash)
            self.assertEqual(sum(call.args[0] == path for call in reads.call_args_list), 1)
            self.assertTrue((self.output / "build-ww08-baseline-reservation.json").is_file())
            return status

        status, _, calls = self.fake_campaign(execute=execute)
        self.assertEqual((status, len(calls)), (1, 2))

    def test_invalid_external_hash_is_rejected_before_read_reservation_or_subprocess(self):
        import arm_execute as executor
        for value in (None, "", "a" * 63, "a" * 65, "g" * 64, "A" * 64, "a" * 64 + "\n"):
            with self.subTest(value=value), \
                    mock.patch.object(executor, "read_profile_file") as reads, \
                    mock.patch.object(executor, "reserve_pair") as reservation, \
                    mock.patch.object(executor.subprocess, "Popen") as processes:
                with self.assertRaisesRegex(ValueError, "reviewed plan SHA-256"):
                    executor.execute(self.output / "missing-plan.json", "old-authorization", value)
                reads.assert_not_called()
                reservation.assert_not_called()
                processes.assert_not_called()
                self.assertFalse((self.output / "build-ww08-baseline-reservation.json").exists())

    def test_cli_requires_independently_supplied_root_plan_hash(self):
        import arm_execute as executor
        with mock.patch.object(sys, "argv", ["arm_execute", "--plan", str(self.output / "plan.json"),
                                            "--authorize-pair", "old-authorization"]), \
                mock.patch.object(executor, "reserve_pair") as reservation, \
                mock.patch.object(executor.subprocess, "Popen") as processes:
            with self.assertRaises(SystemExit) as result:
                executor.main()
            self.assertEqual(result.exception.code, 2)
            reservation.assert_not_called()
            processes.assert_not_called()

    def test_valid_runtime_failure_is_complete_and_distinct_from_control(self):
        status, root, calls = self.fake_campaign()
        self.assertEqual((status, len(calls)), (1, 2))
        result = json.loads((root / "phases/guest/evidence/result.json").read_text())
        self.assertEqual((result["completion"], result["reported_controls"], result["reported_runtime"]),
                         ("COMPLETE", "PASS", "FAIL"))

    def test_preparation_failure_blocks_request_and_second_process(self):
        status, root, calls = self.fake_campaign(installer="sys.exit(7)")
        self.assertEqual((status, len(calls)), (7, 1))
        self.assertFalse((root / launch.REPORT_DIRECTORY / "arm.request").exists())

    def test_stale_output_is_preserved_and_blocks_guest(self):
        status, root, calls = self.fake_campaign(stale=True)
        self.assertEqual((status, len(calls)), (2, 1))
        self.assertEqual((root / launch.REPORT_DIRECTORY / "arm.report").read_text(), "stale")

    def test_lingering_guest_does_not_convert_report_into_successful_completion(self):
        status, root, calls = self.fake_campaign(guest="time.sleep(60)")
        self.assertEqual((status, len(calls)), (125, 2))
        result = json.loads((root / "phases/guest/evidence/result.json").read_text())
        self.assertEqual((result["completion"], result["reported_semantic"]), ("INCOMPLETE", "FAIL"))

    def test_execute_reviewed_flag_cannot_create_and_run_an_unreviewed_pair(self):
        with mock.patch.object(sys, "argv", [*self.argv, "--execute-reviewed"]), \
                mock.patch.object(launch.subprocess, "Popen", side_effect=AssertionError("unexpected process")), \
                self.assertRaises(SystemExit):
            launch.main(control=True)

    def test_wrong_exact_pair_authorization_stops_before_process(self):
        from arm_execute import execute
        root, _ = self.stage()
        with mock.patch.object(launch.subprocess, "Popen", side_effect=AssertionError("unexpected process")), \
                self.assertRaisesRegex(ValueError, "authorization"):
            path = root / "evidence/plan.json"
            execute(path, "wrong", launch.sha256(path))

    def test_exact_pair_reservation_cannot_be_repeated(self):
        from arm_execute import reserve_pair
        budget = self.output / "reservation.json"
        reserve_pair(budget, {"authorization": "exact", "source": "a" * 40})
        with self.assertRaises(FileExistsError):
            reserve_pair(budget, {"authorization": "exact", "source": "a" * 40})

    def test_failed_installer_still_restores_canonical_environment(self):
        status, root, _ = self.fake_campaign(installer="sys.exit(7)")
        self.assertEqual(status, 7)
        self.assertIn('user-id: ""', (root / CONFIG_PATH).read_text())
