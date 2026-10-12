// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gx_ztexture_fixture.h"

namespace gx_indirect_fixture {

inline std::vector<std::uint8_t> quad(std::uint8_t matrix_id = 0,
                                     std::uint8_t format = 0,
                                     std::uint8_t stage = 0,
                                     std::uint8_t texmap = 1) {
  auto fifo = gx_ztexture_fixture::clear_quad(2, 0);
  constexpr auto draw_size = 3 + 4 * 5 * sizeof(float);
  const std::vector<std::uint8_t> draw(fifo.end() - draw_size, fifo.end());
  fifo.resize(fifo.size() - draw_size);
  gx_fixture::push_bp(fifo, 0x00, 1u | (2u << 10) | ((stage + 1u) << 16));
  const auto bank = texmap < 4 ? 0u : 0x20u;
  gx_fixture::push_bp(fifo, 0x88 + bank + texmap % 4,
                      gx_fixture::tex_image0(4, 4, 6));
  gx_fixture::push_bp(fifo, 0x94 + bank + texmap % 4, gx_fixture::kRampBase >> 5);
  gx_fixture::push_bp(fifo, 0x27, texmap << (6 * stage));
  gx_fixture::push_bp(fifo, 0x25 + stage / 2, 0x21u << (8 * (stage % 2)));
  gx_fixture::push_bp(fifo, 0x06, 256u | (1u << 22));
  gx_fixture::push_bp(fifo, 0x07, (256u << 11));
  gx_fixture::push_bp(fifo, 0x08, 1u << 22);
  gx_fixture::push_bp(fifo, 0x10, stage | (format << 2) | (7u << 4) |
                                 (1u << 7) | (1u << 9) | (matrix_id << 11));
  gx_fixture::push_bp(fifo, 0x11, stage | (format << 2) | (2u << 7) |
                                 (1u << 20) | (2u << 13) | (3u << 16));
  gx_fixture::push_bp(fifo, 0x12, 1u << 20);
  gx_fixture::push_bp(fifo, 0x28, (1u << 6) | (5u << 7) |
                                 ((1u << 6 | 6u << 7) << 12));
  gx_fixture::push_bp(fifo, 0x29, 1u << 6);
  for (std::uint32_t n = 1; n < 3; ++n) {
    gx_fixture::push_bp(fifo, 0xC0 + 2 * n, 8u | (15u << 4) | (15u << 8) |
                                            (15u << 12) | (1u << 19));
    gx_fixture::push_bp(fifo, 0xC1 + 2 * n, (5u << 4) | (7u << 7) | (7u << 10) |
                                            (7u << 13) | (1u << 19));
  }
  gx_fixture::push_bp(fifo, 0xF6, 4);
  gx_fixture::push_bp(fifo, 0xF7, 14);
  fifo.insert(fifo.end(), draw.begin(), draw.end());
  return fifo;
}

} // namespace gx_indirect_fixture
