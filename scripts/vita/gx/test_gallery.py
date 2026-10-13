import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

import gallery
from test_capture import command


class GalleryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp/opencode')
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.output = self.root / 'images'
        self.build = self.root / 'build'
        self.run = self.source / ('vita3k-run-' + 'a' * 32)
        self.data = self.run / 'data/ux0/fixture'
        self.data.mkdir(parents=True)
        self.journal = self.data / 'ww05.log'
        self.report = self.data / 'ww05.report'
        self.snapshot = self.data / 'ww05.mem1.json'
        self.payload = self.data / 'mem1.bin'
        self.write_run(1)

    def tearDown(self):
        self.temporary.cleanup()

    def write_run(self, draws):
        commands = [bytes.fromhex('800000')] * draws
        self.journal.write_text('\n'.join(command(n, data[0], data) for n, data in enumerate(commands, 1)))
        self.report.write_text(f'WW05 token {"b" * 40} vpk\nGX_STREAM decoded={draws} logged={draws} pending=0\nRENDER draws=9\n')
        payload = b'captured-memory'
        self.payload.write_bytes(payload)
        manifest = dict(format='gx-mem1-snapshot-v1', binary=self.payload.name,
                        binary_sha256=hashlib.sha256(payload).hexdigest(),
                        fifo_sha256=hashlib.sha256(b''.join(commands)).hexdigest(),
                        regions=[dict(address=0, offset=0, size=len(payload))])
        self.snapshot.write_text(json.dumps(manifest))
        return commands, manifest

    def fake_render(self, fixture, metadata, output, build):
        output.mkdir(parents=True)
        image = dict(width=2, height=1, sha256='c' * 64, distinct_rgba=1, alpha_nonzero=2)
        (output / 'capture.png').write_bytes(b'private-render-output')
        (output / 'report.txt').write_text(f'draws={metadata["captured_draws"]} programs=1\n')
        (output / 'image.json').write_text(json.dumps(dict(verdict='image-captured', image=image,
                                                         capture=metadata, parity_verified=False)))
        return 0

    def scan(self):
        return list(gallery.scan_once(self.source, self.output, self.build, recent_hours=24,
                                      code_revision='d' * 40))

    def test_discovers_standard_manifest_in_recent_run(self):
        self.assertEqual(gallery.discover(self.source, 24, time.time()), [self.report])

    def test_excludes_synthetic_contract_directory(self):
        other = self.source / 'logs/ww16-contract'
        other.mkdir(parents=True)
        (other / 'ww05.report').write_text('report')
        (other / 'ww05.mem1.json').write_text('{}')
        self.assertEqual(gallery.discover(self.source, 24, time.time()), [self.report])

    def test_matches_digest_extent_not_producer_draw_number(self):
        commands, manifest = self.write_run(2)
        manifest['producer'] = dict(command=1, source='b' * 40)
        self.snapshot.write_text(json.dumps(manifest))
        candidate = gallery.inspect_run(self.report, 'd' * 40)
        self.assertEqual(candidate['draws'], 2)
        self.assertIsNone(candidate['through_command'])

    def test_selects_shorter_bound_prefix_without_rebinding_hash(self):
        commands, manifest = self.write_run(2)
        manifest['fifo_sha256'] = hashlib.sha256(commands[0]).hexdigest()
        self.snapshot.write_text(json.dumps(manifest))
        candidate = gallery.inspect_run(self.report, 'd' * 40)
        self.assertEqual((candidate['draws'], candidate['through_command']), (1, 1))

    def test_command_threshold_uses_the_bound_draw_not_report_totals(self):
        self.write_run(2)
        with patch.object(gallery.image, 'render_fixture') as renderer:
            events = list(gallery.scan_once(self.source, self.output, self.build,
                                           code_revision='d' * 40, after_command=2))
        renderer.assert_not_called()
        self.assertEqual(events, [])

    def test_later_bound_draw_is_replayed_without_rebinding(self):
        self.write_run(3)
        with patch.object(gallery.image, 'render_fixture', side_effect=self.fake_render):
            events = list(gallery.scan_once(self.source, self.output, self.build,
                                           code_revision='d' * 40, after_command=2))
        self.assertEqual(events[0]['last_draw_command'], 3)
        self.assertEqual(events[0]['draws'], 3)

    def test_numbered_snapshot_fallback_prefers_richer_bound_prefix(self):
        commands, manifest = self.write_run(2)
        self.snapshot.unlink()
        first = dict(manifest, fifo_sha256=hashlib.sha256(commands[0]).hexdigest())
        (self.data / 'ww05.mem1-1.snapshot.json').write_text(json.dumps(first))
        (self.data / 'ww05.mem1-2.snapshot.json').write_text(json.dumps(manifest))
        candidate = gallery.inspect_run(self.report, 'd' * 40)
        self.assertEqual(candidate['snapshot'].name, 'ww05.mem1-2.snapshot.json')

    def test_keeps_lane16_source_files_byte_identical(self):
        before = {path: path.read_bytes() for path in self.data.iterdir()}
        with patch.object(gallery.image, 'render_fixture', side_effect=self.fake_render):
            events = self.scan()
        self.assertEqual(events[0]['event'], 'gallery-added')
        self.assertEqual(before, {path: path.read_bytes() for path in self.data.iterdir()})

    def test_deduplicates_completed_input_after_restart(self):
        with patch.object(gallery.image, 'render_fixture', side_effect=self.fake_render) as renderer:
            self.scan()
            self.assertEqual(self.scan(), [])
        self.assertEqual(renderer.call_count, 1)

    def test_richer_dump_preserves_both_gallery_versions(self):
        with patch.object(gallery.image, 'render_fixture', side_effect=self.fake_render):
            first = self.scan()[0]
            self.write_run(2)
            second = self.scan()[0]
        self.assertNotEqual(first['image_path'], second['image_path'])
        self.assertTrue((self.output / first['image_path']).is_file())
        self.assertTrue((self.output / second['image_path']).is_file())
        self.assertEqual((first['draws'], second['draws']), (1, 2))

    def test_index_does_not_confuse_capture_lane_totals_with_replay(self):
        with patch.object(gallery.image, 'render_fixture', side_effect=self.fake_render):
            event = self.scan()[0]
        self.assertEqual((event['draws'], event['rendered'], event['lane16_draws']), (1, 1, 9))
        index = (self.output / 'INDEX.md').read_text()
        self.assertIn('UTC', index)
        self.assertIn('2×1', index)
        self.assertIn('c' * 64, index)
        self.assertIn('![image-captured]', index)

    def test_partial_memory_delivery_defers_without_rendering(self):
        self.payload.write_bytes(b'partial')
        with patch.object(gallery.image, 'render_fixture') as renderer:
            events = self.scan()
            self.assertEqual(self.scan(), [])
        renderer.assert_not_called()
        self.assertEqual(events[0]['event'], 'gallery-deferred')

    def test_partial_delivery_is_retried_when_complete(self):
        self.payload.write_bytes(b'partial')
        with patch.object(gallery.image, 'render_fixture', side_effect=self.fake_render):
            self.scan()
            self.payload.write_bytes(b'captured-memory')
            events = self.scan()
        self.assertEqual(events[0]['event'], 'gallery-added')

    def test_nonuniform_valid_image_signals_immediately(self):
        def render(*arguments):
            self.fake_render(*arguments)
            path = arguments[2] / 'image.json'
            manifest = json.loads(path.read_text())
            manifest['image']['distinct_rgba'] = 4
            path.write_text(json.dumps(manifest))
            return 0
        with patch.object(gallery.image, 'render_fixture', side_effect=render):
            event = self.scan()[0]
        self.assertTrue(event['nonblack'])
        self.assertEqual(event['event'], 'gallery-nonblack')

    def test_known_logo_is_archived_without_a_new_content_alert(self):
        comparison = dict(classification='logo-compatible', nonblack_RGB_pixels=1)
        with patch.object(gallery.image, 'render_fixture', side_effect=self.fake_render), \
                patch.object(gallery.novelty, 'classify_frame', return_value=comparison):
            event = list(gallery.scan_once(self.source, self.output, self.build,
                                          code_revision='d' * 40, known_logo={'sha256': 'e' * 64}))[0]
        self.assertEqual(event['event'], 'gallery-known-logo')
        self.assertFalse(event['new_visual_content'])
        self.assertTrue((self.output / event['image_path']).is_file())

    def test_new_visual_candidate_is_reported_without_claiming_a_title_screen(self):
        comparison = dict(classification='new-content-candidate', nonblack_RGB_pixels=1)
        with patch.object(gallery.image, 'render_fixture', side_effect=self.fake_render), \
                patch.object(gallery.novelty, 'classify_frame', return_value=comparison):
            event = list(gallery.scan_once(self.source, self.output, self.build,
                                          code_revision='d' * 40, known_logo={'sha256': 'e' * 64}))[0]
        self.assertEqual(event['event'], 'gallery-new-content')
        self.assertTrue(event['new_visual_content'])
        self.assertEqual(event['visual'], comparison)

    def test_novelty_notification_does_not_wake_for_a_logo(self):
        event = dict(event='gallery-known-logo', nonblack=True, new_visual_content=False)
        self.assertFalse(gallery.notification_required(event, new_content_only=True))

    def test_novelty_notification_wakes_for_new_content(self):
        event = dict(event='gallery-new-content', new_visual_content=True)
        self.assertTrue(gallery.notification_required(event, new_content_only=True))

    def test_legacy_notification_still_wakes_for_every_update(self):
        self.assertTrue(gallery.notification_required(dict(event='gallery-added'), new_content_only=False))

    def test_novelty_notification_reports_a_replay_blocker(self):
        event = dict(event='gallery-failed', error='guarded memory unavailable')
        self.assertTrue(gallery.notification_required(event, new_content_only=True))

    def test_image_error_is_kept_and_never_signals_a_scene(self):
        with patch.object(gallery.image, 'render_fixture', side_effect=ValueError('readback failed')):
            event = self.scan()[0]
        self.assertEqual(event['event'], 'gallery-failed')
        self.assertFalse(event['nonblack'])
        self.assertIn('readback failed', event['error'])

    def test_existing_unowned_index_is_not_overwritten(self):
        self.output.mkdir()
        (self.output / 'INDEX.md').write_text('user work')
        with self.assertRaisesRegex(ValueError, 'unowned'):
            self.scan()
        self.assertEqual((self.output / 'INDEX.md').read_text(), 'user work')

    def test_old_runs_are_excluded_by_recent_window(self):
        old = time.time() - 48 * 3600
        os.utime(self.report, (old, old))
        self.assertEqual(gallery.discover(self.source, 24, time.time()), [])

    def test_output_inside_source_is_refused_before_any_write(self):
        target = self.source / 'unsafe-output'
        with self.assertRaisesRegex(ValueError, 'disjoint'):
            list(gallery.scan_once(self.source, target, self.build, code_revision='d' * 40))
        self.assertFalse(target.exists())

    def test_blocked_readback_does_not_signal_nonblack(self):
        def render(*arguments):
            self.fake_render(*arguments)
            path = arguments[2] / 'image.json'
            manifest = json.loads(path.read_text())
            manifest['verdict'] = 'image-readback-blocked'
            manifest['image'].update(distinct_rgba=4, alpha_nonzero=0)
            path.write_text(json.dumps(manifest))
            return 2
        with patch.object(gallery.image, 'render_fixture', side_effect=render):
            event = self.scan()[0]
        self.assertFalse(event['nonblack'])

    def test_unchanged_renderer_failure_is_not_replayed_forever(self):
        with patch.object(gallery.image, 'render_fixture', side_effect=ValueError('renderer refusal')) as renderer:
            self.scan()
            self.assertEqual(self.scan(), [])
        self.assertEqual(renderer.call_count, 1)

    def test_restored_completed_input_clears_stale_delivery_error(self):
        original = self.snapshot.read_bytes()
        with patch.object(gallery.image, 'render_fixture', side_effect=self.fake_render) as renderer:
            self.scan()
            manifest = json.loads(original)
            manifest['binary_sha256'] = '0' * 64
            self.snapshot.write_text(json.dumps(manifest))
            self.assertEqual(self.scan()[0]['event'], 'gallery-deferred')
            self.snapshot.write_bytes(original)
            self.assertEqual(self.scan(), [])
        self.assertEqual(renderer.call_count, 1)
        self.assertEqual(json.loads((self.output / 'GALLERY.json').read_text())['deferred'], {})

    def test_writer_change_during_staging_is_rejected_before_gpu_launch(self):
        load = gallery.memory_snapshot.load
        calls = 0
        def changing_load(*arguments):
            nonlocal calls
            loaded = load(*arguments)
            calls += 1
            if calls == 1:
                self.report.write_text(self.report.read_text().replace('draws=9', 'draws=10'))
            return loaded
        with patch.object(gallery.memory_snapshot, 'load', side_effect=changing_load), \
                patch.object(gallery.image, 'render_fixture') as renderer:
            event = self.scan()[0]
        renderer.assert_not_called()
        self.assertEqual(event['event'], 'gallery-failed')
        self.assertIn('changed while staging', event['error'])


if __name__ == '__main__':
    unittest.main()
