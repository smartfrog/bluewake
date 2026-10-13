import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import test_host

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'feasibility/gx'))
from test_png_capture import parse_png
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/vita/armv7'))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/vita/ww05'))


class LiveFramesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, root, scenario):
        result = subprocess.run([str(test_host.OUT / 'gx_drain_probe'), scenario], cwd=root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stderr

    def test_mem1_profile_produces_each_actual_frame_as_raw_and_visible_png_with_temporal_metadata(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            self.probe(root, 'image-two')
            for frame in (1, 2):
                with self.subTest(frame=frame):
                    metadata = json.loads((root / f'frames/frame-{frame}.json').read_text())
                    self.assertEqual(metadata['frame'], frame)
                    self.assertEqual(metadata['phase'], 'after-gx-present-before-next-frame')
                    raw = parse_png(root / f'frames/frame-{frame}-rgba.png')[-1]
                    visible = parse_png(root / f'frames/frame-{frame}.png')[-1]
                    self.assertEqual(raw, bytes([70, 80, 90, 255, 100, 110, 120, 0, 10, 20, 30, 0, 40, 50, 60, 128]))
                    for channel in range(3):
                        self.assertEqual(raw[channel::4], visible[channel::4])
                    self.assertEqual(visible[3::4], bytes([255] * 4))

    def test_no_dump_profile_does_not_read_back_or_create_images(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            self.probe(root, 'image-off')
            self.assertEqual(list(root.iterdir()), [])

    def test_failed_present_or_readback_does_not_manufacture_a_png(self):
        for scenario in ('image-failed-present', 'image-readback-failure', 'image-invalid-readback'):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
                root = Path(directory)
                self.probe(root, scenario)
                self.assertEqual(list((root / 'frames').glob('*')), [])

    def test_capture_profile_enables_surface_writeback_without_changing_the_no_dump_schema(self):
        from fp_profile import CONFIG_TEMPLATE, CONFIG_PATH, CONFIG_SHA256, config_identity
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            path = root / CONFIG_PATH
            path.parent.mkdir(parents=True)
            original = CONFIG_TEMPLATE.read_bytes()
            path.write_bytes(original)
            self.assertEqual(config_identity(root)['sha256'], CONFIG_SHA256)
            path.write_bytes(original.replace(b'disable-surface-sync: true', b'disable-surface-sync: false'))
            identity = config_identity(root, surface_sync=True)
            self.assertTrue(identity['surface_sync'])
            self.assertEqual(path.read_bytes().replace(b'disable-surface-sync: false', b'disable-surface-sync: true'), original)
            self.assertEqual(CONFIG_TEMPLATE.read_bytes(), original)

    def test_existing_frame_evidence_is_preserved_and_prevents_capture_activation(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            (root / 'frames').mkdir()
            evidence = root / 'frames/frame-1.png'
            evidence.write_bytes(b'preserve existing private evidence')
            self.probe(root, 'image-preexisting')
            self.assertEqual(evidence.read_bytes(), b'preserve existing private evidence')

    def test_png_write_failure_stops_after_actual_present_and_preserves_unpublished_partial(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            self.probe(root, 'image-png-write-failure')
            self.assertTrue((root / 'frames/frame-1-rgba.png.partial').exists())
            self.assertFalse((root / 'frames/frame-1.json').exists())

    def test_live_collector_keeps_both_pngs_in_private_evidence_and_records_exact_pixel_statistics(self):
        from frame_capture import collect_frames
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            self.probe(root, 'image-two')
            result = collect_frames(root / 'frames', root / 'evidence-frames', enabled=True, presented=2,
                                    token='c' * 32, source='a' * 40, package='b' * 64)
            self.assertEqual(result['captures'], 2)
            self.assertFalse(result['fps_baseline'])
            self.assertEqual(result['frames'][0]['image']['alpha_nonzero'], 4)
            self.assertEqual(result['frames'][0]['raw_image']['alpha_nonzero'], 2)
            self.assertEqual(result['frames'][0]['image']['distinct_rgba'], 4)
            for name in ('frame-1.png', 'frame-1-rgba.png', 'frame-1.json'):
                self.assertEqual((root / 'frames' / name).read_bytes(), (root / 'evidence-frames' / name).read_bytes())

    def test_installer_user_id_rewrite_does_not_disable_capture_surface_writeback(self):
        from fp_profile import CONFIG_PATH, profile_config, config_identity
        from fp_campaign import emitted_config, restore_config
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            path = root / CONFIG_PATH
            path.parent.mkdir(parents=True)
            phase = root / 'install'
            (phase / 'evidence').mkdir(parents=True)
            canonical = profile_config(surface_sync=True)
            path.write_bytes(canonical.replace(b'user-id: ""\n', b'user-id: 00\n'))
            restored = emitted_config(root, phase, surface_sync=True)
            restore_config(root, restored, surface_sync=True)
            self.assertEqual(path.read_bytes(), canonical)
            self.assertTrue(config_identity(root, surface_sync=True)['surface_sync'])

    def test_unrequested_or_partial_images_cannot_be_promoted_into_verified_evidence(self):
        from frame_capture import collect_frames
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            self.probe(root, 'image-two')
            with self.assertRaises(ValueError):
                collect_frames(root / 'frames', root / 'evidence-off', enabled=False, presented=2,
                               token='c' * 32, source='a' * 40, package='b' * 64)
            (root / 'frames/frame-3.png.partial').write_bytes(b'partial evidence')
            with self.assertRaises(ValueError):
                collect_frames(root / 'frames', root / 'evidence-partial', enabled=True, presented=2,
                               token='c' * 32, source='a' * 40, package='b' * 64)

    def test_empty_sandbox_mounted_evidence_directory_is_used_directly_without_replacing_any_image(self):
        from frame_capture import collect_frames
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            (root / 'frames').mkdir()
            self.probe(root, 'image-two')
            before = (root / 'frames/frame-1.png').read_bytes()
            result = collect_frames(root / 'frames', root / 'frames', enabled=True, presented=2,
                                    token='c' * 32, source='a' * 40, package='b' * 64)
            self.assertEqual(result['captures'], 2)
            self.assertEqual((root / 'frames/frame-1.png').read_bytes(), before)

    def test_strict_frame_images_cannot_be_relabelled_as_a_deferred_audio_run(self):
        from frame_capture import collect_frames
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            self.probe(root, 'image-two')
            with self.assertRaisesRegex(ValueError, 'audio policy'):
                collect_frames(root / 'frames', root / 'frames', enabled=True, presented=2,
                               token='c' * 32, source='a' * 40, package='b' * 64, audio='deferred')

    def test_live_metadata_keeps_actual_renderer_counters_at_each_presentation(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            log = self.probe(root, 'image-two')
            for frame in (1, 2):
                with self.subTest(frame=frame):
                    metadata = json.loads((root / f'frames/frame-{frame}.json').read_text())
                    renderer = metadata['renderer']
                    self.assertEqual(renderer['plans'], frame)
                    self.assertEqual(renderer['programs'], frame)
                    self.assertEqual(renderer['programs_distinct'], 1)
                    self.assertEqual(renderer['display_copies'], frame)
                    for field in ('plans_skipped', 'programs_refused', 'copies_refused'):
                        self.assertEqual(renderer[field], 0)
                    self.assertEqual(renderer['gaps'], 1)
                    self.assertIn(f'[frame-renderer-gap] frame={frame} audio=strict name=unresolved_tex_matrix value={frame}', log)

    def test_live_metadata_exposes_a_real_shader_refusal_without_claiming_an_extra_submitted_draw(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            log = self.probe(root, 'image-refused')
            metadata = json.loads((root / 'frames/frame-2.json').read_text())
            self.assertEqual(metadata['draws'], 1)
            self.assertEqual(metadata['renderer']['plans'], 2)
            self.assertEqual(metadata['renderer']['programs_refused'], 1)
            self.assertIn('category=shader audio=strict reason=indirect TEV lookups not supported', log)

    def test_renderer_refusal_trace_failure_stops_and_preserves_the_actual_published_frame(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            log = self.probe(root, 'image-refusal-trace-failure')
            self.assertIn('refusal trace write failed', log)
            self.assertTrue((root / 'frames/frame-2.png').exists())
            metadata = json.loads((root / 'frames/frame-2.json').read_text())
            self.assertEqual(metadata['renderer']['programs_refused'], 1)

    def test_live_collector_rejects_malformed_present_renderer_counters(self):
        from frame_capture import collect_frames
        for value in (-1, True, 1.5, '0', 1 << 64):
            with self.subTest(value=value), tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
                root = Path(directory)
                self.probe(root, 'image-two')
                path = root / 'frames/frame-2.json'
                metadata = json.loads(path.read_text())
                metadata['renderer']['programs_refused'] = value
                path.write_text(json.dumps(metadata))
                with self.assertRaisesRegex(ValueError, 'renderer'):
                    collect_frames(root / 'frames', root / 'frames', enabled=True, presented=2,
                                   token='c' * 32, source='a' * 40, package='b' * 64)

    def test_live_collector_rejects_partial_renderer_counter_schema(self):
        from frame_capture import collect_frames
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            self.probe(root, 'image-two')
            path = root / 'frames/frame-2.json'
            metadata = json.loads(path.read_text())
            del metadata['renderer']['programs_refused']
            path.write_text(json.dumps(metadata))
            with self.assertRaisesRegex(ValueError, 'renderer'):
                collect_frames(root / 'frames', root / 'frames', enabled=True, presented=2,
                               token='c' * 32, source='a' * 40, package='b' * 64)

    def test_legacy_frame_metadata_without_renderer_counters_remains_unknown_not_zero(self):
        from frame_capture import collect_frames
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-live-') as directory:
            root = Path(directory)
            self.probe(root, 'image-two')
            for frame in (1, 2):
                path = root / f'frames/frame-{frame}.json'
                metadata = json.loads(path.read_text())
                del metadata['renderer']
                path.write_text(json.dumps(metadata))
            result = collect_frames(root / 'frames', root / 'frames', enabled=True, presented=2,
                                    token='c' * 32, source='a' * 40, package='b' * 64)
            for frame in result['frames']:
                self.assertNotIn('renderer', frame)
