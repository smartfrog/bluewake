from pathlib import Path
import struct
import tempfile
import unittest
import zlib

import gallery


def write_rgba_png(path, width, height, pixels):
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data +
                struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF))

    rows = b''.join(b'\0' + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    header = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) +
                     chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


class NoveltyTests(unittest.TestCase):
    def reference(self):
        return gallery.novelty.logo_reference(2, 2, bytes([
            0, 0, 0, 255, 220, 0, 0, 255,
            50, 0, 0, 255, 0, 0, 0, 255]))

    def test_red_logo_fade_is_not_new_content(self):
        frame = bytes([0, 0, 0, 255, 15, 0, 0, 255,
                       3, 0, 0, 255, 0, 0, 0, 255])
        result = gallery.novelty.classify_pixels(2, 2, frame, self.reference())
        self.assertEqual(result['classification'], 'logo-compatible')

    def test_real_png_logo_load_and_frame_classification(self):
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            path = Path(directory) / 'logo.png'
            write_rgba_png(path, 2, 1, bytes([0, 0, 0, 255, 220, 0, 0, 255]))
            self.assertEqual(gallery.image.png_metadata(path)['distinct_rgba'], 2)
            reference = gallery.novelty.load_logo(path)
            result = gallery.novelty.classify_frame(path, reference['sha256'], reference)
        self.assertEqual(result['classification'], 'logo-compatible')
        self.assertEqual(result['reference_sha256'], reference['sha256'])

    def test_black_frame_is_not_new_content(self):
        result = gallery.novelty.classify_pixels(2, 2, bytes(16), self.reference())
        self.assertEqual(result['classification'], 'black')

    def test_new_red_pixels_outside_logo_signal_a_candidate(self):
        frame = bytes([1, 0, 0, 255] + [0] * 12)
        result = gallery.novelty.classify_pixels(2, 2, frame, self.reference())
        self.assertEqual(result['classification'], 'new-content-candidate')
        self.assertEqual(result['outside_logo_pixels'], 1)

    def test_new_color_inside_logo_signals_a_candidate(self):
        frame = bytes([0, 0, 0, 255, 0, 0, 100, 255] + [0] * 8)
        result = gallery.novelty.classify_pixels(2, 2, frame, self.reference())
        self.assertEqual(result['classification'], 'new-content-candidate')
        self.assertEqual(result['non_red_pixels'], 1)

    def test_red_above_known_logo_range_signals_a_candidate(self):
        frame = bytes([0, 0, 0, 255, 255, 0, 0, 255] + [0] * 8)
        result = gallery.novelty.classify_pixels(2, 2, frame, self.reference())
        self.assertEqual(result['classification'], 'new-content-candidate')

    def test_changed_dimensions_require_visual_inspection(self):
        result = gallery.novelty.classify_pixels(1, 1, bytes([10, 0, 0, 255]), self.reference())
        self.assertEqual(result['classification'], 'new-content-candidate')
        self.assertEqual(result['reason'], 'dimensions-changed')

    def test_incomplete_pixels_are_refused(self):
        with self.assertRaisesRegex(ValueError, 'complete RGBA8'):
            gallery.novelty.classify_pixels(2, 2, bytes(15), self.reference())

    def test_black_reference_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'red logo'):
            gallery.novelty.logo_reference(2, 2, bytes(16))

    def test_non_red_reference_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'red logo'):
            gallery.novelty.logo_reference(1, 1, bytes([0, 100, 0, 255]))


if __name__ == '__main__':
    unittest.main()
