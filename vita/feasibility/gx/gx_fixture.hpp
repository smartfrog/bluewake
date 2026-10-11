// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 GX fixtures: documented synthetic command streams and the replay
// path through the pinned RetailGxFrontend and GxCoreSink.
//
// Every input value belongs to the fixture, so a consumer (the report tool,
// the vitaGL translation test) can check the pinned decode against the
// fixture's own bytes. The texture zones are deterministic patterns.

#pragma once

#include "gxruntime/aurora_recomp/retail_gx_frontend.hpp"
#include "gxruntime/gxcore/gxcore.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace gx_fixture {

namespace ar = gxruntime::aurora_recomp;
namespace gxc = gxruntime::gxcore;

// --- Documented GX FIFO encodings (big-endian, as the game writes them) -----

inline void push_u8(std::vector<std::uint8_t>& fifo, std::uint8_t value) {
  fifo.push_back(value);
}

inline void push_u16(std::vector<std::uint8_t>& fifo, std::uint16_t value) {
  fifo.push_back(static_cast<std::uint8_t>(value >> 8u));
  fifo.push_back(static_cast<std::uint8_t>(value));
}

inline void push_u32(std::vector<std::uint8_t>& fifo, std::uint32_t value) {
  fifo.push_back(static_cast<std::uint8_t>(value >> 24u));
  fifo.push_back(static_cast<std::uint8_t>(value >> 16u));
  fifo.push_back(static_cast<std::uint8_t>(value >> 8u));
  fifo.push_back(static_cast<std::uint8_t>(value));
}

inline std::uint32_t f32_bits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline void push_f32(std::vector<std::uint8_t>& fifo, float value) {
  push_u32(fifo, f32_bits(value));
}

inline void push_cp(std::vector<std::uint8_t>& fifo, std::uint8_t reg,
                    std::uint32_t value) {
  push_u8(fifo, DOL_GX_CMD_LOAD_CP_REG);
  push_u8(fifo, reg);
  push_u32(fifo, value);
}

inline void push_bp(std::vector<std::uint8_t>& fifo, std::uint8_t reg,
                    std::uint32_t value) {
  push_u8(fifo, DOL_GX_CMD_LOAD_BP_REG);
  push_u32(fifo, (static_cast<std::uint32_t>(reg) << 24u) |
                     (value & 0x00FFFFFFu));
}

inline void push_xf_block(std::vector<std::uint8_t>& fifo, std::uint16_t base,
                          const std::vector<std::uint32_t>& values) {
  push_u8(fifo, DOL_GX_CMD_LOAD_XF_REG);
  push_u32(fifo, (static_cast<std::uint32_t>(values.size() - 1u) << 16u) |
                     static_cast<std::uint32_t>(base));
  for (std::uint32_t value : values)
    push_u32(fifo, value);
}

inline void push_xf_f32(std::vector<std::uint8_t>& fifo, std::uint16_t base,
                        const std::vector<float>& values) {
  std::vector<std::uint32_t> words;
  words.reserve(values.size());
  for (float value : values)
    words.push_back(f32_bits(value));
  push_xf_block(fifo, base, words);
}

inline void push_draw(std::vector<std::uint8_t>& fifo, std::uint8_t command,
                      std::uint16_t vertex_count,
                      const std::vector<float>& data) {
  push_u8(fifo, command);
  push_u16(fifo, vertex_count);
  for (float value : data)
    push_f32(fifo, value);
}

// --- Fixture memory: one zeroed MEM1 image behind the guest resolver -------

struct FixtureContext {
  std::vector<std::uint8_t> mem1;
};

inline constexpr u32 kTextureBase = 0x1000u; // 4x4 RGBA8, 64 bytes
inline constexpr u32 kTextureWidth = 4u;
inline constexpr u32 kTextureHeight = 4u;
inline constexpr u32 kRampBase = 0x1800u; // 4x4 RGBA8 ramp, 64 bytes
inline constexpr u32 kCi8Base = 0x800u; // 8x8 CI8 indices, 64 bytes
inline constexpr u32 kCi8Width = 8u;
inline constexpr u32 kCi8Height = 8u;
inline constexpr u32 kTlutBase = 0x1400u; // 16 RGB5A3 entries, 32 bytes
inline constexpr u32 kTlutEntries = 16u;
inline constexpr u32 kTlutTmem = 0x20u;
inline constexpr u32 kCopyBase = 0x1C00u; // EFB copy destination, 4x4 R8 = 16 bytes

