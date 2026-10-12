// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../feasibility/gx/gx_fixture.hpp"

namespace gx_ztexture_fixture {

inline void fill_memory(gx_fixture::FixtureContext& memory) {
  memory.mem1.assign(0x2000, 0);
  for (std::uint32_t pixel = 0; pixel < 16; ++pixel) {
    memory.mem1[gx_fixture::kTextureBase + 2 * pixel] = 0;
    memory.mem1[gx_fixture::kTextureBase + 2 * pixel + 1] = 255;
    memory.mem1[gx_fixture::kTextureBase + 32 + 2 * pixel] = 255;
    memory.mem1[gx_fixture::kTextureBase + 33 + 2 * pixel] = 255;
  }
}

inline std::vector<std::uint8_t> clear_quad(std::uint8_t type = 2,
                                          std::uint8_t operation = 2,
                                          std::uint32_t bias = 0) {
  std::vector<std::uint8_t> fifo;
  gx_fixture::push_common_state(fifo);
  gx_fixture::push_xf_f32(fifo, DOL_GX_XF_PROJECTION_BASE, {1, 0, 1, 0, 1, 0});
  gx_fixture::push_xf_block(fifo, DOL_GX_XF_PROJECTION_TYPE, {1});
  gx_fixture::push_xf_block(fifo, 0x1018, {60u << 6});
  gx_fixture::push_xf_block(fifo, 0x1040, {5u << 7});
  gx_fixture::push_xf_f32(fifo, 240, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
  gx_fixture::push_bp(fifo, 0x00, 1);
  gx_fixture::push_bp(fifo, 0x40, 0x1F);
  gx_fixture::push_bp(fifo, 0x43, 0);
  gx_fixture::push_bp(fifo, 0x28, 1u << 6);
  gx_fixture::push_bp(fifo, 0x30, 3);
  gx_fixture::push_bp(fifo, 0x31, 3);
  gx_fixture::push_bp(fifo, 0x80, 0);
  gx_fixture::push_bp(fifo, 0xC0, 8u | (15u << 4) | (15u << 8) |
                                (15u << 12) | (1u << 19));
  gx_fixture::push_bp(fifo, 0xC1, (4u << 4) | (7u << 7) | (7u << 10) |
                                (7u << 13) | (1u << 19));
  gx_fixture::push_bp(fifo, 0xF3, (7u << 16) | (7u << 19));
  gx_fixture::push_bp(fifo, 0xF4, bias);
  gx_fixture::push_bp(fifo, 0xF5, type | (operation << 2));
  gx_fixture::push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0, gx_fixture::tex_image0(4, 4, 6));
  gx_fixture::push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3, gx_fixture::kTextureBase >> 5);
  gx_fixture::push_draw(fifo, 0x80, 4,
                        {-1, -1, -0.5f, 0, 0, 1, -1, -0.5f, 1, 0,
                          1,  1, -0.5f, 1, 1, -1, 1, -0.5f, 0, 1});
  return fifo;
}

inline std::vector<std::uint8_t> matrix_quad(bool indexed = false, bool projected = false) {
  auto fifo = clear_quad();
  fifo.resize(fifo.size() - (3 + 4 * 5 * sizeof(float)));
  gx_fixture::push_xf_block(fifo, 0x1018, {30u << 6});
  gx_fixture::push_xf_block(fifo, 0x1040, {(5u << 7) | (projected ? 2u : 0u)});
  gx_fixture::push_xf_f32(fifo, 120, {2, 0, 0, 4, 0, 3, 0, 5, 1, 0, 0, 2,
                                    4, 0, 0, 8, 0, 5, 0, 9, 1, 0, 0, 3});
  if (indexed)
    gx_fixture::push_cp(fifo, 0x50, (1u << 9) | (1u << 1));
  gx_fixture::push_u8(fifo, 0x80);
  gx_fixture::push_u16(fifo, 4);
  constexpr float vertices[4][5] = {{-1, -1, -0.5f, 0, 0}, {1, -1, -0.5f, 1, 0},
                                    {1, 1, -0.5f, 1, 1}, {-1, 1, -0.5f, 0, 1}};
  for (std::uint32_t v = 0; v < 4; ++v) {
    if (indexed)
      gx_fixture::push_u8(fifo, v % 2 == 0 ? 30 : 33);
    for (float value : vertices[v])
      gx_fixture::push_f32(fifo, value);
  }
  return fifo;
}

inline std::vector<std::uint8_t> post_matrix_quad() {
  auto fifo = matrix_quad();
  const auto draw_size = 3 + 4 * 5 * sizeof(float);
  const std::vector<std::uint8_t> draw(fifo.end() - draw_size, fifo.end());
  fifo.resize(fifo.size() - draw_size);
  gx_fixture::push_xf_block(fifo, 0x1012, {1});
  gx_fixture::push_xf_f32(fifo, 0x500, {1, 0, 0, 10, 0, 1, 0, 20, 0, 0, 1, 0});
  gx_fixture::push_xf_block(fifo, 0x1050, {0});
  fifo.insert(fifo.end(), draw.begin(), draw.end());
  return fifo;
}

inline std::vector<std::uint8_t> lighting_quad(std::uint8_t light = 0) {
  auto fifo = clear_quad();
  fifo.resize(fifo.size() - (3 + 4 * 5 * sizeof(float)));
  gx_fixture::push_cp(fifo, 0x50, (1u << 9) | (1u << 11));
  gx_fixture::push_cp(fifo, 0x70, 1u | (4u << 1) | (4u << 10) |
                                (1u << 21) | (4u << 22));
  gx_fixture::push_xf_f32(fifo, 0x400, {1, 0, 0, 0, 1, 0, 0, 0, 1});
  gx_fixture::push_xf_block(fifo, 0x1009, {1});
  gx_fixture::push_xf_block(fifo, 0x100A, {0});
  gx_fixture::push_xf_block(fifo, 0x100C, {0xFFFFFFFF});
  const std::uint32_t mask = 1u << light;
  gx_fixture::push_xf_block(fifo, 0x100E, {2u | ((mask & 15u) << 2) |
                                               ((mask >> 4) << 11) | (2u << 7)});
  gx_fixture::push_xf_block(fifo, 0x1010, {1});
  gx_fixture::push_xf_block(fifo, 0x1040, {2u << 4});
  gx_fixture::push_xf_block(fifo, 0x600 + 16 * light,
                          {0, 0, 0, 0x4080C0FF, gx_fixture::f32_bits(1), 0, 0,
                           gx_fixture::f32_bits(1), 0, 0, 0, 0,
                           gx_fixture::f32_bits(10000), 0, 0, gx_fixture::f32_bits(1)});
  gx_fixture::push_bp(fifo, 0xC0, 10u | (15u << 4) | (15u << 8) |
                                (15u << 12) | (1u << 19));
  gx_fixture::push_bp(fifo, 0xC1, (5u << 4) | (7u << 7) | (7u << 10) |
                                (7u << 13) | (1u << 19));
  gx_fixture::push_bp(fifo, 0xF6, 4);
  gx_fixture::push_bp(fifo, 0xF7, 14);
  gx_fixture::push_draw(fifo, 0x80, 4,
                        {-1, -1, -0.5f, 0, 0, 1, 0, 0,
                          1, -1, -0.5f, 0, 0, 1, 1, 0,
                          1,  1, -0.5f, 0, 0, 1, 1, 1,
                         -1,  1, -0.5f, 0, 0, 1, 0, 1});
  return fifo;
}

} // namespace gx_ztexture_fixture
