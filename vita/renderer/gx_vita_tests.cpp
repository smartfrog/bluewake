// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 GX -> vitaGL translation tests.
//
// Replays each fixture through the pinned frontend and checks the translation
// against the fixture's own values: the decoded geometry, the pipeline state
// in GL tokens, and texture bytes checked both against golden texels (derived
// from the documented GX formats, not from the decoder) and against a direct
// pinned-codec decode.
//
// Checks are explicit (no assert): a release build must fail loudly too.

#include "gx_vita.h"

#include "../feasibility/gx/gx_fixture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gxc = gxruntime::gxcore;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
      return 1;                                                              \
    }                                                                        \
  } while (0)

namespace {

struct CopyCapture {
  gxc::EfbCopyCommand cmd{};
  int count = 0;
};

void capture_copy(const gxc::EfbCopyCommand& cmd, void* user) {
  auto* capture = static_cast<CopyCapture*>(user);
  capture->cmd = cmd;
  ++capture->count;
}

void capture(const gxc::DrawPlan& plan, void* user) {
  *static_cast<gx_vita::TranslatedDraw*>(user) = gx_vita::translate(plan);
}

bool replay(const char* scenario, gx_vita::TranslatedDraw& out) {
  std::string error;
  if (!gx_fixture::replay_scenario(scenario, capture, &out, &error)) {
    std::fprintf(stderr, "%s: replay failed: %s\n", scenario, error.c_str());
    return false;
  }
  return true;
}

std::uint8_t texel(const std::vector<std::uint8_t>& rgba, std::uint32_t width,
                   std::uint32_t x, std::uint32_t y, int channel) {
  return rgba[(static_cast<std::size_t>(y) * width + x) * 4u +
              static_cast<std::size_t>(channel)];
}

void check_texel(const std::vector<std::uint8_t>& rgba, std::uint32_t width,
                 std::uint32_t x, std::uint32_t y, std::uint8_t r,
                 std::uint8_t g, std::uint8_t b, std::uint8_t a,
                 const char* label, int& failures) {
  if (texel(rgba, width, x, y, 0) != r || texel(rgba, width, x, y, 1) != g ||
      texel(rgba, width, x, y, 2) != b || texel(rgba, width, x, y, 3) != a) {
    std::fprintf(stderr,
                 "FAIL golden texel %s(%u,%u) = (%u,%u,%u,%u), expected "
                 "(%u,%u,%u,%u)\n",
                 label, x, y, texel(rgba, width, x, y, 0),
                 texel(rgba, width, x, y, 1), texel(rgba, width, x, y, 2),
                 texel(rgba, width, x, y, 3), r, g, b, a);
    ++failures;
  }
}

std::vector<std::uint8_t> fixture_rgba8_pattern() {
  std::vector<std::uint8_t> pattern(64u);
  for (std::uint32_t i = 0; i < 64u; ++i)
    pattern[i] = static_cast<std::uint8_t>(i);
  return pattern;
}

std::vector<std::uint8_t> fixture_ramp_pattern() {
  std::vector<std::uint8_t> pattern(64u);
  for (std::uint32_t i = 0; i < 64u; ++i)
    pattern[i] = static_cast<std::uint8_t>(0xFFu - i);
  return pattern;
}

std::vector<std::uint8_t> fixture_ci8_indices() {
  std::vector<std::uint8_t> indices(64u);
  for (std::uint32_t i = 0; i < 64u; ++i)
    indices[i] = static_cast<std::uint8_t>(i % 16u);
  return indices;
}

std::vector<std::uint8_t> fixture_tlut() {
  std::vector<std::uint8_t> tlut(32u);
  for (std::uint32_t i = 0; i < 16u; ++i) {
    const std::uint16_t entry =
        static_cast<std::uint16_t>(0x8000u | (i * 0x421u));
    tlut[i * 2u] = static_cast<std::uint8_t>(entry >> 8u);
    tlut[i * 2u + 1u] = static_cast<std::uint8_t>(entry);
  }
  return tlut;
}

} // namespace