inline bool resolve_mem1(void* user, u32 address, u32 size,
                         DolGuestAddressSpace space,
                         DolGuestResourceKind resource,
                         DolGuestResolvedRange* out) {
  auto* ctx = static_cast<FixtureContext*>(user);
  if (out == nullptr || size == 0u)
    return false;
  const u32 physical = dol_gx_recomp_guest_to_physical(address);
  if (physical >= ctx->mem1.size() || size > ctx->mem1.size() - physical)
    return false;
  *out = {
      .data = ctx->mem1.data() + physical,
      .address = address,
      .size = size,
      .available = static_cast<u32>(ctx->mem1.size() - physical),
      .space = space,
      .resource = resource,
  };
  return true;
}

// Fill the fixture's texture zones: the 4x4 RGBA8 pattern (byte i = i), the
// 4x4 RGBA8 ramp (byte i = 0xFF - i), the 8x8 CI8 indices (i % 16) and the
// 16-entry RGB5A3 palette (opaque, one distinct value per entry).
inline void fill_texture_zones(FixtureContext& ctx) {
  ctx.mem1.assign(24u << 20u, 0u);
  for (std::uint32_t i = 0; i < 64u; ++i)
    ctx.mem1[kTextureBase + i] = static_cast<std::uint8_t>(i);
  for (std::uint32_t i = 0; i < 64u; ++i)
    ctx.mem1[kRampBase + i] = static_cast<std::uint8_t>(0xFFu - i);
  for (std::uint32_t i = 0; i < 64u; ++i)
    ctx.mem1[kCi8Base + i] = static_cast<std::uint8_t>(i % 16u);
  for (std::uint32_t i = 0; i < kTlutEntries; ++i) {
    const std::uint16_t entry =
        static_cast<std::uint16_t>(0x8000u | (i * 0x421u));
    ctx.mem1[kTlutBase + i * 2u] = static_cast<std::uint8_t>(entry >> 8u);
    ctx.mem1[kTlutBase + i * 2u + 1u] = static_cast<std::uint8_t>(entry);
  }
}

// The BP image0 payload: width-1 | (height-1) << 10 | format << 20.
inline std::uint32_t tex_image0(std::uint16_t width, std::uint16_t height,
                                std::uint32_t format) {
  return static_cast<std::uint32_t>(width - 1u) |
         (static_cast<std::uint32_t>(height - 1u) << 10u) |
         ((format & 0xFu) << 20u);
}

// --- Shared stream setup ----------------------------------------------------
//
// Direct float32 position + tex0 (VCD_LO bit 9 = position direct, VCD_HI
// bit 0 = tex0 direct; VAT g0 bit 0 = 3 position elements, bits 1-3 = format
// 4 f32, bit 21 = 2 tex0 elements, bits 22-24 = format 4 f32), an identity
// position matrix, a viewport, a projection and one texgen.

inline void push_common_state(std::vector<std::uint8_t>& fifo) {
  push_cp(fifo, DOL_GX_CP_REG_VCD_LO, 1u << 9u);
  push_cp(fifo, DOL_GX_CP_REG_VCD_HI, 1u << 0u);
  const std::uint32_t vat0 =
      (1u << 0u) | (4u << 1u) | (1u << 21u) | (4u << 22u);
  push_cp(fifo, DOL_GX_CP_REG_VAT_GRP0, vat0);
  push_cp(fifo, DOL_GX_CP_REG_VAT_GRP1, 0u);
  push_cp(fifo, DOL_GX_CP_REG_VAT_GRP2, 0u);

  const std::vector<std::uint32_t> identity = {
      0x3F800000u, 0u, 0u, 0u, 0u, 0x3F800000u,
      0u, 0u, 0u, 0u, 0x3F800000u, 0u,
  };
  push_xf_block(fifo, 0x0000u, identity);
  push_xf_f32(fifo, DOL_GX_XF_VIEWPORT_BASE,
              {320.f, -240.f, 16777215.f, 342.f, 342.f, 16777215.f});
  push_xf_f32(fifo, DOL_GX_XF_PROJECTION_BASE,
              {1.f, 1.f, 0.1f, 1000.f, 0.f, 0.f});
  push_xf_block(fifo, DOL_GX_XF_PROJECTION_TYPE, {0u});
  push_xf_block(fifo, 0x103Fu, {1u}); // numTexGens
}

