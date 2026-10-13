import math
import unittest

import lod_reference as lod


class SamplerTests(unittest.TestCase):
    def test_preserves_all_lod_mode_fields(self):
        mode0 = 2 | (3 << 2) | (1 << 4) | (2 << 5) | (1 << 7) | (1 << 8)
        mode0 |= (0xE0 << 9) | (2 << 19) | (1 << 21)
        sampler = lod.sampler(mode0, 0x3820)
        self.assertEqual(sampler, dict(wrap_s=2, wrap_t=3, mag_linear=True,
                                      mip_mode=2, min_linear=True, diagonal=True,
                                      bias_s2_5=-32, anisotropy=2, bias_clamp=True,
                                      min_lod=32, max_lod=56))

    def test_gx_filter_enum_is_not_the_bp_filter_encoding(self):
        expected = [(0, False), (0, True), (1, False), (1, True), (2, False), (2, True)]
        for hardware, (mip, linear) in zip([0, 4, 1, 5, 2, 6], expected):
            with self.subTest(hardware=hardware):
                sampler = lod.sampler(hardware << 5, 0)
                self.assertEqual((sampler['mip_mode'], sampler['min_linear']), (mip, linear))

    def test_gradient_selects_original_footprint_without_moving_lookup(self):
        sampler = lod.sampler(1 << 5, 0xA000)
        original = lod.select((128, 0), (0, 128), sampler)
        modified = lod.select((1024, 0), (0, 1024), sampler)
        self.assertEqual((original['lod'], modified['lod']), (0, 48))

    def test_diagonal_mode_changes_the_selected_level(self):
        edge = lod.select((128, 0), (128, 0), lod.sampler(1 << 5, 0xA000))
        diagonal = lod.select((128, 0), (128, 0), lod.sampler((1 << 5) | (1 << 8), 0xA000))
        self.assertEqual((edge['level'], diagonal['level']), (0, 1))

    def test_signed_bias_moves_the_selected_level(self):
        sampler = lod.sampler((1 << 5) | (0xE0 << 9), 0xA000)
        self.assertEqual(lod.select((256, 0), (0, 256), sampler)['lod'], 0)

    def test_zero_derivative_uses_minimum_without_adding_bias(self):
        sampler = lod.sampler((1 << 5) | (0xE0 << 9), 0xA010)
        self.assertEqual(lod.select((0, 0), (0, 0), sampler)['lod'], 16)

    def test_min_mag_selection_precedes_lod_bounds(self):
        sampler = lod.sampler((1 << 5) | (1 << 4), 0xA020)
        selected = lod.select((128, 0), (0, 128), sampler)
        self.assertEqual((selected['linear'], selected['level']), (True, 2))

    def test_inverted_lod_bounds_use_maximum_as_reference_normalization(self):
        sampler = lod.sampler(1 << 5, 0x1040)
        self.assertEqual(lod.select((128, 0), (0, 128), sampler)['lod'], 16)

    def test_fraction_rounds_point_mip_at_half(self):
        sampler = lod.sampler(1 << 5, 0xA000)
        for fraction in range(16):
            with self.subTest(fraction=fraction):
                rho = 2 ** ((fraction + 0.25) / 16)
                selected = lod.select((rho * 128, 0), (0, rho * 128), sampler)
                self.assertEqual((selected['fraction'], selected['level']), (fraction, int(fraction >= 8)))

    def test_linear_mip_keeps_sixteenths(self):
        sampler = lod.sampler(2 << 5, 0xA000)
        selected = lod.select((192, 0), (0, 192), sampler)
        self.assertEqual((selected['lod'], selected['level'], selected['fraction']), (9, 0, 9))

    def test_no_mip_mode_stays_at_level_zero(self):
        sampler = lod.sampler(1 << 7, 0xFF00)
        self.assertEqual(lod.select((4096, 0), (0, 4096), sampler)['level'], 0)

    def test_bias_clamp_is_not_fabricated_from_unknown_reference_behavior(self):
        with self.assertRaisesRegex(ValueError, 'bias-clamp.*unimplemented'):
            lod.select((128, 0), (0, 128), lod.sampler(1 << 21, 0))

    def test_anisotropy_is_not_guessed(self):
        with self.assertRaisesRegex(ValueError, 'anisotropy'):
            lod.select((128, 0), (0, 128), lod.sampler(1 << 19, 0))

    def test_nonfinite_gradients_are_refused(self):
        for value in [math.nan, math.inf, -math.inf]:
            with self.subTest(value=value):
                with self.assertRaisesRegex(ValueError, 'finite'):
                    lod.select((value, 0), (0, 128), lod.sampler(0, 0))

    def test_pinned_software_and_gpu_lod_fractions_disagree(self):
        gpu = lod.select((192, 0), (0, 192), lod.sampler(2 << 5, 0xA000))
        self.assertEqual((lod.software_fixed_log2(1.5), gpu['lod']), (8, 9))

    def test_invalid_sampler_register_values_are_refused(self):
        for value in [True, -1, 1 << 24, 0.5]:
            with self.subTest(value=value):
                with self.assertRaisesRegex(ValueError, '24-bit'):
                    lod.sampler(value, 0)


