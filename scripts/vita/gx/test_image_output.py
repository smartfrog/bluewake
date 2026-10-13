import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import image
import launch


class ImageOutputTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)
        self.fixture = self.root / 'fixture'
        self.fixture.mkdir()
        (self.fixture / 'stream.fifo').write_bytes(b'\0')
        self.metadata = dict(fifo_sha256=hashlib.sha256(b'\0').hexdigest(),
                             mem1_sha256=None, requires_mem1=False, captured_draws=1,
                             coverage='logged-command-prefix-only', source='/captured/ww05.log')
        self.output = self.root / 'images'

    def tearDown(self):
        self.temporary.cleanup()

    def test_fifo_tampering_is_refused_before_emulator_launch(self):
        (self.fixture / 'stream.fifo').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'FIFO SHA-256'):
            image.validate_fixture(self.fixture, self.metadata)

    def test_absent_required_memory_records_a_blocker_without_a_fake_image(self):
        self.metadata['requires_mem1'] = True
        with patch.object(image, 'run_replay') as runner:
            status = image.render_fixture(self.fixture, self.metadata, self.output, self.root)
        self.assertNotEqual(status, 0)
        runner.assert_not_called()
        self.assertFalse((self.output / 'capture.png').exists())
        manifest = json.loads((self.output / 'image.json').read_text())
        self.assertEqual(manifest['verdict'], 'memory-required')

    def test_existing_output_is_not_overwritten(self):
        self.output.mkdir()
        (self.output / 'user-note.txt').write_text('retain')
        with self.assertRaises(FileExistsError):
            image.render_fixture(self.fixture, self.metadata, self.output, self.root)
        self.assertEqual((self.output / 'user-note.txt').read_text(), 'retain')

    def test_readback_failure_is_not_a_successful_image_delivery(self):
        with patch.object(image.extract, 'replay_fixture', return_value=('verdict=ran\n', 0)), \
                patch.object(image, 'run_replay', return_value=dict(ok=False, report='FAIL readback\n',
                                                                  capture=None, evidence={} )):
            status = image.render_fixture(self.fixture, self.metadata, self.output, self.root)
        self.assertNotEqual(status, 0)
        manifest = json.loads((self.output / 'image.json').read_text())
        self.assertEqual(manifest['verdict'], 'image-unavailable')
        self.assertIsNone(manifest['image'])

    def test_capture_profile_enables_actual_surface_writeback(self):
        text = 'backend-renderer: OpenGL\ndisable-surface-sync: true\n'
        self.assertIn('disable-surface-sync: false\n', launch.surface_sync_config(text))

    def test_capture_profile_can_add_the_surface_sync_setting(self):
        text = 'backend-renderer: OpenGL\n'
        self.assertIn('disable-surface-sync: false\n', launch.surface_sync_config(text))

    def test_transparent_readback_is_not_a_visible_image_success(self):
        captured = self.root / 'capture.png'
        captured.write_bytes(b'writer-output')
        result = dict(ok=True, report='capture written 2x1 point=after-gx-present\n',
                      capture=captured, evidence={})
        with patch.object(image.extract, 'replay_fixture', return_value=('verdict=ran\n', 0)), \
                patch.object(image, 'run_replay', return_value=result), \
                patch.object(image, 'png_metadata', return_value=dict(alpha_nonzero=0)):
            status = image.render_fixture(self.fixture, self.metadata, self.output, self.root)
        self.assertEqual(status, 2)
        self.assertEqual(json.loads((self.output / 'image.json').read_text())['verdict'], 'image-readback-blocked')


if __name__ == '__main__':
    unittest.main()
