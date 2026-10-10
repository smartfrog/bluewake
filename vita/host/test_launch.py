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