inline void push_triangle(std::vector<std::uint8_t>& fifo) {
  push_draw(fifo, 0x90u, 3u,
            {
                0.f, 0.f, 0.f, 0.f, 0.f,
                1.f, 0.f, 0.f, 1.f, 0.f,
                0.f, 1.f, 0.f, 0.f, 1.f,
            });
}

// --- Scenarios ---------------------------------------------------------------

// One textured triangle; the texture is the 4x4 RGBA8 pattern at 0x1000.
inline std::vector<std::uint8_t> build_textured_triangle() {
  std::vector<std::uint8_t> fifo;
  push_common_state(fifo);
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0,
          tex_image0(kTextureWidth, kTextureHeight, 6u)); // GX_TF_RGBA8
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3, kTextureBase >> 5u);
  push_triangle(fifo);
  return fifo;
}

// One CI8 triangle through the 16-entry RGB5A3 palette.
inline std::vector<std::uint8_t> build_ci8_triangle() {
  std::vector<std::uint8_t> fifo;
  push_common_state(fifo);
  push_bp(fifo, DOL_GX_BP_REG_LOAD_TLUT0, kTlutBase >> 5u);
  push_bp(fifo, DOL_GX_BP_REG_LOAD_TLUT1,
          kTlutTmem | ((kTlutEntries / 16u) << 10u));
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0,
          tex_image0(kCi8Width, kCi8Height, 9u)); // GX_TF_C8
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3, kCi8Base >> 5u);
  push_bp(fifo, DOL_GX_BP_REG_TX_SETTLUT, kTlutTmem | (2u << 10u));
  push_triangle(fifo);
  return fifo;
}

// Depth/alpha/blending state on one textured triangle.
inline std::vector<std::uint8_t> build_state_triangle() {
  std::vector<std::uint8_t> fifo;
  push_common_state(fifo);
  push_bp(fifo, DOL_GX_BP_REG_GENMODE, (1u << 0u) | (1u << 14u));
  push_bp(fifo, 0x40u, 1u | (3u << 1u) | (1u << 4u));
  push_bp(fifo, 0x41u, 1u | (4u << 8u) | (5u << 5u) | (1u << 3u) | (1u << 4u));
  push_bp(fifo, DOL_GX_BP_REG_PE_CONTROL, 1u);
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0,
          tex_image0(kTextureWidth, kTextureHeight, 6u));
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3, kTextureBase >> 5u);
  push_triangle(fifo);
  return fifo;
}

