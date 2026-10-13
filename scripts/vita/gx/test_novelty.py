import unittest

import gallery


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
