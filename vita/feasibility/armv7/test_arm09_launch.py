import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock
import zipfile

import test_fp_campaign as fp_tests
from test_arm_report import report
from test_mem_launch import sfo
from fp_profile import (CONFIG_ALT_SHA256, CONFIG_PATH, CONFIG_TEMPLATE, alt_config,
                        checked_config)

launch = fp_tests.launch


class Arm09LauncherTest(unittest.TestCase):
    def setUp(self):
        fp_tests.CampaignTest.setUp(self)
        self.out = self.output / "build-ww09"
        self.out.mkdir()
        self.vpk = self.out / "vita-arm/ww_arm.vpk"
        self.vpk.parent.mkdir()
        with zipfile.ZipFile(self.vpk, "w") as archive:
            archive.writestr("eboot.bin", b"SCE\0synthetic unit test SELF")
            archive.writestr("sce_sys/param.sfo", sfo(b"BWWA00008\0"))
        (self.out / "build.json").write_text(json.dumps({"source": "a" * 40,
            "method": "scalar-vfp-v1", "title": "BWWA00008", "artifacts": {
                str(self.vpk): {"sha256": launch.sha256(self.vpk)}}}))
        self.argv = ["fp_launch", "--two-phase", "--runtime", str(self.runtime / "Vita3K"),
                     "--runtime-sha256", launch.sha256(self.runtime / "Vita3K"),
                     "--runtime-source-revision", "fake", "--libraries", str(self.libraries),
                     "--firmware", str(self.firmware), "--build-evidence", str(self.out / "build.json"),
                     "--out", str(self.out), "--timeout", "1"]
        self.alt = alt_config(checked_config(CONFIG_TEMPLATE))

    def stage(self):
        import arm09_launch
        before = set(self.out.glob("vita3k-run-*"))
        with mock.patch.object(sys, "argv", self.argv), \
                mock.patch.object(launch, "ROOT", self.output), \
                mock.patch.object(launch, "build_directory", return_value=self.out):
            self.assertEqual(arm09_launch.main(), 0)
        root, = set(self.out.glob("vita3k-run-*")) - before
        return root, json.loads((root / "evidence/plan.json").read_text())

    def test_stage_only_cpu_opt_off_plan_claims_reservation_and_runs_no_process(self):
        with mock.patch.object(launch.subprocess, "Popen", side_effect=AssertionError("unexpected process")):
            root, plan = self.stage()
        self.assertEqual(plan["comparison"], "cpu-opt-off")
        self.assertEqual(plan["authorization"],
                         f"ARM09:{plan['source']}:{plan['run']}:cpu-opt-off:1,2")
        self.assertEqual((plan["process_budget"], plan["ticket_process_budget"],
                          plan["process_budget_used"]), (2, 2, 0))
        self.assertFalse(plan["real_execution"])
        self.assertEqual(plan["private_config"]["sha256"], CONFIG_ALT_SHA256)
        self.assertEqual((root / CONFIG_PATH).read_bytes(), self.alt)
        reservation = json.loads((self.out / "campaign-reservation.json").read_text())
        self.assertEqual((reservation["authorization"], reservation["source"], reservation["run"]),
                         (plan["authorization"], plan["source"], plan["run"]))
        self.assertEqual((reservation["reserved_process_numbers"], reservation["retry"]), ([1, 2], "FORBIDDEN"))
        self.assertEqual(plan["phases"]["install"]["command"][-5:],
                         ["/stack/vita3k/Vita3K", "--console", "--backend-renderer", "OpenGL", "arm.vpk"])
        self.assertEqual(plan["phases"]["guest"]["command"][-5:],
                         ["/stack/vita3k/Vita3K", "--backend-renderer", "OpenGL", "--installed-path", "BWWA00008"])
        self.assertFalse((root / launch.REPORT_DIRECTORY / "arm.request").exists())
        self.assertFalse(list(root.rglob("process.log")))

    def test_execute_reviewed_flag_cannot_stage_and_run(self):
        import arm09_launch
        with mock.patch.object(sys, "argv", [*self.argv, "--execute-reviewed"]), \
                mock.patch.object(launch.subprocess, "Popen", side_effect=AssertionError("unexpected process")), \
                self.assertRaises(SystemExit):
            arm09_launch.main()

    def test_cpu_opt_off_staging_requires_the_fixed_ignored_output_directory(self):
        import arm09_launch
        argv = list(self.argv)
        argv[argv.index(str(self.out))] = str(self.output)
        with mock.patch.object(sys, "argv", argv), \
                mock.patch.object(launch, "ROOT", self.output), \
                mock.patch.object(launch, "build_directory", return_value=self.output):
            with self.assertRaises(SystemExit):
                arm09_launch.main()
        self.assertFalse((self.output / "campaign-reservation.json").exists())

    def test_second_staging_is_refused_when_campaign_reservation_exists(self):
        import arm09_launch
        (self.out / "campaign-reservation.json").write_text("{}\n")
        with mock.patch.object(sys, "argv", self.argv), \
                mock.patch.object(launch, "ROOT", self.output), \
                mock.patch.object(launch, "build_directory", return_value=self.out):
            with self.assertRaisesRegex(ValueError, "reserved"):
                arm09_launch.main()
        self.assertFalse(list(self.out.glob("vita3k-run-*")))

    def source_checkpoint(self):
        (self.output / ".gitignore").write_text("*\n!.gitignore\n!checkpoint.txt\n")
        (self.output / "checkpoint.txt").write_text("Fake-only coordinator checkpoint\n")
        for arguments in (("init",), ("add", ".gitignore", "checkpoint.txt"),
                          ("-c", "user.name=Fred", "-c", "user.email=297854+fdematos@users.noreply.github.com",
                           "commit", "-m", "Create isolated launch test checkpoint")):
            subprocess.run(["git", "-C", self.output, *arguments], check=True, capture_output=True)
        source = subprocess.check_output(["git", "-C", self.output, "rev-parse", "HEAD"], text=True).strip()
        evidence = self.out / "build.json"
        build = json.loads(evidence.read_text())
        build["source"] = source
        evidence.write_text(json.dumps(build))
        self.assertEqual(subprocess.check_output(["git", "-C", self.output, "status", "--porcelain"], text=True), "")

    def fake_campaign(self, installer="sys.exit(0)", guest="sys.exit(0)", stale=False, execute=None,
                      installer_extra="", guest_extra=""):
        body = report(runtime_failure=True).split("\n", 1)[1]
        script = ("#!/usr/bin/python3\nimport sys,time,zipfile\nfrom pathlib import Path\n"
                  "root=Path('/run/ww03')\n"
                  f"directory=root/{str(launch.REPORT_DIRECTORY)!r}\n"
                  f"config=root/{str(CONFIG_PATH)!r}\n"
                  "if '--console' in sys.argv:\n"
                  " assert sys.argv[1:]==['--console','--backend-renderer','OpenGL','arm.vpk']\n"
                  " assert not (directory/'arm.request').exists()\n"
                  " app=root/'data/Vita3K/Vita3K/ux0/app/BWWA00008'; app.mkdir(parents=True)\n"
                  " with zipfile.ZipFile('arm.vpk') as archive: archive.extractall(app)\n"
                  " config.write_text(config.read_text().replace('user-id: \"\"','user-id: 00'))\n" +
                  (" directory.mkdir(parents=True); (directory/'arm.report').write_text('stale')\n" if stale else "") +
                  "\n".join(" " + line for line in installer_extra.splitlines()) + "\n"
                  " " + installer + "\n"
                  "else:\n"
                  " assert sys.argv[1:]==['--backend-renderer','OpenGL','--installed-path','BWWA00008']\n"
                  " assert 'user-id: \"\"' in config.read_text()\n"
                  " try: (root/'phases/guest/evidence/plan.json').write_text('bad')\n"
                  " except OSError: pass\n else: raise AssertionError('writable plan')\n"
                  " token,source,package=(directory/'arm.request').read_text().split()\n"
                  f" text='ARM08 '+token+' '+source+' '+package+' method=scalar-vfp-v1 phase=guest platform=VITA\\n'+{body!r}\n"
                  " (directory/'arm.partial').write_text(text); (directory/'arm.partial').rename(directory/'arm.report')\n" +
                  "\n".join(" " + line for line in guest_extra.splitlines()) + "\n"
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

    def execute_reviewed(self, root, plan):
        import arm_execute as executor
        sandbox = launch.sandbox_command
        path = root / "evidence/plan.json"
        with mock.patch.object(executor, "ROOT", self.output), \
                mock.patch.object(executor, "sandbox_command", side_effect=lambda root, exe, argv, libs, **kw:
                                  sandbox(root, "Fake", argv, libs, **kw)):
            return executor.execute_cpu_opt_off(path, plan["authorization"], launch.sha256(path))

    def execute_rejected(self, root, authorization, reviewed_hash, message):
        import arm_execute as executor
        reservation = self.out / "campaign-reservation.json"
        claimed = reservation.read_bytes()
        with mock.patch.object(executor, "ROOT", self.output), \
                mock.patch.object(executor, "execute_campaign") as campaign:
            with self.assertRaisesRegex(ValueError, message):
                executor.execute_cpu_opt_off(root / "evidence/plan.json", authorization, reviewed_hash)
            campaign.assert_not_called()
        self.assertEqual(reservation.read_bytes(), claimed)

    def test_unchanged_09_plan_and_external_hash_pass_all_guards_to_fake_pair(self):
        self.source_checkpoint()

        def execute(root, plan):
            reservation = self.out / "campaign-reservation.json"
            self.assertTrue(reservation.is_file())
            status = self.execute_reviewed(root, plan)
            self.assertEqual(json.loads(reservation.read_text())["run"], plan["run"])
            return status

        status, root, calls = self.fake_campaign(execute=execute)
        self.assertEqual((status, len(calls)), (1, 2))

    def test_unknown_plan_variant_is_rejected_before_execution(self):
        self.source_checkpoint()
        root, plan = self.stage()
        path = root / "evidence/plan.json"
        original = path.read_bytes()
        for variant in ("cpu-opt-off-extra", "pristine-baseline"):
            with self.subTest(variant=variant):
                mutated = json.loads(original)
                mutated["comparison"] = variant
                path.write_bytes(json.dumps(mutated).encode() + b"\n")
                self.execute_rejected(root, mutated["authorization"], launch.sha256(path),
                                      "cpu-opt-off ARM09 plan")
        path.write_bytes(original)

    def test_substituted_staged_configuration_is_rejected_before_execution(self):
        self.source_checkpoint()
        root, plan = self.stage()
        config = root / CONFIG_PATH
        original = config.read_bytes()
        substitutions = {"baseline": checked_config(CONFIG_TEMPLATE),
                         "second-change": self.alt.replace(b"show-welcome: false", b"show-welcome: true"),
                         "user-id-serialized": self.alt.replace(b'user-id: ""\n', b"user-id: 00\n")}
        for name, content in substitutions.items():
            with self.subTest(name=name):
                config.write_bytes(content)
                self.execute_rejected(root, plan["authorization"], launch.sha256(root / "evidence/plan.json"),
                                      "configuration")
                config.write_bytes(original)

    def test_stale_plan_source_or_external_hash_is_rejected_before_execution(self):
        self.source_checkpoint()
        root, plan = self.stage()
        path = root / "evidence/plan.json"
        reviewed_hash = launch.sha256(path)
        stale = json.loads(json.dumps(plan))
        stale["source"] = "c" * 40
        stale["authorization"] = f"ARM09:{stale['source']}:{stale['run']}:cpu-opt-off:1,2"
        path.write_text(json.dumps(stale) + "\n")
        for label, plan_hash in (("stale-hash", reviewed_hash), ("invalid-hash", "g" * 64),
                                 ("stale-source", launch.sha256(path))):
            with self.subTest(label=label):
                self.execute_rejected(root, stale["authorization"], plan_hash,
                                      "reviewed plan SHA-256" if label == "invalid-hash" else
                                      "reviewed plan" if label == "stale-hash" else "clean checkpoint")

    def test_reserved_campaign_refuses_second_uuid_and_retry_before_process(self):
        import arm_execute as executor
        import fp_campaign
        self.source_checkpoint()

        def execute(root, plan):
            return self.execute_reviewed(root, plan)

        status, first_root, calls = self.fake_campaign(execute=execute)
        self.assertEqual((status, len(calls)), (1, 2))
        reservation = self.out / "campaign-reservation.json"
        first = reservation.read_bytes()
        first_plan = json.loads((first_root / "evidence/plan.json").read_text())

        with mock.patch.object(executor, "ROOT", self.output), \
                mock.patch.object(executor, "execute_campaign") as campaign:
            with self.assertRaisesRegex(ValueError, "retry forbidden"):
                self.execute_reviewed(first_root, first_plan)
            campaign.assert_not_called()

        decoy = self.out / "stage-b-reservation.json"
        with mock.patch.object(fp_campaign, "reservation_path", return_value=decoy):
            second_root, second_plan = self.stage()
        self.assertTrue(decoy.is_file())
        self.assertNotEqual(second_plan["run"], first_plan["run"])
        with mock.patch.object(executor, "ROOT", self.output), \
                mock.patch.object(executor, "execute_campaign") as campaign:
            with self.assertRaisesRegex(ValueError, "reservation"):
                executor.execute_cpu_opt_off(second_root / "evidence/plan.json", second_plan["authorization"],
                                             launch.sha256(second_root / "evidence/plan.json"))
            campaign.assert_not_called()

        import arm09_launch
        with mock.patch.object(sys, "argv", self.argv), \
                mock.patch.object(launch, "ROOT", self.output), \
                mock.patch.object(launch, "build_directory", return_value=self.out):
            with self.assertRaisesRegex(ValueError, "reserved"):
                arm09_launch.main()
        self.assertEqual(reservation.read_bytes(), first)

    def test_installer_second_mutation_is_rejected_and_alternate_bytes_restored(self):
        status, root, calls = self.fake_campaign(
            installer_extra="config.write_text(config.read_text().replace('show-welcome: false','show-welcome: true'))")
        self.assertNotEqual(status, 0)
        self.assertEqual(len(calls), 1)
        prepared = json.loads((root / "phases/install/evidence/preparation.json").read_text())
        self.assertEqual(prepared["readiness"], "REJECTED")
        self.assertIn("configuration mutation", prepared["error"])
        self.assertIn("show-welcome: true", (root / "phases/install/evidence/config-emitted.yml").read_text())
        self.assertEqual((root / CONFIG_PATH).read_bytes(), self.alt)
        self.assertFalse((root / launch.REPORT_DIRECTORY / "arm.request").exists())

    def test_guest_rejection_still_restores_alternate_bytes(self):
        status, root, calls = self.fake_campaign(
            guest_extra="config.write_text(config.read_text().replace('show-welcome: false','show-welcome: true'))")
        self.assertEqual(len(calls), 2)
        result = json.loads((root / "phases/guest/evidence/result.json").read_text())
        self.assertEqual(result["completion"], "INCOMPLETE")
        self.assertIn("configuration mutation", result["identity_error"])
        self.assertIn("show-welcome: true", (root / "phases/guest/evidence/config-emitted.yml").read_text())
        self.assertEqual((root / CONFIG_PATH).read_bytes(), self.alt)

    def test_failed_installer_still_restores_alternate_environment(self):
        status, root, calls = self.fake_campaign(installer="sys.exit(7)")
        self.assertEqual((status, len(calls)), (7, 1))
        self.assertEqual((root / CONFIG_PATH).read_bytes(), self.alt)

    def test_stale_report_identity_is_preserved_as_incomplete(self):
        status, root, calls = self.fake_campaign(
            guest_extra="(directory/'arm.report').write_text((directory/'arm.report').read_text().replace(token,'f'*32))")
        self.assertEqual((status, len(calls)), (2, 2))
        result = json.loads((root / "phases/guest/evidence/result.json").read_text())
        self.assertEqual((result["completion"], result["semantic"]), ("INCOMPLETE", "UNAVAILABLE"))
        self.assertIn("identity", result["report_error"])

    def test_stale_output_is_preserved_and_blocks_guest(self):
        status, root, calls = self.fake_campaign(stale=True)
        self.assertEqual((status, len(calls)), (2, 1))
        self.assertEqual((root / launch.REPORT_DIRECTORY / "arm.report").read_text(), "stale")

    def test_arm09_cli_requires_independently_supplied_root_plan_hash(self):
        import arm09_execute
        with mock.patch.object(sys, "argv", ["arm09_execute", "--plan", str(self.output / "plan.json"),
                                            "--authorize-pair", "old-authorization"]), \
                mock.patch.object(arm09_execute, "execute_cpu_opt_off") as execution:
            with self.assertRaises(SystemExit) as result:
                arm09_execute.main()
            self.assertEqual(result.exception.code, 2)
            execution.assert_not_called()


if __name__ == "__main__":
    unittest.main()