// Cel-shading: two texgens with the second a Color0 toon source, channel 0 lit
// by light 0, and a two-stage TEV (stage 0 ramp * raster, stage 1 diffuse *
// prev).
inline std::vector<std::uint8_t> build_cel_triangle() {
  std::vector<std::uint8_t> fifo;
  push_common_state(fifo);
  push_xf_block(fifo, 0x103Fu, {2u});     // numTexGens = 2
  push_xf_block(fifo, 0x1041u, {0x120u}); // texgen 1: type Color0
  push_xf_block(fifo, 0x1009u, {1u});     // numChans = 1
  push_xf_block(fifo, 0x100Eu, {0x06u});  // color ctrl 0: lit, light 0
  push_xf_block(fifo, 0x100Au, {0xFFFFFFFFu}); // ambient 0
  push_xf_block(fifo, 0x100Cu, {0xFFFFFFFFu}); // material 0
  push_xf_block(fifo, 0x0600u,
                {
                    0u, 0u, 0u, 0xFFFFFFFFu,     // reserved, RGBA8 colour
                    0x3F800000u, 0u, 0u, 0u,     // cosatt
                    0u, 0u, 0u, 0u,              // distatt
                    0u, 0u, 0u, 0u,              // pos
                    0u, 0u, 0x3F800000u, 0u,     // dir
                });
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0,
          tex_image0(kTextureWidth, kTextureHeight, 6u));
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3, kTextureBase >> 5u);
  // The ramp lives in texture slot 1: the stage 0 texmap 1 samples it, so the
  // multi-texmap set must carry two images (the flat single texture would
  // silently lose the ramp).
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0 + 1u,
          tex_image0(kTextureWidth, kTextureHeight, 6u));
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3 + 1u, kRampBase >> 5u);
  // genMode numtevstages-1 = 1 (bits 10-13), numtexgens = 2 (bits 0-3), one
  // colour channel (bits 4-6).
  push_bp(fifo, DOL_GX_BP_REG_GENMODE, 2u | (1u << 4u) | (1u << 10u));
  // Stage orders (BP 0x28 packs two stages: 0 in bits 0-11, 1 in bits 12-23;
  // texmap 3 bits, texcoord 3 bits, enable 1 bit, colour channel 3 bits).
  push_bp(fifo, 0x28u,
          (1u | (1u << 3u) | (1u << 6u)) | ((1u << 6u) << 12u));
  // Stage 0: COMBINED = ramp (TexColor=8) * raster (RASC=10), dest prev.
  push_bp(fifo, 0xC0u,
          15u | (15u << 4u) | (10u << 8u) | (8u << 12u) | (1u << 19u));
  push_bp(fifo, 0xC1u,
          7u | (7u << 4u) | (5u << 10u) | (4u << 13u) | (1u << 19u));
  // Stage 1: COMBINED = diffuse (TexColor=8) * prev (PrevColor=0), dest prev.
  push_bp(fifo, 0xC2u,
          15u | (15u << 4u) | (0u << 8u) | (8u << 12u) | (1u << 19u));
  push_bp(fifo, 0xC3u,
          7u | (7u << 4u) | (0u << 10u) | (4u << 13u) | (1u << 19u));
  push_triangle(fifo);
  return fifo;
}

// A two-vertex line list (GX command 0xA8): the primitive topology must
// survive translation as GL_LINES, not GL_TRIANGLES.
inline std::vector<std::uint8_t> build_lines_pair() {
  std::vector<std::uint8_t> fifo;
  push_common_state(fifo);
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0,
          tex_image0(kTextureWidth, kTextureHeight, 6u));
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3, kTextureBase >> 5u);
  push_draw(fifo, 0xA8u, 2u,
            {
                0.f, 0.f, 0.f, 0.f, 0.f,
                1.f, 1.f, 0.f, 1.f, 1.f,
            });
  return fifo;
}

// Blending with GX SUBTRACT (cmode0 bit 11): the equation must become
// GL_FUNC_SUBTRACT, not stay additive.
inline std::vector<std::uint8_t> build_subtract_triangle() {
  std::vector<std::uint8_t> fifo;
  push_common_state(fifo);
  push_bp(fifo, DOL_GX_BP_REG_GENMODE, (1u << 0u));
  push_bp(fifo, 0x41u, 1u | (4u << 8u) | (5u << 5u) | (1u << 3u) | (1u << 4u) |
                           (1u << 11u));
  push_bp(fifo, DOL_GX_BP_REG_PE_CONTROL, 1u);
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0,
          tex_image0(kTextureWidth, kTextureHeight, 6u));
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3, kTextureBase >> 5u);
  push_triangle(fifo);
  return fifo;
}

// Constant destination alpha (BP 0x42 enable bit 8, value 0x80) with the alpha
// update active: the shader contract is use_dst_alpha with alpha = 0x80.
inline std::vector<std::uint8_t> build_dstalpha_triangle() {
  std::vector<std::uint8_t> fifo;
  push_common_state(fifo);
  push_bp(fifo, DOL_GX_BP_REG_GENMODE, (1u << 0u));
  push_bp(fifo, 0x41u, 1u | (4u << 8u) | (5u << 5u) | (1u << 3u) | (1u << 4u));
  push_bp(fifo, DOL_GX_BP_REG_PE_CONTROL, 1u);
  push_bp(fifo, 0x42u, (1u << 8u) | 0x80u);
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0,
          tex_image0(kTextureWidth, kTextureHeight, 6u));
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3, kTextureBase >> 5u);
  push_triangle(fifo);
  return fifo;
}

