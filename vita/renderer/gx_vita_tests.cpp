// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 GX -> vitaGL translation tests.
//
// Replays each fixture through the pinned frontend and checks the translation
// against the fixture's own values: the decoded geometry, the pipeline state
// in GL tokens, and texture bytes cross-checked against a direct pinned-codec
// decode of the same fixture pattern.

#include "gx_vita.h"

#include "../feasibility/gx/gx_fixture.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace gxc = gxruntime::gxcore;

namespace {

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

std::vector<std::uint8_t> fixture_rgba8_pattern() {
  std::vector<std::uint8_t> pattern(64u);
  for (std::uint32_t i = 0; i < 64u; ++i)
    pattern[i] = static_cast<std::uint8_t>(i);
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
    const std::uint16_t entry = static_cast<std::uint16_t>(0x8000u | (i * 0x421u));
    tlut[i * 2u] = static_cast<std::uint8_t>(entry >> 8u);
    tlut[i * 2u + 1u] = static_cast<std::uint8_t>(entry);
  }
  return tlut;
}

} // namespace

int main() {
  std::uint64_t textured_key = 0;

  {
    gx_vita::TranslatedDraw draw;
    assert(replay("textured_triangle", draw));
    assert(draw.ok);
    assert(draw.geometry.vertex_count == 3u);
    assert(draw.geometry.vertices.size() == 3u * gxc::kVertexStrideBytes);
    assert(draw.geometry.indices.size() == 3u);
    assert(draw.geometry.indices[0] == 0u && draw.geometry.indices[1] == 1u &&
           draw.geometry.indices[2] == 2u);
    assert(draw.pipeline.program_key != 0u);
    textured_key = draw.pipeline.program_key;

    assert(draw.texture.valid);
    assert(draw.texture.width == 4u && draw.texture.height == 4u);
    assert(draw.texture.rgba.size() == 64u);
    const std::vector<std::uint8_t> pattern = fixture_rgba8_pattern();
    const std::vector<std::uint8_t> expected =
        gxc::decode_texture(6u, 4u, 4u, pattern.data(), pattern.size());
    assert(expected.size() == draw.texture.rgba.size());
    assert(std::memcmp(expected.data(), draw.texture.rgba.data(),
                       expected.size()) == 0);
    std::printf("textured_triangle ok key=%016llx rgba=%zu\n",
                static_cast<unsigned long long>(textured_key),
                draw.texture.rgba.size());
  }

  {
    gx_vita::TranslatedDraw draw;
    assert(replay("ci8_triangle", draw));
    assert(draw.ok);
    assert(draw.texture.valid);
    assert(draw.texture.width == 8u && draw.texture.height == 8u);
    assert(draw.texture.rgba.size() == 256u);
    const std::vector<std::uint8_t> indices = fixture_ci8_indices();
    const std::vector<std::uint8_t> tlut = fixture_tlut();
    const std::vector<std::uint8_t> expected =
        gxc::decode_ci(9u, 8u, 8u, indices.data(), indices.size(), 2u, 16u,
                       tlut.data(), tlut.size());
    assert(expected.size() == draw.texture.rgba.size());
    assert(std::memcmp(expected.data(), draw.texture.rgba.data(),
                       expected.size()) == 0);
    std::printf("ci8_triangle ok rgba=%zu\n", draw.texture.rgba.size());
  }

  {
    gx_vita::TranslatedDraw draw;
    assert(replay("state_triangle", draw));
    assert(draw.ok);
    assert(draw.pipeline.depth_test == 1u);
    assert(draw.pipeline.depth_func == gx_vita::kGlLequal);
    assert(draw.pipeline.depth_write == 1u);
    assert(draw.pipeline.blend_enable == 1u);
    assert(draw.pipeline.blend_src == gx_vita::kGlSrcAlpha);
    assert(draw.pipeline.blend_dst == gx_vita::kGlOneMinusSrcAlpha);
    assert(draw.pipeline.cull_enable == 1u);
    assert(draw.pipeline.cull_face == gx_vita::kGlBack);
    assert(draw.pipeline.color_write == 1u);
    assert(draw.pipeline.alpha_write == 1u);
    std::printf("state_triangle ok depth_func=0x%04x blend=0x%04x/0x%04x\n",
                draw.pipeline.depth_func, draw.pipeline.blend_src,
                draw.pipeline.blend_dst);
  }

  {
    gx_vita::TranslatedDraw draw;
    assert(replay("cel_triangle", draw));
    assert(draw.ok);
    assert(draw.pipeline.program_key != 0u);
    assert(draw.pipeline.program_key != textured_key);
    assert(draw.texture.valid);
    std::printf("cel_triangle ok key=%016llx\n",
                static_cast<unsigned long long>(draw.pipeline.program_key));
  }

  std::printf("gx_vita: all translations ok\n");
  return 0;
}
