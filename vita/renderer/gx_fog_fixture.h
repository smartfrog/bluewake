// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gx_ztexture_fixture.h"

namespace gx_fog_fixture {

inline std::uint32_t parameter(float value) {
  const auto bits = gx_fixture::f32_bits(value);
  return ((bits >> 12) & 0x7FF) | (((bits >> 23) & 0xFF) << 11) |
         ((bits >> 31) << 19);
}

inline std::vector<std::uint8_t> quad(std::uint8_t mode = 2,
                                     bool orthographic = false, bool range = false,
                                     std::uint8_t ztexture = 0) {
  auto fifo = gx_ztexture_fixture::clear_quad(2, ztexture);
  constexpr auto draw_size = 3 + 4 * 5 * sizeof(float);
  const std::vector<std::uint8_t> draw(fifo.end() - draw_size, fifo.end());
  fifo.resize(fifo.size() - draw_size);
  gx_fixture::push_xf_f32(fifo, DOL_GX_XF_VIEWPORT_BASE,
                        {320, -264, 16777215.f, 662, 606, 16777215.f});
  gx_fixture::push_bp(fifo, 0xEE, parameter(orthographic ? 1.f : 0.25f));
  gx_fixture::push_bp(fifo, 0xEF, 0xC00000);
  gx_fixture::push_bp(fifo, 0xF0, 1);
  gx_fixture::push_bp(fifo, 0xF1, parameter(0.125f) | (mode << 21) |
                                (orthographic ? 1u << 20 : 0));
  gx_fixture::push_bp(fifo, 0xF2, 0x204080);
  gx_fixture::push_bp(fifo, 0xE8, 662 | (range ? 1u << 10 : 0));
  for (std::uint32_t i = 0; i < 5; ++i)
    gx_fixture::push_bp(fifo, 0xE9 + i, (256 + 32 * (2 * i)) |
                                      ((256 + 32 * (2 * i + 1)) << 12));
  fifo.insert(fifo.end(), draw.begin(), draw.end());
  return fifo;
}

} // namespace gx_fog_fixture