int main() {
  int failures = 0;
  std::uint64_t textured_key = 0;

  for (bool indexed : {false, true}) {
    for (bool projected : {false, true}) {
      for (std::uint32_t slot : {0, 4, 7}) {
        gxc::DrawPlan plan;
        plan.ok = true;
        plan.vertex_count = 1;
        plan.vertices.resize(gxc::kVertexFloats);
        plan.vertices[gxc::kVertexUvOffset / 4] = 2;
        plan.vertices[gxc::kVertexUvOffset / 4 + 1] = 3;
        auto& key = plan.pipeline.shader;
        key.num_tex_gens = slot + 1;
        key.uv_mask = 1;
        key.tex_gens[slot].sourcerow = 5;
        key.tex_gens[slot].projection = projected;
        float rows[3][4] = {{2, 0, 0, 5}, {0, 3, 0, -7}, {1, 0, 0, 0}};
        if (indexed) {
          key.has_tex_mtx_idx = 1;
          key.tex_mtx_idx_mask = 1u << slot;
          const std::uint32_t row = 3u << (8 * (slot % 4));
          const auto offset = slot < 4 ? gxc::kVertexTexMtxIdxOffset
                                       : gxc::kVertexTexMtxIdxHiOffset;
          std::memcpy(&plan.vertices[offset / 4], &row, sizeof row);
          std::memcpy(plan.constants.transformmatrices[3], rows, sizeof rows);
        } else {
          std::memcpy(plan.constants.texmatrices[3 * slot], rows, sizeof rows);
        }
        const auto draw = gx_vita::translate(plan);
        CHECK(draw.ok);
        CHECK(draw.geometry.texcoords.size() == 8 * 3);
        CHECK(draw.geometry.texcoords[3 * slot] == 9 &&
              draw.geometry.texcoords[3 * slot + 1] == 2);
        CHECK(draw.geometry.texcoords[3 * slot + 2] == (projected ? 2.f : 1.f));
      }
    }
  }

  {
    gxc::DrawPlan plan;
    plan.ok = true;
    plan.vertex_count = 1;
    plan.vertices.resize(gxc::kVertexFloats);
    auto& key = plan.pipeline.shader;
    key.num_tex_gens = 1;
    key.uv_mask = 1;
    key.tex_gens[0].sourcerow = 5;
    key.tex_gens[0].projection = 1;
    plan.vertices[gxc::kVertexUvOffset / 4] = 4;
    plan.vertices[gxc::kVertexUvOffset / 4 + 1] = -6;
    plan.constants.texmatrices[0][0] = plan.constants.texmatrices[1][1] = 1;
    const auto draw = gx_vita::translate(plan);
    CHECK(draw.ok);
    CHECK(draw.geometry.texcoords[0] == 1 && draw.geometry.texcoords[1] == -1);
    CHECK(draw.geometry.texcoords[2] == 0);
  }

  for (std::uint8_t source : {0, 1, 5, 10, 11, 12}) {
    gxc::DrawPlan plan;
    plan.ok = true;
    plan.vertex_count = 1;
    plan.vertices.resize(gxc::kVertexFloats);
    auto& key = plan.pipeline.shader;
    key.num_tex_gens = 1;
    key.tex_gens[0].sourcerow = source;
    key.tex_gens[0].inputform = 1;
    key.tex_gens[0].projection = 1;
    key.has_vertex_normal = source == 1;
    key.raw_tex_hi_in_nbt = source >= 10;
    key.uv_mask = 255;
    plan.vertices[0] = 2;
    plan.vertices[1] = 3;
    plan.vertices[2] = 4;
    for (auto offset : {gxc::kVertexNormalOffset, gxc::kVertexBinormalOffset,
                        gxc::kVertexTangentOffset}) {
      plan.vertices[offset / 4] = 5;
      plan.vertices[offset / 4 + 1] = 6;
      plan.vertices[offset / 4 + 2] = 7;
    }
    plan.vertices[gxc::kVertexUvOffset / 4] = 8;
    plan.vertices[gxc::kVertexUvOffset / 4 + 1] = 9;
    for (int i = 0; i < 3; ++i)
      plan.constants.texmatrices[i][i] = 1;
    const auto draw = gx_vita::translate(plan);
    CHECK(draw.ok);
    CHECK(draw.geometry.texcoords[0] == (source == 0 ? 2.f : source == 5 ? 8.f : 5.f));
    CHECK(draw.geometry.texcoords[1] == (source == 0 ? 3.f : source == 5 ? 9.f : 6.f));
    CHECK(draw.geometry.texcoords[2] == (source == 0 ? 4.f : source == 1 ? 7.f : 1.f));
  }

  {
    gxc::DrawPlan plan;
    plan.ok = true;
    plan.vertex_count = 1;
    plan.vertices.resize(gxc::kVertexFloats);
    plan.pipeline.shader.num_tex_gens = 1;
    plan.pipeline.shader.has_tex_mtx_idx = 1;
    plan.pipeline.shader.tex_mtx_idx_mask = 1;
    plan.pipeline.shader.tex_gens[0].projection = 1;
    const std::uint32_t row = 63;
    std::memcpy(&plan.vertices[gxc::kVertexTexMtxIdxOffset / 4], &row, sizeof row);
    const auto draw = gx_vita::translate(plan);
    CHECK(!draw.ok && std::string(draw.skip_reason) == "texture matrix row out of range");
  }

  for (std::uint8_t channel : {0, 1}) {
    gxc::DrawPlan plan;
    plan.ok = true;
    plan.vertex_count = 1;
    plan.vertices.resize(gxc::kVertexFloats);
    plan.pipeline.shader.num_tex_gens = 1;
    plan.pipeline.shader.tex_gens[0].texgentype = 2 + channel;
    const auto offset = channel == 0 ? gxc::kVertexColor0Offset : gxc::kVertexColor1Offset;
    plan.vertices[offset / 4] = 0.25f;
    plan.vertices[offset / 4 + 1] = 0.75f;
    const auto draw = gx_vita::translate(plan);
    CHECK(draw.ok);
    CHECK(draw.geometry.texcoords[0] == 0.25f && draw.geometry.texcoords[1] == 0.75f);
  }

  {
    gxc::DrawPlan plan;
    plan.ok = true;
    plan.pixel_constants.zbias[3] = 0xFFFFFF;
    plan.viewport_valid = true;
    plan.viewport[2] = 0.5f * 16777216.f;
    plan.viewport[5] = 0.75f * 16777216.f;
    const auto draw = gx_vita::translate(plan);
    CHECK(draw.ok);
    CHECK(draw.ztexture_bias[0] == 65535 && draw.ztexture_bias[1] == 255);
    CHECK(draw.pipeline.depth_near == 0.25f && draw.pipeline.depth_far == 0.75f);
  }

  {
    gxc::DrawPlan plan;
    plan.ok = true;
    plan.vertex_count = 3;
    plan.vertices.resize(3 * gxc::kVertexFloats);
    plan.pipeline.src_factor = 2;
    plan.pipeline.dst_factor = 2;
    const auto draw = gx_vita::translate(plan);
    CHECK(draw.ok);
    CHECK(draw.pipeline.blend_src == gx_vita::kGlDstColor);
    CHECK(draw.pipeline.blend_dst == gx_vita::kGlSrcColor);
  }

  {
    gx_fixture::FixtureContext ctx;
    gx_fixture::fill_texture_zones(ctx);
    DolGuestAddressResolver resolver;
    dol_guest_address_resolver_init_callback(&resolver, gx_fixture::resolve_mem1, &ctx);
    gx_fixture::ar::RetailGxFrontend frontend;
    frontend.reset(&resolver);
    frontend.set_packet_drain_enabled(true);
    gxc::GxCoreSink sink;
    sink.set_guest_resolver(&frontend.state().resolver);
    gx_vita::TranslatedDraw draw;
    sink.set_plan_observer(capture, &draw);
    std::vector<std::uint8_t> fifo;
    gx_fixture::push_common_state(fifo);
    gx_fixture::push_bp(fifo, 0x8B, gx_fixture::tex_image0(4, 4, 6));
    gx_fixture::push_bp(fifo, 0x97, gx_fixture::kTextureBase >> 5);
    gx_fixture::push_bp(fifo, 0x28, 3u | (1u << 6));
    gx_fixture::push_bp(fifo, 0xC0, 8u | (15u << 4) | (15u << 8) |
                                  (15u << 12) | (1u << 19));
    gx_fixture::push_bp(fifo, 0xC1, (4u << 4) | (7u << 7) | (7u << 10) |
                                  (7u << 13) | (1u << 19));
    gx_fixture::push_triangle(fifo);
    CHECK(frontend.replay_fifo(fifo, &sink));
    sink.flush_frame();
    CHECK(draw.ok);
    CHECK(draw.texmap_mask == (1u << 3));
    CHECK(draw.textures[3].valid && draw.textures[3].width == 4);
  }

  {
    gxc::DrawPlan plan;
    plan.ok = true;
    plan.vertex_count = 1;
    plan.vertices.resize(gxc::kVertexFloats);
    plan.pipeline.shader.has_pos_mtx_idx = 1;
    const std::uint32_t row = 63;
    std::memcpy(&plan.vertices[gxc::kVertexPosMtxOffset / 4], &row, sizeof row);
    const auto draw = gx_vita::translate(plan);
    CHECK(!draw.ok);
    CHECK(std::string(draw.skip_reason) == "position matrix row out of range");
  }

  {
    gx_vita::TranslatedDraw draw;
    CHECK(replay("textured_triangle", draw));
    CHECK(draw.ok);
    CHECK(draw.geometry.vertex_count == 3u);
    CHECK(draw.geometry.vertices.size() == 3u * gxc::kVertexStrideBytes);
    CHECK(draw.geometry.indices.size() == 3u);
    CHECK(draw.geometry.indices[0] == 0u && draw.geometry.indices[1] == 1u &&
          draw.geometry.indices[2] == 2u);
    CHECK(draw.pipeline.program_key != 0u);
    CHECK(draw.pipeline.topology == gx_vita::kGlTriangles);
    textured_key = draw.pipeline.program_key;

    CHECK(draw.texture.valid);
    CHECK(draw.texmap_mask == 0u);
    CHECK(draw.texture.width == 4u && draw.texture.height == 4u);
    CHECK(draw.texture.rgba.size() == 64u);
    // Golden texels from the documented GX_TF_RGBA8 layout (4x4 block, AR
    // plane then GB plane) applied to the fixture's byte pattern.
    check_texel(draw.texture.rgba, 4u, 0u, 0u, 1u, 32u, 33u, 0u, "rgba8",
                failures);
    check_texel(draw.texture.rgba, 4u, 1u, 0u, 3u, 34u, 35u, 2u, "rgba8",
                failures);
    // Cross-check: the same bytes as a direct pinned-codec decode.
    const std::vector<std::uint8_t> pattern = fixture_rgba8_pattern();
    const std::vector<std::uint8_t> expected =
        gxc::decode_texture(6u, 4u, 4u, pattern.data(), pattern.size());
    CHECK(expected.size() == draw.texture.rgba.size());
    CHECK(std::memcmp(expected.data(), draw.texture.rgba.data(),
                      expected.size()) == 0);
    std::printf("textured_triangle ok key=%016llx rgba=%zu\n",
                static_cast<unsigned long long>(textured_key),
                draw.texture.rgba.size());
  }

  {
    gx_vita::TranslatedDraw draw;
    CHECK(replay("ci8_triangle", draw));
    CHECK(draw.ok);
    CHECK(draw.texture.valid);
    CHECK(draw.texture.width == 8u && draw.texture.height == 8u);
    CHECK(draw.texture.rgba.size() == 256u);
    // Golden texels: palette entry i = 0x8000 | i * 0x421 (RGB555, opaque),
    // expanded 5->8 bits; indices are 0, 1, 2 at the first texels.
    check_texel(draw.texture.rgba, 8u, 0u, 0u, 0u, 0u, 0u, 255u, "ci8",
                failures);
    check_texel(draw.texture.rgba, 8u, 1u, 0u, 8u, 8u, 8u, 255u, "ci8",
                failures);
    check_texel(draw.texture.rgba, 8u, 2u, 0u, 16u, 16u, 16u, 255u, "ci8",
                failures);
    const std::vector<std::uint8_t> indices = fixture_ci8_indices();
    const std::vector<std::uint8_t> tlut = fixture_tlut();
    const std::vector<std::uint8_t> expected =
        gxc::decode_ci(9u, 8u, 8u, indices.data(), indices.size(), 2u, 16u,
                       tlut.data(), tlut.size());
    CHECK(expected.size() == draw.texture.rgba.size());
    CHECK(std::memcmp(expected.data(), draw.texture.rgba.data(),
                      expected.size()) == 0);
    std::printf("ci8_triangle ok rgba=%zu\n", draw.texture.rgba.size());
  }

  {
    gx_vita::TranslatedDraw draw;
    CHECK(replay("state_triangle", draw));
    CHECK(draw.ok);
    CHECK(draw.pipeline.depth_test == 1u);
    CHECK(draw.pipeline.depth_func == gx_vita::kGlLequal);
    CHECK(draw.pipeline.depth_write == 1u);
    CHECK(draw.pipeline.blend_enable == 1u);
    CHECK(draw.pipeline.blend_src == gx_vita::kGlSrcAlpha);
    CHECK(draw.pipeline.blend_dst == gx_vita::kGlOneMinusSrcAlpha);
    CHECK(draw.pipeline.blend_src_alpha == gx_vita::kGlSrcAlpha);
    CHECK(draw.pipeline.blend_dst_alpha == gx_vita::kGlOneMinusSrcAlpha);
    CHECK(draw.pipeline.blend_equation == gx_vita::kGlFuncAdd);
    CHECK(draw.pipeline.use_dst_alpha == 0u);
    CHECK(draw.pipeline.cull_enable == 1u);
    CHECK(draw.pipeline.cull_face == gx_vita::kGlBack);
    CHECK(draw.pipeline.front_face == gx_vita::kGlCcw);
    CHECK(draw.pipeline.color_write == 1u);
    CHECK(draw.pipeline.alpha_write == 1u);
    std::printf("state_triangle ok depth_func=0x%04x blend=0x%04x/0x%04x\n",
                draw.pipeline.depth_func, draw.pipeline.blend_src,
                draw.pipeline.blend_dst);
  }

  {
    gx_vita::TranslatedDraw draw;
    CHECK(replay("cel_triangle", draw));
    CHECK(draw.ok);
    CHECK(draw.pipeline.program_key != 0u);
    CHECK(draw.pipeline.program_key != textured_key);
    // Two texmaps are sampled: the flat single texture must NOT be used, and
    // both per-texmap uploads must carry their image.
    CHECK(draw.texmap_mask == 0x3u);
    CHECK(!draw.texture.valid);
    CHECK(draw.textures[0].valid);
    CHECK(draw.textures[1].valid);
    CHECK(draw.textures[0].rgba.size() == 64u);
    CHECK(draw.textures[1].rgba.size() == 64u);
    const std::vector<std::uint8_t> ramp = fixture_ramp_pattern();
    const std::vector<std::uint8_t> expected_ramp =
        gxc::decode_texture(6u, 4u, 4u, ramp.data(), ramp.size());
    CHECK(std::memcmp(expected_ramp.data(), draw.textures[1].rgba.data(),
                      expected_ramp.size()) == 0);
    std::printf("cel_triangle ok key=%016llx texmap_mask=0x%x\n",
                static_cast<unsigned long long>(draw.pipeline.program_key),
                draw.texmap_mask);
  }

  {
    gx_vita::TranslatedDraw draw;
    CHECK(replay("lines_pair", draw));
    CHECK(draw.ok);
    CHECK(draw.pipeline.topology == gx_vita::kGlLines);
    CHECK(draw.geometry.vertex_count == 2u);
    CHECK(draw.geometry.indices.size() == 2u);
    CHECK(draw.geometry.indices[0] == 0u && draw.geometry.indices[1] == 1u);
    std::printf("lines_pair ok topology=0x%04x\n", draw.pipeline.topology);
  }

  {
    gx_vita::TranslatedDraw draw;
    CHECK(replay("subtract_triangle", draw));
    CHECK(draw.ok);
    CHECK(draw.pipeline.blend_enable == 1u);
    CHECK(draw.pipeline.blend_equation == gx_vita::kGlFuncSubtract);
    std::printf("subtract_triangle ok equation=0x%04x\n",
                draw.pipeline.blend_equation);
  }

  {
    gx_vita::TranslatedDraw draw;
    CHECK(replay("dstalpha_triangle", draw));
    CHECK(draw.ok);
    CHECK(draw.pipeline.use_dst_alpha == 1u);
    CHECK(draw.pipeline.dst_alpha == 0x80u);
    CHECK(draw.pipeline.blend_src_alpha == gx_vita::kGlOne);
    CHECK(draw.pipeline.blend_dst_alpha == gx_vita::kGlZero);
    std::printf("dstalpha_triangle ok dst_alpha=0x%02x\n",
                draw.pipeline.dst_alpha);
  }

  {
    // EFB copy-to-texture: the sink's copy observer carries the resolved
    // command (destination, R8 format, source rectangle).
    gx_vita::TranslatedDraw draw;
    CopyCapture copy;
    std::string error;
    CHECK(gx_fixture::replay_scenario("copy_triangle", capture, &draw, &error,
                                      capture_copy, &copy));
    CHECK(draw.ok);
    CHECK(copy.count == 1);
    CHECK(copy.cmd.dest_address == gx_fixture::kCopyBase);
    CHECK(copy.cmd.format == 0x28u); // GX_CTF_R8
    // 4x4 at 8 bits/texel: one 8x4 source block, 32 bytes.
    CHECK(copy.cmd.byte_size == 32u);
    CHECK(copy.cmd.width == 4u && copy.cmd.height == 4u);
    CHECK(copy.cmd.src_x == 0u && copy.cmd.src_y == 0u);
    const gx_vita::TranslatedCopy translated = gx_vita::translate_copy(copy.cmd);
    CHECK(translated.ok);
    CHECK(translated.dest_address == gx_fixture::kCopyBase);
    CHECK(translated.format == 0x28u);
    CHECK(translated.byte_size == 32u);
    CHECK(translated.width == 4u && translated.height == 4u);
    CHECK(translated.dest_width == 4u && translated.dest_height == 4u);
    CHECK(!translated.clear);
    CHECK(translated.color_update && translated.alpha_update &&
          translated.depth_update);
    std::printf("copy_triangle ok dest=0x%X format=0x%X %ux%u\n",
                copy.cmd.dest_address, copy.cmd.format, copy.cmd.width,
                copy.cmd.height);
  }

  {
    // A display copy has no texture destination: the translation must refuse
    // it with a reason instead of producing an empty copy.
    gxc::EfbCopyCommand display{};
    display.format = 0xFu;
    const gx_vita::TranslatedCopy translated =
        gx_vita::translate_copy(display);
    CHECK(!translated.ok);
    CHECK(translated.skip_reason != nullptr);
    std::printf("display copy refused ok: %s\n", translated.skip_reason);
  }

  {
    gx_vita::TranslatedDraw draw;
    CHECK(replay("hud_quad", draw));
    CHECK(draw.ok);
    CHECK(draw.geometry.vertex_count == 4u);
    CHECK(draw.geometry.indices.size() == 6u);
    CHECK(draw.geometry.indices[0] == 0u && draw.geometry.indices[1] == 1u &&
          draw.geometry.indices[2] == 2u && draw.geometry.indices[3] == 2u &&
          draw.geometry.indices[4] == 3u && draw.geometry.indices[5] == 0u);
    CHECK(draw.pipeline.topology == gx_vita::kGlTriangles);
    CHECK(draw.pipeline.depth_test == 0u);
    CHECK(draw.pipeline.blend_enable == 1u);
    CHECK(draw.texture.valid);
    std::printf("hud_quad ok vertices=%u indices=%zu\n",
                draw.geometry.vertex_count, draw.geometry.indices.size());
  }

  {
    // EFB readback: a 2x2 RGBA8 rectangle copied into a wider guest stride,
    // with the dirty-mark spans reported per row and the bounds refusal.
    const std::uint8_t pixels[16] = {
        1, 2, 3, 4, 5, 6, 7, 8,        // row 0
        9, 10, 11, 12, 13, 14, 15, 16, // row 1
    };
    std::uint8_t guest[32];
    std::memset(guest, 0xEE, sizeof guest);
    gx_vita::EfbReadback rect{};
    rect.width = 2u;
    rect.height = 2u;
    struct MarkState {
      int count = 0;
      std::uint32_t offsets[4] = {};
      std::uint32_t sizes[4] = {};
    };
    MarkState marks;
    auto mark = [](void* user, std::uint32_t offset, std::uint32_t size) {
      auto* state = static_cast<MarkState*>(user);
      if (state->count < 4) {
        state->offsets[state->count] = offset;
        state->sizes[state->count] = size;
      }
      ++state->count;
    };
    CHECK(gx_vita::copy_readback_to_guest(pixels, rect, 16u, guest,
                                          sizeof guest, mark, &marks));
    CHECK(marks.count == 2);
    CHECK(marks.offsets[0] == 0u && marks.sizes[0] == 8u);
    CHECK(marks.offsets[1] == 16u && marks.sizes[1] == 8u);
    CHECK(guest[0] == 1u && guest[7] == 8u);
    CHECK(guest[16] == 9u && guest[23] == 16u);
    CHECK(guest[8] == 0xEEu && guest[15] == 0xEEu); // stride gap untouched
    // 2 rows need 16 + 8 = 24 bytes: 20 is refused and writes nothing.
    CHECK(!gx_vita::copy_readback_to_guest(pixels, rect, 16u, guest, 20u,
                                           nullptr, nullptr));
    gx_vita::EfbReadback empty{};
    CHECK(!gx_vita::copy_readback_to_guest(pixels, empty, 16u, guest,
                                           sizeof guest));
    std::printf("readback ok rows=2 marks=%d\n", marks.count);
  }

  {
    // Golden hash: pinned for the pinned ShaderKey layout, so a pin change
    // that moves the key bytes fails here instead of silently re-keying Cg
    // programs. The zero key's hash and three distinct variants must differ.
    gxc::ShaderKey zero{};
    const std::uint64_t zero_hash = gx_vita::shader_key_hash(zero);
    std::printf("shader_key_hash zero=%016llx sizeof=%zu\n",
                static_cast<unsigned long long>(zero_hash), sizeof(zero));
    // Pinned golden: the pinned ShaderKey layout (RecompCore ddd031b) is 844
    // bytes and the zero key hashes to this value. A pin change that moves the
    // key bytes must fail here instead of silently re-keying Cg programs.
    CHECK(sizeof(gxc::ShaderKey) == 844u);
    CHECK(zero_hash == 0xff540a73510ed19bULL);
    gxc::ShaderKey variant = zero;
    variant.num_tex_gens = 1;
    gxc::ShaderKey variant2 = zero;
    variant2.textured = 1;
    gxc::ShaderKey variant3 = zero;
    variant3.tev_valid = 1;
    const std::uint64_t hashes[3] = {gx_vita::shader_key_hash(variant),
                                     gx_vita::shader_key_hash(variant2),
                                     gx_vita::shader_key_hash(variant3)};
    for (int i = 0; i < 3; ++i) {
      CHECK(hashes[i] != zero_hash);
      for (int j = i + 1; j < 3; ++j)
        CHECK(hashes[i] != hashes[j]);
    }
  }

  if (failures != 0) {
    std::fprintf(stderr, "gx_vita: %d golden texel failures\n", failures);
    return 1;
  }
  std::printf("gx_vita: all translations ok\n");
  return 0;
}
