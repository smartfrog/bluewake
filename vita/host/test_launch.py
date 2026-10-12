import json
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))
sys.path.insert(0, str(ROOT / 'scripts/vita/armv7'))


class PrivateLaunchTest(unittest.TestCase):
    def setUp(self):
        from fp_launch import create_run, sandbox_command
        from fp_profile import CONFIG_PATH, CONFIG_TEMPLATE, config_identity
        from stage_plan import make_plan, sha256

        temporary = tempfile.TemporaryDirectory(dir='/home/fred/projects/zeldaWWHDRecomp/build-f5', prefix='ww05-launch-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name) / 'run'
        create_run(self.root)
        (self.root / CONFIG_PATH).parent.mkdir()
        shutil.copyfile(CONFIG_TEMPLATE, self.root / CONFIG_PATH)
        package = self.root / 'runtime/ww05.vpk'
        with zipfile.ZipFile(package, 'w') as archive:
            for name in ('eboot.bin', 'sce_sys/param.sfo', 'main.dol'):
                archive.writestr(name, b'synthetic ' + name.encode())
        args = SimpleNamespace(timeout=2, libraries=None, wayland_socket=None)
        self.plan = make_plan(self.root, args, 'a' * 40, sha256(package), 'c' * 32)
        self.plan['private_config'] = config_identity(self.root)
        for entry in self.plan['phases'].values():
            phase = Path(entry['evidence_root'])
            (phase / 'evidence').mkdir(parents=True)
            entry['command'] = sandbox_command(self.root, 'Fake', entry['command'][entry['command'].index('/stack/vita3k/Vita3K') + 1:])

    def fake(self, installer, guest='sys.exit(0)'):
        fake = self.root / 'runtime/Fake'
        fake.write_text('#!/usr/bin/python3\nimport sys,time,zipfile\nfrom pathlib import Path\n'
                        "root=Path('/run/ww03')\nif '--console' in sys.argv:\n" +
                        ''.join(' ' + line + '\n' for line in installer.splitlines()) +
                        'else:\n' + ''.join(' ' + line + '\n' for line in guest.splitlines()))
        fake.chmod(0o700)

    def run_fake(self):
        import launch
        with patch.object(launch, 'verify_inputs'):
            status = launch.execute_run(self.root, self.plan)
        return status, json.loads((self.root / 'evidence/result.json').read_text())

    def test_failed_installation_is_recorded_and_never_starts_guest(self):
        self.fake('print("synthetic installer failure")\nsys.exit(7)')
        status, result = self.run_fake()
        self.assertNotEqual(status, 0)
        self.assertIn('installer disposition', result['error'])
        self.assertTrue((self.root / 'phases/install/evidence/process.json').is_file())
        self.assertFalse((self.root / 'phases/guest/evidence/process.json').exists())
        self.assertFalse(list(self.root.rglob('ww05.request')))

    def test_idle_guest_is_reaped_early_with_partial_log_evidence(self):
        self.fake("app=root/'data/Vita3K/Vita3K/ux0/app/BWWG00005'\napp.mkdir(parents=True)\n"
                  "with zipfile.ZipFile('ww05.vpk') as archive: archive.extractall(app)\n",
                  "print('partial guest log', flush=True)\ntime.sleep(10)")
        self.plan['idle_timeout_seconds'] = 0.3
        self.plan['phases']['guest']['timeout_seconds'] = 5
        status, result = self.run_fake()
        self.assertEqual(status, 125)
        self.assertEqual(result['process']['stop_reason'], 'inactive')
        self.assertLess(result['process']['duration_seconds'], 3)
        self.assertEqual(result['completion'], 'INCOMPLETE')
        self.assertIn('partial guest log',
                      (self.root / 'phases/guest/evidence/process.log').read_text())
        self.assertFalse(any(Path('/proc', str(pid)).exists()
                             for pid in result['process']['monitored_pids']))

    def test_low_cpu_guest_with_growing_process_log_is_not_declared_inactive(self):
        self.successful_fake("for index in range(30):\n print('guest progress', flush=True)\n time.sleep(.05)")
        self.plan['idle_timeout_seconds'] = 0.2
        status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertEqual(result['process']['stop_reason'], 'exit')

    def test_low_cpu_guest_with_growing_native_journal_is_not_declared_inactive(self):
        self.successful_fake("journal=root/'data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log'\n"
                             "for index in range(30):\n"
                             " with journal.open('a') as stream: stream.write('guest progress\\n')\n"
                             " time.sleep(.05)")
        self.plan['idle_timeout_seconds'] = 0.2
        status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertEqual(result['process']['stop_reason'], 'exit')

    def test_growing_logs_do_not_hide_the_separate_explicit_phase_deadline(self):
        self.successful_fake("for index in range(30):\n print('guest progress', flush=True)\n time.sleep(.05)")
        self.plan['idle_timeout_seconds'] = 0.2
        self.plan['phases']['guest']['timeout_seconds'] = 0.6
        status, result = self.run_fake()
        self.assertEqual(status, 124)
        self.assertEqual(result['process']['stop_reason'], 'timeout')
        self.assertEqual(result['completion'], 'INCOMPLETE')

    def test_timeout_without_a_closed_report_keeps_deadline_status_and_partial_logs(self):
        self.fake("app=root/'data/Vita3K/Vita3K/ux0/app/BWWG00005'\napp.mkdir(parents=True)\n"
                  "with zipfile.ZipFile('ww05.vpk') as archive: archive.extractall(app)\n",
                  "print('partial timed guest log', flush=True)\ntime.sleep(10)")
        self.plan['phases']['guest']['timeout_seconds'] = 0.2
        status, result = self.run_fake()
        self.assertEqual(status, 124)
        self.assertEqual(result['completion'], 'INCOMPLETE')
        self.assertEqual(result['reported_frontier'], 'UNAVAILABLE')
        self.assertIn('partial timed guest log', (self.root / 'phases/guest/evidence/process.log').read_text())

    def test_missing_verified_input_after_a_timeout_is_not_hidden_as_report_absence(self):
        import launch
        self.successful_fake('time.sleep(10)')
        self.plan['phases']['guest']['timeout_seconds'] = 0.2
        with patch.object(launch, 'verify_inputs', side_effect=[None, None, FileNotFoundError(2, 'missing input', 'main.dol')]):
            status = launch.execute_run(self.root, self.plan)
        self.assertEqual(status, 2)
        result = json.loads((self.root / 'evidence/result.json').read_text())
        self.assertIn('main.dol', result['error'])

    def test_busy_sandbox_descendant_is_not_mistaken_for_an_idle_bwrap_parent(self):
        self.successful_fake('end=time.monotonic()+1\nwhile time.monotonic()<end: pass')
        self.plan['idle_timeout_seconds'] = 0.3
        self.plan['phases']['guest']['timeout_seconds'] = 5
        status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertEqual(result['process']['stop_reason'], 'exit')

    def test_installer_stops_as_soon_as_its_exact_payload_is_complete(self):
        self.successful_fake()
        fake = self.root / 'runtime/Fake'
        fake.write_text(fake.read_text().replace("else:\n", " time.sleep(10)\nelse:\n"))
        self.plan['phases']['install']['timeout_seconds'] = 5
        status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertEqual(result['install_process']['stop_reason'], 'installed_payload')
        self.assertLess(result['install_process']['duration_seconds'], 3)

    def successful_fake(self, ending=''):
        from test_report import ReportTest
        oracle = ReportTest()
        oracle.setUp()
        body = oracle.content.split('\n', 1)[1]
        self.fake("app=root/'data/Vita3K/Vita3K/ux0/app/BWWG00005'\napp.mkdir(parents=True)\n"
                  "assert not list(root.rglob('ww05.request'))\n"
                  "with zipfile.ZipFile('ww05.vpk') as archive: archive.extractall(app)\n",
                  "assert sys.argv[1:]==['--backend-renderer','OpenGL','--installed-path','BWWG00005']\n"
                  "directory=root/'data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture'\n"
                  "token,source,package=(directory/'ww05.request').read_text().split()\n"
                  "header=f'WW05 {token} {source} {package} method=startup-gx-v1 phase=guest platform=VITA\\n'\n"
                  f"(directory/'ww05.report').write_text(header+{body!r})\n" + ending)

    def test_verified_installation_precedes_fresh_guest_request(self):
        self.successful_fake()
        status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertEqual(result['completion'], 'COMPLETE')
        self.assertTrue(result['frontier_verified'])
        self.assertFalse(result['rendered'])

    def test_progress_log_larger_than_the_fp_limit_can_certify_a_closed_guest_report(self):
        self.successful_fake("print('x' * (1024 * 1024 + 1))")
        status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertTrue(result['frontier_verified'])

    def test_configuration_parse_failure_after_a_large_trace_is_still_refused(self):
        self.successful_fake("print('x' * (1024 * 1024 + 1))\n"
                             "print(\"Config file can't be loaded: synthetic failure\")")
        status, result = self.run_fake()
        self.assertEqual(status, 2)
        self.assertIn('configuration parse failure', result['error'])

    def test_trace_log_limit_remains_enforced(self):
        import launch
        self.successful_fake("print('x' * 1024)")
        with patch.object(launch, 'PHASE_LOG_LIMIT', 512):
            status, result = self.run_fake()
        self.assertEqual(status, 2)
        self.assertIn('bounded regular file', result['error'])

    def test_large_runtime_cache_trace_can_certify_a_closed_guest_report(self):
        self.successful_fake("cache=root/'cache/Vita3K'\ncache.mkdir(exist_ok=True)\n"
                             "(cache/'vita3k.log').write_text('x' * (1024 * 1024 + 1))")
        status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertTrue(result['frontier_verified'])

    def test_configuration_error_at_the_end_of_a_large_cache_trace_is_refused(self):
        self.successful_fake("cache=root/'cache/Vita3K'\ncache.mkdir(exist_ok=True)\n"
                             "(cache/'vita3k.log').write_text('x' * (1024 * 1024 + 1) + "
                             "\"Config file can't be loaded: synthetic failure\")")
        status, result = self.run_fake()
        self.assertEqual(status, 2)
        self.assertIn('configuration parse failure', result['error'])

    def test_runtime_cache_trace_remains_bounded(self):
        import launch
        self.successful_fake("cache=root/'cache/Vita3K'\ncache.mkdir(exist_ok=True)\n"
                             "(cache/'vita3k.log').write_text('x' * 1024)")
        with patch.object(launch, 'PHASE_LOG_LIMIT', 512):
            status, result = self.run_fake()
        self.assertEqual(status, 2)
        self.assertIn('bounded regular file', result['error'])

    def test_presented_frames_are_recorded_without_claiming_a_visible_scene(self):
        self.successful_fake()
        fake = self.root / 'runtime/Fake'
        fake.write_text(fake.read_text().replace('display_copies=0 presented=0', 'display_copies=2 presented=2')
                        .replace('rendered=0', 'rendered=2'))
        status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertTrue(result['rendered'])
        self.assertEqual(result['presented'], 2)

    def test_continuation_request_and_closed_journal_certify_frontier_despite_later_guest_stop(self):
        from test_report import ReportTest
        oracle = ReportTest()
        oracle.setUp()
        body = oracle.continuation_report().split('\n', 1)[1]
        self.plan['continue_gx'] = 64
        self.fake("app=root/'data/Vita3K/Vita3K/ux0/app/BWWG00005'\napp.mkdir(parents=True)\n"
                  "with zipfile.ZipFile('ww05.vpk') as archive: archive.extractall(app)\n",
                  "directory=root/'data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture'\n"
                  "token,source,package=(directory/'ww05.request').read_text().split()\n"
                  "assert (directory/'ww05.continue').read_text()==f'{token} {source} {package} 64\\n'\n"
                  "header=f'WW05 {token} {source} {package} method=startup-gx-v1 phase=guest platform=VITA\\n'\n"
                  f"(directory/'ww05.report').write_text(header+{body!r})\n"
                  "(directory/'ww05.log').write_text('[gx-command] index=1 opcode=61 bytes=5 pc=80001000 "
                  "completion_pc=80001004 sp=817FFF00 hex=61FEFFFFFF\\n')\nsys.exit(1)")
        status, result = self.run_fake()
        self.assertEqual(status, 1)
        self.assertTrue(result['frontier_verified'])
        self.assertEqual(result['commands_verified'], 1)
        self.assertEqual(result['completion'], 'COMPLETE')

    def test_stale_opt_in_is_preserved_and_prevents_a_fresh_request(self):
        import launch
        from fp_launch import REPORT_DIRECTORY
        directory = self.root / REPORT_DIRECTORY
        directory.mkdir(parents=True)
        (directory / 'ww05.continue').write_text('preserve this opt-in')
        with self.assertRaisesRegex(ValueError, 'preexisting WW05'):
            launch.fresh_request(self.root, 'a' * 40, 'b' * 64, 64)
        self.assertFalse((directory / 'ww05.request').exists())
        self.assertEqual((directory / 'ww05.continue').read_text(), 'preserve this opt-in')

    def test_default_mode_never_creates_a_continuation_request(self):
        import launch
        from fp_launch import REPORT_DIRECTORY
        launch.fresh_request(self.root, 'a' * 40, 'b' * 64)
        self.assertFalse((self.root / REPORT_DIRECTORY / 'ww05.continue').exists())

    def test_optional_disc_is_read_only_inside_the_guest_sandbox_and_never_modified(self):
        from stage_plan import make_plan, sha256
        image = self.root.parent / 'fixture.iso'
        image.write_bytes(b'private disc fixture')
        destination = self.root / 'data/Vita3K/Vita3K/ux0/data/bluewake/GZLE01.iso'
        destination.parent.mkdir(parents=True)
        destination.touch()
        args = SimpleNamespace(timeout=2, libraries=None, wayland_socket=None, disc=image)
        command = make_plan(self.root, args, 'a' * 40, 'b' * 64, 'c' * 32)['phases']['guest']['command']
        command[command.index('/stack/vita3k/Vita3K')] = '/stack/vita3k/Fake'
        self.plan['phases']['guest']['command'] = command
        self.successful_fake("disc=root/'data/Vita3K/Vita3K/ux0/data/bluewake/GZLE01.iso'\n"
                             "assert disc.read_bytes()==b'private disc fixture'\n"
                             "try: disc.write_bytes(b'forbidden')\n"
                             "except OSError as error: assert error.errno==30\n"
                             "else: raise AssertionError('disc must be read only')")
        before = sha256(image)
        status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertEqual(sha256(image), before)
        self.assertEqual(destination.read_bytes(), b'')

    def test_installed_dol_mismatch_withholds_guest(self):
        self.fake("app=root/'data/Vita3K/Vita3K/ux0/app/BWWG00005'\napp.mkdir(parents=True)\n"
                  "with zipfile.ZipFile('ww05.vpk') as archive: archive.extractall(app)\n"
                  "(app/'main.dol').write_bytes(b'changed DOL')\n")
        status, result = self.run_fake()
        self.assertNotEqual(status, 0)
        self.assertIn('payload mismatch: main.dol', result['error'])
        self.assertFalse((self.root / 'phases/guest/evidence/process.json').exists())

    def test_lingering_frontier_keeps_process_incomplete(self):
        self.successful_fake('time.sleep(10)')
        self.plan['phases']['guest']['timeout_seconds'] = 1
        status, result = self.run_fake()
        self.assertEqual(status, 124)
        self.assertTrue(result['frontier_verified'])
        self.assertEqual(result['completion'], 'INCOMPLETE')
        self.assertEqual(result['process']['stop_reason'], 'timeout')

    def test_preexisting_request_is_preserved(self):
        import launch
        from fp_launch import REPORT_DIRECTORY
        directory = self.root / REPORT_DIRECTORY
        directory.mkdir(parents=True)
        request = directory / 'ww05.request'
        request.write_text('preserve me')
        with self.assertRaisesRegex(ValueError, 'preexisting WW05'):
            launch.fresh_request(self.root, 'a' * 40, 'b' * 64)
        self.assertEqual(request.read_text(), 'preserve me')

    def test_preparation_exception_is_retained_before_spawn(self):
        import launch
        with patch.object(launch, 'verify_inputs', side_effect=RuntimeError('synthetic source read failure')):
            with patch.object(launch, 'run_owned', side_effect=AssertionError('must not spawn')):
                status = launch.execute_run(self.root, self.plan)
        self.assertNotEqual(status, 0)
        result = json.loads((self.root / 'evidence/result.json').read_text())
        self.assertIn('synthetic source read failure', result['error'])
        self.assertIn('RuntimeError', result['traceback'])

    def test_baseline_does_not_allow_the_current_launcher_checkpoint_to_change(self):
        import launch
        self.plan['launcher_source'] = 'b' * 40
        with patch('subprocess.check_output', return_value='c' * 40 + '\n'):
            with self.assertRaisesRegex(ValueError, 'source checkpoint'):
                launch.verify_inputs(self.root, self.plan)

    def test_selected_baseline_receipt_must_remain_byte_identical(self):
        import launch
        path = self.root / 'evidence/snapshot-receipt.json'
        path.write_text('changed receipt')
        self.plan.update(launcher_source='b' * 40, build_evidence_path=str(path), build_evidence_sha256='f' * 64)
        with patch('subprocess.check_output', side_effect=['b' * 40 + '\n', '']), \
             patch.object(launch, 'checked_sources'):
            with self.assertRaisesRegex(ValueError, 'build receipt changed'):
                launch.verify_inputs(self.root, self.plan)

    def test_request_parent_symlink_cannot_create_outside_profile(self):
        import launch

        outside = self.root.parent / 'outside'
        outside.mkdir()
        (self.root / 'data/Vita3K/Vita3K/ux0').mkdir(parents=True)
        (self.root / 'data/Vita3K/Vita3K/ux0/data').symlink_to(outside, target_is_directory=True)
        with self.assertRaises(OSError):
            launch.fresh_request(self.root, 'a' * 40, 'b' * 64)
        self.assertEqual(list(outside.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