class MipExtentTests(unittest.TestCase):
    def test_rgba8_small_levels_each_occupy_a_whole_tile(self):
        self.assertEqual(lod.mip_layout(6, 4, 4, 3), [
            dict(level=0, width=4, height=4, offset=0, size=64),
            dict(level=1, width=2, height=2, offset=64, size=64),
            dict(level=2, width=1, height=1, offset=128, size=64)])

    def test_mip_source_size_is_not_tightly_packed_rgba_size(self):
        for format_, tile in [(0, 32), (1, 32), (2, 32), (3, 32), (4, 32),
                              (5, 32), (6, 64), (8, 32), (9, 32), (10, 32), (14, 32)]:
            with self.subTest(format=format_):
                self.assertEqual(lod.mip_layout(format_, 1, 1, 1)[0]['size'], tile)

    def test_rectangular_levels_stop_when_both_axes_are_one(self):
        layout = lod.mip_layout(14, 1024, 1, 11)
        self.assertEqual([(entry['width'], entry['height']) for entry in layout],
                         [(1024 >> n, 1) for n in range(11)])

    def test_excess_levels_are_not_invented_after_one_by_one(self):
        with self.assertRaisesRegex(ValueError, 'level count'):
            lod.mip_layout(6, 4, 4, 4)

    def test_non_power_of_two_layout_is_explicitly_outside_bounded_oracle(self):
        with self.assertRaisesRegex(ValueError, 'power-of-two'):
            lod.mip_layout(6, 7, 3, 3)

    def test_missing_mip_payload_is_refused_instead_of_generated(self):
        with self.assertRaisesRegex(ValueError, 'captured mip bytes'):
            lod.captured_levels(bytes(64), lod.mip_layout(6, 4, 4, 3))

    def test_captured_levels_preserve_independent_authored_colors(self):
        payload = bytes([10]) * 64 + bytes([200]) * 64 + bytes([50]) * 64
        levels = lod.captured_levels(payload, lod.mip_layout(6, 4, 4, 3))
        self.assertEqual(levels, [bytes([10]) * 64, bytes([200]) * 64, bytes([50]) * 64])

    def test_mip_fraction_blend_truncates_instead_of_half_up(self):
        self.assertEqual(lod.blend_mips(0, 1, 8), 0)

    def test_mip_fraction_blend_covers_all_weights(self):
        for fraction in range(16):
            with self.subTest(fraction=fraction):
                self.assertEqual(lod.blend_mips(0, 240, fraction), 15 * fraction)

    def test_byte_blend_overflow_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'bounds'):
            lod.blend_mips(256, 0, 0)


if __name__ == '__main__':
    unittest.main()
