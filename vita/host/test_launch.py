import json
from pathlib import Path
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
    def test_sixteen_snapshot_request_is_bound_to_the_original_private_identity(self):
        from launch import fresh_request
        from fp_launch import REPORT_DIRECTORY
        token = fresh_request(self.root, 'a' * 40, 'b' * 64, continue_gx=196608,
                              mem1_snapshots=16, mem1_command_window=[102937, 196608])
        self.assertEqual((self.root / REPORT_DIRECTORY / 'ww05.mem1').read_text(),
                         f'{token} {"a" * 40} {"b" * 64} 16\n')

    def test_scene_window_request_binds_both_endpoints_to_the_same_private_identity(self):
        from launch import fresh_request
        from fp_launch import REPORT_DIRECTORY
        token = fresh_request(self.root, 'a' * 40, 'b' * 64, continue_gx=133260,
                               mem1_snapshots=8, mem1_command_window=[101854, 102936])
        for suffix, command in (('start', 101854), ('end', 102936)):
            with self.subTest(suffix=suffix):
                self.assertEqual((self.root / REPORT_DIRECTORY / ('ww05.mem1-' + suffix)).read_text(),
                                 f'{token} {"a" * 40} {"b" * 64} {command}\n')

    def test_scene_window_outside_the_journal_is_rejected_before_any_request_is_written(self):
        from launch import fresh_request
        with self.assertRaisesRegex(ValueError, 'command window'):
            fresh_request(self.root, 'a' * 40, 'b' * 64, continue_gx=2048,
                          mem1_snapshots=8, mem1_command_window=[9976, 11004])
        self.assertFalse(list(self.root.rglob('ww05.request')))

    def test_deferred_audio_request_is_bound_to_the_new_run_identity_and_never_reuses_stale_mode_files(self):
        from launch import fresh_request
        from fp_launch import REPORT_DIRECTORY
        token = fresh_request(self.root, 'a' * 40, 'b' * 64, continue_gx=2048, defer_audio=True)
        request = self.root / REPORT_DIRECTORY / 'ww05.audio'
        self.assertEqual(request.read_text(), f'{token} {"a" * 40} {"b" * 64} 1\n')
        with self.assertRaises(ValueError):
            fresh_request(self.root, 'a' * 40, 'b' * 64, continue_gx=2048, defer_audio=True)
        self.assertEqual(request.read_text(), f'{token} {"a" * 40} {"b" * 64} 1\n')

    def setUp(self):
        from fp_launch import create_run
        from fp_profile import CONFIG_PATH, FIRMWARE_DIRECTORY, config_identity, profile_config
        from stage_plan import make_plan, sha256

        temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww05-launch-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name) / 'run'
        create_run(self.root)
        (self.root / CONFIG_PATH).parent.mkdir()
        (self.root / CONFIG_PATH).write_bytes(profile_config(pref_path=self.root / FIRMWARE_DIRECTORY))
        package = self.root / 'runtime/ww05.vpk'
        with zipfile.ZipFile(package, 'w') as archive:
            for name in ('eboot.bin', 'sce_sys/param.sfo', 'main.dol'):
                archive.writestr(name, b'synthetic ' + name.encode())
        args = SimpleNamespace(timeout=2, libraries=None, wayland_socket=None)
        self.plan = make_plan(self.root, args, 'a' * 40, sha256(package), 'c' * 32)
        self.plan['private_config'] = config_identity(self.root, pref_path=self.root / FIRMWARE_DIRECTORY)
        for entry in self.plan['phases'].values():
            phase = Path(entry['evidence_root'])
            (phase / 'evidence').mkdir(parents=True)
            entry['command'][0] = str(self.root / 'runtime/Fake')

    def fake(self, installer, guest='sys.exit(0)'):
        fake = self.root / 'runtime/Fake'
        fake.write_text('#!/usr/bin/python3\nimport sys,time,zipfile\nfrom pathlib import Path\n'
                        f"root=Path({str(self.root)!r})\nif '--console' in sys.argv:\n" +
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

    def test_insufficient_ram_withholds_install_before_any_process_starts(self):
        import launch
        minimum = 5 * 1024 ** 3 // 2
        self.plan['minimum_available_memory_bytes'] = minimum
        with patch('launch.available_memory_bytes', return_value=minimum - 1024), \
             patch('launch.run_owned', side_effect=AssertionError('RAM pause must not spawn')):
            status, result = self.run_fake()
        self.assertEqual(status, 2)
        self.assertIn('available user RAM', result['error'])
        self.assertFalse((self.root / 'phases/install/evidence/process.json').exists())
        self.assertFalse(list(self.root.rglob('ww05.request')))

    def test_guest_ram_gate_is_rechecked_after_the_installer_is_reaped(self):
        import launch
        minimum = 5 * 1024 ** 3 // 2
        self.successful_fake()
        self.plan['minimum_available_memory_bytes'] = minimum
        with patch('launch.available_memory_bytes', side_effect=(minimum, minimum - 1024)), \
             patch('activity.available_memory_bytes', return_value=minimum):
            status, result = self.run_fake()
        self.assertEqual(status, 2)
        self.assertIn('available user RAM', result['error'])
        self.assertTrue(result['install_process']['waited'])
        self.assertFalse((self.root / 'phases/guest/evidence/process.json').exists())
        self.assertFalse(list(self.root.rglob('ww05.request')))

    def test_live_ram_pressure_reaps_owned_guest_and_preserves_incomplete_disposition(self):
        import launch
        minimum = 5 * 1024 ** 3 // 2
        self.plan['minimum_available_memory_bytes'] = minimum
        self.fake("app=root/'data/Vita3K/Vita3K/ux0/app/BWWG00005'\napp.mkdir(parents=True)\n"
                  "with zipfile.ZipFile('ww05.vpk') as archive: archive.extractall(app)\n",
                  "print('synthetic RAM-guard fixture', flush=True)\ntime.sleep(10)")
        def available():
            return minimum - 1024 if list(self.root.rglob('ww05.request')) else minimum
        with patch('launch.available_memory_bytes', return_value=minimum), \
             patch('activity.available_memory_bytes', side_effect=available):
            status, result = self.run_fake()
        self.assertEqual(status, 125)
        self.assertEqual(result['process']['stop_reason'], 'memory-pressure')
        self.assertTrue(result['process']['waited'])
        self.assertEqual(result['completion'], 'INCOMPLETE')
        self.assertEqual(result['reported_frontier'], 'UNAVAILABLE')

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

    def test_measured_guest_disposition_survives_interruption_during_post_run_validation(self):
        import launch
        self.successful_fake('time.sleep(10)')
        self.plan['phases']['guest']['timeout_seconds'] = 0.2
        with patch.object(launch, 'verify_inputs', side_effect=[None, None, KeyboardInterrupt]):
            with self.assertRaises(KeyboardInterrupt):
                launch.execute_run(self.root, self.plan)
        disposition = json.loads((self.root / 'evidence/guest-disposition.json').read_text())
        self.assertEqual(disposition['process']['stop_reason'], 'timeout')
        self.assertEqual(disposition['process']['exit'], -15)
        self.assertTrue(disposition['process']['waited'])
        self.assertEqual(disposition['process_status'], 124)
        self.assertEqual(disposition['validation'], 'PENDING')
        self.assertEqual(disposition['audio'], 'strict')
        self.assertFalse(disposition['frontier_verified'])
        self.assertFalse((self.root / 'evidence/result.json').exists())

    def test_pre_validation_guest_disposition_retains_the_explicit_deferred_audio_identity(self):
        self.successful_fake()
        self.plan.update(continue_gx=64, defer_audio=True)
        self.run_fake()
        disposition = json.loads((self.root / 'evidence/guest-disposition.json').read_text())
        self.assertEqual(disposition['audio'], 'deferred')
        self.assertEqual(disposition['source'], self.plan['source'])
        self.assertEqual(disposition['vpk_sha256'], self.plan['vpk_sha256'])
        self.assertEqual(disposition['reported_frontier'], 'UNAVAILABLE')

    def test_busy_native_guest_is_not_mistaken_for_an_idle_process(self):
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
        self.assertFalse((self.root / REPORT_DIRECTORY / 'ww05.mem1').exists())

    def test_native_subprocess_does_not_inherit_forced_software_or_player_home(self):
        import os
        self.successful_fake("import os\n"
                             "assert 'LIBGL_ALWAYS_SOFTWARE' not in os.environ\n"
                             "assert os.environ['HOME']==str(root/'home')\n"
                             "assert os.environ['XDG_DATA_HOME']==str(root/'data')\n")
        with patch.dict(os.environ, {'LIBGL_ALWAYS_SOFTWARE': '1', 'HOME': '/player'}):
            status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertEqual(result['renderer'], {'mode': 'native-default', 'observation': 'UNOBSERVED'})

    def test_native_frames_link_stays_within_the_private_run_evidence(self):
        from launch import fresh_request
        from fp_launch import REPORT_DIRECTORY
        fresh_request(self.root, 'a' * 40, 'b' * 64, continue_gx=2048, mem1_snapshots=2)
        frames = self.root / REPORT_DIRECTORY / 'frames'
        self.assertEqual(frames.readlink(), self.root / 'evidence/frames')
        (frames / 'synthetic-frame').write_bytes(b'synthetic frame')
        self.assertEqual((self.root / 'evidence/frames/synthetic-frame').read_bytes(), b'synthetic frame')

    def test_memory_capture_request_is_bound_to_the_same_fresh_guest_identity(self):
        import launch
        from fp_launch import REPORT_DIRECTORY
        token = launch.fresh_request(self.root, 'a' * 40, 'b' * 64, 2048, 2)
        directory = self.root / REPORT_DIRECTORY
        self.assertEqual((directory / 'ww05.mem1').read_text(), f'{token} {"a" * 40} {"b" * 64} 2\n')
        self.assertTrue((self.root / 'evidence/frames').is_dir())
        self.assertTrue((directory / 'frames').is_dir())

    def test_preexisting_memory_capture_is_preserved_before_creating_any_new_request(self):
        import launch
        from fp_launch import REPORT_DIRECTORY
        directory = self.root / REPORT_DIRECTORY
        directory.mkdir(parents=True)
        snapshot = directory / 'ww05.mem1-1.bin'
        snapshot.write_bytes(b'preserve private evidence')
        with self.assertRaisesRegex(ValueError, 'preexisting WW05'):
            launch.fresh_request(self.root, 'a' * 40, 'b' * 64, 2048, 2)
        self.assertEqual(snapshot.read_bytes(), b'preserve private evidence')
        self.assertFalse((directory / 'ww05.request').exists())

    def test_optional_disc_link_reads_the_verified_image_without_copying_or_modifying_it(self):
        from stage_plan import stage_disc, sha256
        image = self.root.parent / 'fixture.iso'
        image.write_bytes(b'private disc fixture')
        disc = stage_disc(self.root, image, sha256(image))
        destination = self.root / disc['install_path']
        self.successful_fake("disc=root/'data/Vita3K/Vita3K/ux0/data/bluewake/GZLE01.iso'\n"
                              "assert disc.read_bytes()==b'private disc fixture'")
        before = sha256(image)
        status, result = self.run_fake()
        self.assertEqual(status, 0)
        self.assertEqual(sha256(image), before)
        self.assertTrue(destination.is_symlink())
        self.assertEqual(destination.readlink(), image)

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
