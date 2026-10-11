// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 GX replay harness.
//
// Builds documented synthetic GX command streams and replays them through the
// pinned RetailGxFrontend and GxCoreSink, printing one machine-readable line
// per decoded fact. The fixture owns every input value, so the pinned decode
// is checked against the fixture's own bytes (test_gx_replay.py).

#include "gxruntime/aurora_recomp/retail_gx_frontend.hpp"
#include "gxruntime/gxcore/gxcore.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace ar = gxruntime::aurora_recomp;
namespace gxc = gxruntime::gxcore;

namespace {

// --- Documented GX FIFO encodings (big-endian, as the game writes them) -----

void push_u8(std::vector<std::uint8_t>& fifo, std::uint8_t value) {
  fifo.push_back(value);
}

void push_u16(std::vector<std::uint8_t>& fifo, std::uint16_t value) {
  fifo.push_back(static_cast<std::uint8_t>(value >> 8u));
  fifo.push_back(static_cast<std::uint8_t>(value));
}

void push_u32(std::vector<std::uint8_t>& fifo, std::uint32_t value) {
  fifo.push_back(static_cast<std::uint8_t>(value >> 24u));
  fifo.push_back(static_cast<std::uint8_t>(value >> 16u));
  fifo.push_back(static_cast<std::uint8_t>(value >> 8u));
  fifo.push_back(static_cast<std::uint8_t>(value));
}

std::uint32_t f32_bits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void push_f32(std::vector<std::uint8_t>& fifo, float value) {
  push_u32(fifo, f32_bits(value));
}

void push_cp(std::vector<std::uint8_t>& fifo, std::uint8_t reg,
             std::uint32_t value) {
  push_u8(fifo, DOL_GX_CMD_LOAD_CP_REG);
  push_u8(fifo, reg);
  push_u32(fifo, value);
}

void push_bp(std::vector<std::uint8_t>& fifo, std::uint8_t reg,
             std::uint32_t value) {
  push_u8(fifo, DOL_GX_CMD_LOAD_BP_REG);
  push_u32(fifo, (static_cast<std::uint32_t>(reg) << 24u) |
                     (value & 0x00FFFFFFu));
}

void push_xf_block(std::vector<std::uint8_t>& fifo, std::uint16_t base,
                   const std::vector<std::uint32_t>& values) {
  push_u8(fifo, DOL_GX_CMD_LOAD_XF_REG);
  push_u32(fifo, (static_cast<std::uint32_t>(values.size() - 1u) << 16u) |
                     static_cast<std::uint32_t>(base));
  for (std::uint32_t value : values)
    push_u32(fifo, value);
}

void push_xf_f32(std::vector<std::uint8_t>& fifo, std::uint16_t base,
                 const std::vector<float>& values) {
  std::vector<std::uint32_t> words;
  words.reserve(values.size());
  for (float value : values)
    words.push_back(f32_bits(value));
  push_xf_block(fifo, base, words);
}

void push_draw(std::vector<std::uint8_t>& fifo, std::uint8_t command,
               std::uint16_t vertex_count, const std::vector<float>& data) {
  push_u8(fifo, command);
  push_u16(fifo, vertex_count);
  for (float value : data)
    push_f32(fifo, value);
}

// --- Fixture memory: one zeroed MEM1 image behind the guest resolver -------

struct FixtureContext {
  std::vector<std::uint8_t> mem1;
};

constexpr u32 kTextureBase = 0x1000u; // physical address of the fixture texture
constexpr u32 kTextureWidth = 4u;
constexpr u32 kTextureHeight = 4u;

bool resolve_mem1(void* user, u32 address, u32 size, DolGuestAddressSpace space,
                  DolGuestResourceKind resource, DolGuestResolvedRange* out) {
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

// The BP image0 payload: width-1 | (height-1) << 10 | format << 20.
std::uint32_t tex_image0(std::uint16_t width, std::uint16_t height,
                         std::uint32_t format) {
  return static_cast<std::uint32_t>(width - 1u) |
         (static_cast<std::uint32_t>(height - 1u) << 10u) |
         ((format & 0xFu) << 20u);
}

// --- Scenario: one textured triangle, direct float32 position and tex0 -----
//
// VCD_LO bits 9-10: position type 1 (direct).
// VCD_HI bits 0-1:  tex0 type 1 (direct).
// VAT g0: bit 0 = position has 3 elements, bits 1-3 = format 4 (f32),
//         bit 21 = tex0 has 2 elements, bits 22-24 = format 4 (f32).
// The texture is a 4x4 RGBA8 pattern at physical 0x1000, bound through BP
// image0/image3 (SETIMAGE3 holds address >> 5) with XF numTexGens = 1.

std::vector<std::uint8_t> build_textured_triangle() {
  std::vector<std::uint8_t> fifo;
  push_cp(fifo, DOL_GX_CP_REG_VCD_LO, 1u << 9u);
  push_cp(fifo, DOL_GX_CP_REG_VCD_HI, 1u << 0u);
  const std::uint32_t vat0 =
      (1u << 0u) | (4u << 1u) | (1u << 21u) | (4u << 22u);
  push_cp(fifo, DOL_GX_CP_REG_VAT_GRP0, vat0);
  push_cp(fifo, DOL_GX_CP_REG_VAT_GRP1, 0u);
  push_cp(fifo, DOL_GX_CP_REG_VAT_GRP2, 0u);

  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE0,
          tex_image0(kTextureWidth, kTextureHeight, 6u)); // GX_TF_RGBA8
  push_bp(fifo, DOL_GX_BP_REG_TX_SETIMAGE3, kTextureBase >> 5u);

  // Identity position matrix (XF 0x0000), a viewport, a projection and one
  // texgen so the draw carries its transform state (the plan keeps the raw
  // values).
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

  push_draw(fifo, 0x90u, 3u,
            {
                0.f, 0.f, 0.f, 0.f, 0.f,
                1.f, 0.f, 0.f, 1.f, 0.f,
                0.f, 1.f, 0.f, 0.f, 1.f,
            });
  return fifo;
}

// --- Report -----------------------------------------------------------------

void plan_observer(const gxc::DrawPlan& plan, void* user) {
  auto* out = static_cast<std::vector<std::string>*>(user);
  char line[256];
  if (!plan.ok) {
    std::snprintf(line, sizeof line, "plan ok=0 reason=%s",
                  plan.skip_reason != nullptr ? plan.skip_reason : "unknown");
    out->push_back(line);
    return;
  }
  std::snprintf(line, sizeof line, "plan ok=1 vertices=%u",
                plan.vertex_count);
  out->push_back(line);
  for (std::uint32_t i = 0; i < plan.vertex_count; ++i) {
    const float* vertex = &plan.vertices[i * gxc::kVertexFloats];
    const float* uv0 = &plan.vertices[i * gxc::kVertexFloats +
                                     gxc::kVertexUvOffset / 4u];
    std::snprintf(line, sizeof line, "v %u pos=", i);
    std::string text = line;
    char part[64];
    for (int c = 0; c < 3; ++c) {
      std::snprintf(part, sizeof part, "%s%g", c == 0 ? "" : ",", vertex[c]);
      text += part;
    }
    text += " uv0=";
    for (int c = 0; c < 2; ++c) {
      std::snprintf(part, sizeof part, "%s%g", c == 0 ? "" : ",", uv0[c]);
      text += part;
    }
    out->push_back(text);
  }
  std::string indices = "idx";
  for (std::uint16_t index : plan.indices)
    indices += " " + std::to_string(index);
  out->push_back(indices);
  std::snprintf(line, sizeof line, "pipeline textured=%u has_pos_mtx_idx=%u",
                plan.pipeline.shader.textured,
                plan.pipeline.shader.has_pos_mtx_idx);
  out->push_back(line);
  if (plan.has_texture) {
    std::snprintf(line, sizeof line, "texture format=%u width=%u height=%u",
                  plan.tex_format, plan.tex_width, plan.tex_height);
    out->push_back(line);
  }
}

int run(const std::vector<std::uint8_t>& fifo) {
  FixtureContext ctx;
  ctx.mem1.assign(24u << 20u, 0u);
  // The 4x4 RGBA8 fixture texture: byte i = i & 0xFF.
  for (std::uint32_t i = 0; i < 64u; ++i)
    ctx.mem1[kTextureBase + i] = static_cast<std::uint8_t>(i);

  ar::RetailGxFrontend frontend;
  DolGuestAddressResolver resolver;
  dol_guest_address_resolver_init_callback(&resolver, resolve_mem1, &ctx);
  frontend.reset(&resolver);
  frontend.set_packet_drain_enabled(true);

  std::vector<std::string> report;
  gxc::GxCoreSink sink;
  sink.set_guest_resolver(&frontend.state().resolver);
  sink.set_plan_observer(plan_observer, &report);

  if (!frontend.replay_fifo(fifo, &sink)) {
    std::fprintf(stderr, "frontend rejected the fixture: %s (opcode=0x%02X "
                         "offset=%zu)\n",
                 frontend.last_error() != nullptr ? frontend.last_error()
                                                  : "unknown",
                 static_cast<unsigned>(frontend.last_error_opcode()),
                 frontend.last_error_offset());
    return 1;
  }
  sink.flush_frame();

  std::printf("scenario ok\n");
  for (const std::string& line : report)
    std::printf("%s\n", line.c_str());
  const gxc::GapCounters& counters = sink.counters();
  std::printf("counters planned=%llu skipped=%llu\n",
              counters.draws_planned, counters.draws_skipped);
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: gx_replay <scenario>\n");
    return 2;
  }
  const std::string scenario = argv[1];
  if (scenario == "textured_triangle")
    return run(build_textured_triangle());
  std::fprintf(stderr, "unknown scenario: %s\n", scenario.c_str());
  return 2;
}
