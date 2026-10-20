// SPDX-License-Identifier: GPL-3.0-or-later
#include "gx_draw_census.h"
#include "gx_vita.h"
#include "gx_texgen.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
  return 1; } } while (0)

namespace {
namespace gxc = gxruntime::gxcore;
namespace census = gx_vita::census;

struct Fixture {
  gxc::DrawPlan plan;
  gx_vita::TranslatedDraw draw;
  Fixture() {
    plan.ok = draw.ok = true;
    plan.vertex_count = draw.geometry.vertex_count = 4;
    plan.has_texture = true;
    plan.tex_address = 0x123400;
    plan.tex_width = 376;
    plan.tex_height = 104;
    plan.tex_format = 3;
    auto& key = plan.pipeline.shader;
    key.textured = 1;
    key.tev_valid = 1;
    key.num_tex_gens = key.num_tev_stages = 1;
    key.tev_stages[0].tevorders_enable = 1;
    draw.geometry.indices = {0, 1, 2, 0, 2, 3};
    draw.geometry.vertices.resize(4 * gxc::kVertexStrideBytes);
    draw.geometry.texcoords.resize(4 * gx_vita::texgen::kFloatsPerVertex);
    for (unsigned i = 0; i < 4; ++i) {
      const float position[] = {i == 0 || i == 3 ? -1.f : 1.f,
                                i < 2 ? -1.f : 1.f, 0.f};
      std::memcpy(draw.geometry.vertices.data() + i * gxc::kVertexStrideBytes,
                  position, sizeof position);
      auto* uv = draw.geometry.texcoords.data() + i * gx_vita::texgen::kFloatsPerVertex;
      uv[0] = (position[0] + 1) / 2;
      uv[1] = (position[1] + 1) / 2;
      uv[2] = 1;
    }
    for (unsigned i = 0; i < 4; ++i)
      draw.position_matrix[5 * i] = draw.projection[5 * i] = 1;
    draw.texture.valid = true;
  }
};

int describes_draw(const std::string& scenario) {
  Fixture fixture;
  if (scenario == "ci8") fixture.plan.tex_format = 9;
  if (scenario == "invalid-texture") fixture.draw.texture.valid = false;
  if (scenario == "depth-test") fixture.draw.pipeline.depth_test = 1;
  if (scenario == "depth-write") fixture.draw.pipeline.depth_write = 1;
  if (scenario == "rgba8") fixture.plan.tex_format = 6;
  if (scenario == "untextured") fixture.plan.pipeline.shader.textured = 0;
  if (scenario == "triangle") fixture.draw.geometry.vertex_count = 3;
  if (scenario == "uv-range") fixture.draw.geometry.texcoords[0] = 1.01f;
  if (scenario == "uv-nan") fixture.draw.geometry.texcoords[0] = std::numeric_limits<float>::quiet_NaN();
  if (scenario == "uv-q-zero") {
    fixture.plan.pipeline.shader.tex_gens[0].projection = 1;
    fixture.draw.geometry.texcoords[2] = 0;
  }
  if (scenario == "nonzero-texmap") {
    fixture.plan.pipeline.shader.tev_stages[0].tevorders_texmap = 3;
    fixture.plan.texmap_mask = fixture.draw.texmap_mask = 8;
    fixture.plan.textures[3].address = 0x456780;
    fixture.plan.textures[3].width = 320;
    fixture.plan.textures[3].height = 240;
    fixture.plan.textures[3].format = 3;
    fixture.draw.textures[3].valid = true;
  }
  const auto info = census::describe(fixture.plan, fixture.draw);
  CHECK(info.required_mask == (scenario == "untextured" ? 0u : scenario == "nonzero-texmap" ? 8u : 1u));
  if (!info.required_mask) return 0;
  const auto& entry = info.entries[scenario == "nonzero-texmap" ? 3 : 0];
  CHECK(entry.four_vertices == (scenario != "triangle"));
  CHECK(entry.depth_test == (scenario == "depth-test"));
  CHECK(entry.depth_write == (scenario == "depth-write"));
  if (scenario == "nonzero-texmap") {
    CHECK(entry.slot == 3);
    CHECK(entry.texture == (census::TextureIdentity{0x456780, 320, 240, 3}));
  } else {
    CHECK(entry.texture == (census::TextureIdentity{0x123400, 376, 104, fixture.plan.tex_format}));
  }
  if (scenario == "invalid-texture") CHECK(info.decode_failed_mask == 1);
  return 0;
}

int fullscreen(const std::string& scenario) {
  Fixture fixture;
  if (scenario == "partial") fixture.draw.projection[0] = 0.5f;
  if (scenario == "bad-w") fixture.draw.projection[15] = 0;
  CHECK(census::describe(fixture.plan, fixture.draw).fullscreen == (scenario == "full"));
  return 0;
}

int counts_draw(const std::string& scenario) {
  census::Counts counts;
  census::DrawInfo info;
  info.is_quad = true;
  unsigned valid = 0, skipped = 0;
  if (scenario == "textured") { info.required_mask = valid = 1; }
  if (scenario == "other") info.is_quad = false;
  if (scenario == "fullscreen") info.fullscreen = true;
  if (scenario == "missing") { info.required_mask = info.decode_failed_mask = skipped = 1; }
  if (scenario == "partial-bind") { info.required_mask = 9; valid = 1; skipped = 8; }
  if (scenario == "textured-other") { info.is_quad = false; info.required_mask = valid = 1; }
  census::count_draw(counts, info, valid, skipped);
  CHECK(counts.textured_quad == (info.is_quad && info.required_mask != 0));
  CHECK(counts.untextured_quad == (info.is_quad && info.required_mask == 0));
  CHECK(counts.other == !info.is_quad);
  CHECK(counts.fullscreen == info.fullscreen);
  CHECK(counts.no_valid_texture == (scenario == "missing"));
  CHECK(counts.incomplete_texture == (skipped != 0));
  CHECK(counts.decode_failed == (scenario == "missing"));
  CHECK(counts.required_skipped == (skipped != 0));
  CHECK(counts.textured == (info.required_mask != 0));
  return 0;
}

int formats_inventory(const std::string& scenario) {
  census::Counts counts;
  if (scenario == "empty") {
    CHECK(census::format_frame(counts, 313, false) ==
        "[draw-census] present=313 partial=0 tq=0 uq=0 other=0 tx=0 rest=0 refused=0 "
        "no_valid=0 decode_failed=0 bind_valid=0 bind_skipped=0 "
        "invalid=0 skip=0 bindfail=0 uploadfail=0 draws=-");
    return 0;
  }
  Fixture fixture;
  if (scenario == "all-fields") {
    fixture.draw.pipeline.depth_test = fixture.draw.pipeline.depth_write = 1;
    census::count_draw(counts, census::describe(fixture.plan, fixture.draw), 1, 0);
    CHECK(census::format_frame(counts, 314, true) ==
        "[draw-census] present=314 partial=1 tq=1 uq=0 other=0 tx=1 rest=0 refused=0 "
        "no_valid=0 decode_failed=0 bind_valid=0 bind_skipped=0 "
        "invalid=0 skip=0 bindfail=0 uploadfail=0 draws=00123400@0,376x104,3,1,1,1,1*1");
    return 0;
  }
  if (scenario == "maximum-fields") {
    census::DrawInfo info;
    info.required_mask = 1;
    auto& entry = info.entries[0];
    entry.texture = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    for (unsigned i = 0; i < census::kMaxEntries; ++i) {
      entry.slot = i;
      census::count_draw(counts, info, 1, 0);
    }
    CHECK(census::format_frame(counts, UINT32_MAX, true).find(
        "ffffffff@7,4294967295x4294967295,4294967295,0,0,0,0*1") != std::string::npos);
    return 0;
  }
  for (unsigned i = 0; i < 12; ++i) {
    fixture.plan.tex_address = i;
    census::count_draw(counts, census::describe(fixture.plan, fixture.draw), 1, 0);
  }
  CHECK(counts.entry_count == 8 && counts.omitted == 4);
  if (scenario == "repeat-after-full") {
    fixture.plan.tex_address = 0;
    census::count_draw(counts, census::describe(fixture.plan, fixture.draw), 1, 0);
    CHECK(counts.entries[0].count == 2 && counts.omitted == 4);
  }
  CHECK(census::format_frame(counts, 315, false).find("|00000007@0,376x104,3,1,1,0,0*1") != std::string::npos);
  CHECK(census::format_frame(counts, 315, false).find("00000008@") == std::string::npos);
  return 0;
}

int counts_bind(const std::string& scenario) {
  census::Counts counts;
  const auto status = scenario == "valid" ? census::BindStatus::Valid :
      scenario == "skipped" ? census::BindStatus::Skipped :
      scenario == "bind-failed" ? census::BindStatus::BindFailed : census::BindStatus::UploadFailed;
  census::count_bind(counts, status);
  CHECK(counts.bind_valid == (scenario == "valid"));
  CHECK(counts.bind_skipped == (scenario == "skipped"));
  CHECK(counts.bind_failed == (scenario == "bind-failed"));
  CHECK(counts.upload_failed == (scenario == "upload-failed"));
  return 0;
}
} // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const std::string group = argv[1], scenario = argv[2];
  if (group == "describe") return describes_draw(scenario);
  if (group == "format") return formats_inventory(scenario);
  if (group == "fullscreen") return fullscreen(scenario);
  if (group == "draw") return counts_draw(scenario);
  if (group == "bind") return counts_bind(scenario);
  return 2;
}
