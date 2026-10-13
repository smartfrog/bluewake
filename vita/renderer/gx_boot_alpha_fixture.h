// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../feasibility/gx/gx_fixture.hpp"

namespace gx_boot_alpha_fixture {

inline void vertex_color_state(std::vector<std::uint8_t>& fifo, bool textured) {
  gx_fixture::push_cp(fifo, DOL_GX_CP_REG_VCD_LO, (1u << 9) | (1u << 13));
  gx_fixture::push_cp(fifo, DOL_GX_CP_REG_VCD_HI, textured ? 1u : 0u);
  gx_fixture::push_cp(fifo, DOL_GX_CP_REG_VAT_GRP0,
                      1u | (4u << 1) | (1u << 13) | (5u << 14) | (1u << 21) | (4u << 22));
  gx_fixture::push_xf_block(fifo, 0x1009, {1});
  gx_fixture::push_xf_block(fifo, 0x100E, {1});
  gx_fixture::push_xf_block(fifo, 0x1010, {1});
  gx_fixture::push_xf_block(fifo, 0x103F, {textured ? 1u : 0u});
  gx_fixture::push_xf_block(fifo, 0x1018, {60u << 6});
  gx_fixture::push_xf_block(fifo, 0x1040, {5u << 7});
  if (textured)
    gx_fixture::push_xf_f32(fifo, 240, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
  gx_fixture::push_xf_f32(fifo, DOL_GX_XF_PROJECTION_BASE, {2, -1, -2, 1, 0.5f, 0});
  gx_fixture::push_xf_block(fifo, DOL_GX_XF_PROJECTION_TYPE, {1});
  gx_fixture::push_bp(fifo, 0x00, (1u << 4) | (textured ? 1u | (1u << 10) : 0u));
  gx_fixture::push_bp(fifo, 0x40, 0);
  gx_fixture::push_bp(fifo, 0x41, 1u | (4u << 8) | (5u << 5) | (1u << 3) | (1u << 4));
  gx_fixture::push_bp(fifo, DOL_GX_BP_REG_PE_CONTROL, 1);
  gx_fixture::push_bp(fifo, 0xF3, (7u << 16) | (7u << 19));
  gx_fixture::push_bp(fifo, 0xF6, 4);
  gx_fixture::push_bp(fifo, 0xF7, 14);
}

inline void quad(std::vector<std::uint8_t>& fifo, std::uint32_t rgba, bool textured) {
  gx_fixture::push_u8(fifo, 0x80);
  gx_fixture::push_u16(fifo, 4);
  constexpr float positions[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (const auto& position : positions) {
    gx_fixture::push_f32(fifo, position[0]);
    gx_fixture::push_f32(fifo, position[1]);
    gx_fixture::push_f32(fifo, -0.5f);
    gx_fixture::push_u32(fifo, rgba);
    if (textured) {
      gx_fixture::push_f32(fifo, position[0]);
      gx_fixture::push_f32(fifo, position[1]);
    }
  }
}

inline std::vector<std::uint8_t> black_overlay() {
  std::vector<std::uint8_t> fifo;
  gx_fixture::push_common_state(fifo);
  vertex_color_state(fifo, false);
  gx_fixture::push_bp(fifo, 0x28, 0);
  gx_fixture::push_bp(fifo, 0xC0, 10u | (15u << 4) | (15u << 8) | (15u << 12) | (1u << 19));
  gx_fixture::push_bp(fifo, 0xC1, (5u << 4) | (7u << 7) | (7u << 10) | (7u << 13) | (1u << 19));
  quad(fifo, 17, false);
  return fifo;
}

inline std::vector<std::uint8_t> transparent_notice() {
  auto fifo = gx_fixture::build_ci8_triangle();
  const auto authored_triangle_bytes = 3 + 3 * 5 * sizeof(float);
  fifo.resize(fifo.size() - authored_triangle_bytes);
  vertex_color_state(fifo, true);
  gx_fixture::push_bp(fifo, 0x28, (1u << 6) | (7u << 7));
  gx_fixture::push_bp(fifo, 0xC0, 15u | (15u << 4) | (15u << 8) | (8u << 12) | (1u << 19));
  gx_fixture::push_bp(fifo, 0xC1, (7u << 4) | (7u << 7) | (7u << 10) | (4u << 13) | (1u << 19));
  gx_fixture::push_bp(fifo, 0xC2, 15u | (10u << 4) | (15u << 12) | (1u << 19));
  gx_fixture::push_bp(fifo, 0xC3, (7u << 4) | (5u << 7) | (7u << 13) | (1u << 19));
  quad(fifo, 0xFFFFFF00, true);
  return fifo;
}

} // namespace gx_boot_alpha_fixture
