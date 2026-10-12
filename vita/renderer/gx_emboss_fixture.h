// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gx_ztexture_fixture.h"

namespace gx_emboss_fixture {

inline std::vector<std::uint8_t> quad(bool indexed = false, bool chained = false,
                                     std::uint8_t light = 0) {
  auto fifo = gx_ztexture_fixture::clear_quad(2, 0);
  fifo.resize(fifo.size() - (3 + 4 * 5 * sizeof(float)));
  gx_fixture::push_cp(fifo, 0x50, (1u << 9) | (1u << 11) | (indexed ? 1u : 0u));
  gx_fixture::push_cp(fifo, 0x70, 1u | (4u << 1) | (1u << 9) | (4u << 10) |
                                  (1u << 21) | (4u << 22));
  gx_fixture::push_xf_block(fifo, 0x103F, {chained ? 3u : 2u});
  gx_fixture::push_xf_block(fifo, 0x1009, {1});
  const std::uint32_t light_mask = 1u << light;
  gx_fixture::push_xf_block(fifo, 0x100E,
                          {2u | ((light_mask & 15u) << 2) | ((light_mask >> 4) << 11)});
  gx_fixture::push_xf_block(fifo, 0x1041, {(1u << 4) | (light << 15)});
  if (chained)
    gx_fixture::push_xf_block(fifo, 0x1042, {(1u << 4) | (1u << 12) | (light << 15)});
  gx_fixture::push_xf_f32(fifo, indexed ? 0x409 : 0x400,
                        {indexed ? -2.f : 2.f, 0, 0, 0, 3, 0, 0, 0, 1});
  if (indexed)
    gx_fixture::push_xf_f32(fifo, 12, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
  gx_fixture::push_xf_block(fifo, 0x600 + 16 * light,
                          {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                           gx_fixture::f32_bits(2), gx_fixture::f32_bits(3),
                           gx_fixture::f32_bits(-0.5f), 0, 0, 0});
  gx_fixture::push_bp(fifo, 0x00, chained ? 3u : 2u);
  gx_fixture::push_bp(fifo, 0x28, (1u << 6) | ((chained ? 2u : 1u) << 3));
  gx_fixture::push_u8(fifo, 0x80);
  gx_fixture::push_u16(fifo, 4);
  constexpr float vertices[4][5] = {{-1, -1, -0.5f, 0, 0}, {1, -1, -0.5f, 1, 0},
                                    {1, 1, -0.5f, 1, 1}, {-1, 1, -0.5f, 0, 1}};
  for (const auto& vertex : vertices) {
    if (indexed)
      gx_fixture::push_u8(fifo, 3);
    for (int axis = 0; axis < 3; ++axis)
      gx_fixture::push_f32(fifo, vertex[axis]);
    for (float value : {0, 0, 1, 0, 1, 0, 1, 0, 0})
      gx_fixture::push_f32(fifo, value);
    gx_fixture::push_f32(fifo, vertex[3]);
    gx_fixture::push_f32(fifo, vertex[4]);
  }
  return fifo;
}

} // namespace gx_emboss_fixture
