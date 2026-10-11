// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 GX replay harness: replays a synthetic fixture (gx_fixture.hpp)
// and prints one machine-readable line per decoded fact, checked by
// test_gx_replay.py.

#include "gx_fixture.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace gxc = gxruntime::gxcore;

namespace {

void plan_observer(const gxc::DrawPlan& plan, void* user) {
  auto* out = static_cast<std::vector<std::string>*>(user);
  char line[512];
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
  std::snprintf(line, sizeof line,
                "pipeline textured=%u has_pos_mtx_idx=%u cull_mode=%u "
                "depth_test=%u depth_func=%u depth_update=%u blend_enable=%u "
                "blend_subtract=%u src_factor=%u dst_factor=%u color_update=%u "
                "alpha_update=%u topology=%u texmap_mask=%u texgens=%u litchan0=%u",
                plan.pipeline.shader.textured,
                plan.pipeline.shader.has_pos_mtx_idx,
                plan.pipeline.cull_mode, plan.pipeline.depth_test,
                plan.pipeline.depth_func, plan.pipeline.depth_update,
                plan.pipeline.blend_enable, plan.pipeline.blend_subtract,
                plan.pipeline.src_factor,
                plan.pipeline.dst_factor, plan.pipeline.color_update,
                plan.pipeline.alpha_update, plan.pipeline.primitive_topology,
                plan.texmap_mask,
                plan.pipeline.shader.num_tex_gens,
                plan.pipeline.shader.litchan[0].enablelighting);
  out->push_back(line);
  for (std::uint32_t t = 0; t < plan.pipeline.shader.num_tex_gens; ++t) {
    const gxc::TexGenKey& texgen = plan.pipeline.shader.tex_gens[t];
    std::snprintf(line, sizeof line, "texgen %u type=%u sourcerow=%u enabled=%u",
                  t, texgen.texgentype, texgen.sourcerow, texgen.enabled);
    out->push_back(line);
  }
  if (plan.pipeline.shader.tev_valid != 0u) {
    std::snprintf(line, sizeof line, "tev stages=%u",
                  plan.pipeline.shader.num_tev_stages);
    out->push_back(line);
    for (std::uint32_t n = 0; n < plan.pipeline.shader.num_tev_stages; ++n) {
      const gxc::TevStageKey& stage = plan.pipeline.shader.tev_stages[n];
      std::snprintf(line, sizeof line,
                    "tevstage %u texmap=%u texcoord=%u enable=%u",
                    n, stage.tevorders_texmap, stage.tevorders_texcoord,
                    stage.tevorders_enable);
      out->push_back(line);
    }
  }
  if (plan.texmap_mask != 0u) {
    for (std::uint32_t t = 0; t < 8u; ++t) {
      if ((plan.texmap_mask & (1u << t)) == 0u)
        continue;
      const gxc::PlanTexture& texture = plan.textures[t];
      std::snprintf(line, sizeof line,
                    "texture texmap=%u valid=%u format=%u width=%u height=%u",
                    t, texture.valid ? 1u : 0u, texture.format, texture.width,
                    texture.height);
      out->push_back(line);
    }
  } else if (plan.has_texture) {
    std::snprintf(line, sizeof line, "texture format=%u width=%u height=%u",
                  plan.tex_format, plan.tex_width, plan.tex_height);
    out->push_back(line);
    if (plan.has_tlut) {
      std::snprintf(line, sizeof line, "tlut format=%u entries=%u",
                    plan.tlut_format, plan.tlut_entries);
      out->push_back(line);
    }
  }
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: gx_replay <scenario>\n");
    return 2;
  }
  const std::string scenario = argv[1];
  if (!gx_fixture::known_scenario(scenario)) {
    std::fprintf(stderr, "unknown scenario: %s\n", scenario.c_str());
    return 2;
  }
  std::vector<std::string> report;
  std::string error;
  if (!gx_fixture::replay_scenario(scenario, plan_observer, &report, &error)) {
    std::fprintf(stderr, "frontend rejected the fixture: %s\n", error.c_str());
    return 1;
  }
  std::printf("scenario ok\n");
  for (const std::string& line : report)
    std::printf("%s\n", line.c_str());
  return 0;
}