// One textured draw followed by an EFB copy-to-texture: source rectangle 0,0
// 4x4, destination at kCopyBase, R8 format (trigger field 2), no half-scale,
// no clear. The sink's copy observer carries the resolved command.
inline std::vector<std::uint8_t> build_copy_triangle() {
  std::vector<std::uint8_t> fifo;
  push_common_state(fifo);
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0,
          tex_image0(kTextureWidth, kTextureHeight, 6u));
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3, kTextureBase >> 5u);
  push_bp(fifo, DOL_GX_BP_REG_EFB_TL, 0u);
  push_bp(fifo, DOL_GX_BP_REG_EFB_WH, 3u | (3u << 10u)); // width-1, height-1
  push_bp(fifo, DOL_GX_BP_REG_EFB_ADDR, kCopyBase >> 5u);
  push_bp(fifo, DOL_GX_BP_REG_TRIGGER_EFB_COPY, 2u << 3u);
  push_triangle(fifo);
  return fifo;
}

// --- Replay -----------------------------------------------------------------

using PlanObserver = void (*)(const gxc::DrawPlan& plan, void* user);

inline bool known_scenario(const std::string& name) {
  return name == "textured_triangle" || name == "ci8_triangle" ||
         name == "state_triangle" || name == "cel_triangle" ||
         name == "lines_pair" || name == "subtract_triangle" ||
         name == "dstalpha_triangle" || name == "copy_triangle";
}

// Replays the named scenario through the pinned frontend and GxCoreSink. The
// observer receives every plan; `copy_observer` (optional) receives resolved
// EFB copy commands; `counters` (optional) receives the sink's gap counters.
// `error` carries the frontend's rejection detail when the replay fails.
inline bool replay_scenario(const std::string& name, PlanObserver observer,
                            void* user, std::string* error,
                            gxc::GxCoreSink::CopyObserver copy_observer = nullptr,
                            void* copy_user = nullptr,
                            gxc::GapCounters* counters = nullptr) {
  std::vector<std::uint8_t> fifo;
  if (name == "textured_triangle")
    fifo = build_textured_triangle();
  else if (name == "ci8_triangle")
    fifo = build_ci8_triangle();
  else if (name == "state_triangle")
    fifo = build_state_triangle();
  else if (name == "cel_triangle")
    fifo = build_cel_triangle();
  else if (name == "lines_pair")
    fifo = build_lines_pair();
  else if (name == "subtract_triangle")
    fifo = build_subtract_triangle();
  else if (name == "dstalpha_triangle")
    fifo = build_dstalpha_triangle();
  else if (name == "copy_triangle")
    fifo = build_copy_triangle();
  else
    return false;

  FixtureContext ctx;
  fill_texture_zones(ctx);

  ar::RetailGxFrontend frontend;
  DolGuestAddressResolver resolver;
  dol_guest_address_resolver_init_callback(&resolver, resolve_mem1, &ctx);
  frontend.reset(&resolver);
  frontend.set_packet_drain_enabled(true);

  gxc::GxCoreSink sink;
  sink.set_guest_resolver(&frontend.state().resolver);
  sink.set_plan_observer(observer, user);
  if (copy_observer != nullptr)
    sink.set_copy_observer(copy_observer, copy_user);

  if (!frontend.replay_fifo(fifo, &sink)) {
    if (error != nullptr) {
      char detail[160];
      std::snprintf(detail, sizeof detail,
                    "%s (opcode=0x%02X offset=%zu)",
                    frontend.last_error() != nullptr ? frontend.last_error()
                                                     : "unknown",
                    static_cast<unsigned>(frontend.last_error_opcode()),
                    frontend.last_error_offset());
      *error = detail;
    }
    return false;
  }
  sink.flush_frame();
  if (counters != nullptr)
    *counters = sink.counters();
  return true;
}

} // namespace gx_fixture
