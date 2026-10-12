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

} // namespace gx_ztexture_fixture
