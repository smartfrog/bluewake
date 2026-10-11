// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 bounded Cg generation tests.
//
// The cel and textured keys come from the real fixtures through the pinned
// frontend; the unsupported and untextured keys are built by hand. Checks are
// explicit (no assert).

#include "gx_cg.h"

#include "../feasibility/gx/gx_fixture.hpp"

#include <cstdio>
#include <string>

namespace gxc = gxruntime::gxcore;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
      return 1;                                                              \
    }                                                                        \
  } while (0)

namespace {

struct KeyCapture {
  gxc::ShaderKey key{};
  bool have = false;
};

void capture_key(const gxc::DrawPlan& plan, void* user) {
  auto* capture = static_cast<KeyCapture*>(user);
  capture->key = plan.pipeline.shader;
  capture->have = true;
}

bool replay_key(const char* scenario, gxc::ShaderKey& key) {
  KeyCapture capture;
  std::string error;
  if (!gx_fixture::replay_scenario(scenario, capture_key, &capture, &error)) {
    std::fprintf(stderr, "%s: replay failed: %s\n", scenario, error.c_str());
    return false;
  }
  if (!capture.have) {
    std::fprintf(stderr, "%s: no plan\n", scenario);
    return false;
  }
  key = capture.key;
  return true;
}

bool contains(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

} // namespace

int main() {
  {
    gxc::ShaderKey key;
    CHECK(replay_key("cel_triangle", key));
    const gx_vita::CgProgram cg = gx_vita::generate_cg(key);
    CHECK(cg.ok);
    CHECK(contains(cg.vertex,
                   "mul(mul(float4(position, 1.0), position_matrix), "
                   "projection)"));
    CHECK(contains(cg.vertex, "out_color0 = color0;"));
    CHECK(contains(cg.fragment, "uniform sampler2D tex0"));
    CHECK(contains(cg.fragment, "uniform sampler2D tex1"));
    CHECK(contains(cg.fragment, "tex2D(tex1, float2(color0.r, 0.5))"));
    CHECK(contains(cg.fragment, "tex2D(tex0, uv0)"));
    CHECK(contains(cg.fragment, "return prev;"));
    std::printf("cel ok\n");
  }

  {
    gxc::ShaderKey key;
    CHECK(replay_key("textured_triangle", key));
    const gx_vita::CgProgram cg = gx_vita::generate_cg(key);
    CHECK(cg.ok);
    CHECK(contains(cg.fragment, "uniform sampler2D tex0"));
    CHECK(contains(cg.fragment, "return tex2D(tex0, uv0);"));
    CHECK(!contains(cg.fragment, "sampler2D tex1"));
    std::printf("passthrough ok\n");
  }

  {
    gxc::ShaderKey key{};
    const gx_vita::CgProgram cg = gx_vita::generate_cg(key);
    CHECK(cg.ok);
    CHECK(contains(cg.fragment, "return float4(color0.rgb, color0.a);"));
    std::printf("untextured ok\n");
  }

  {
    // An active indirect lookup must be refused with a reason.
    gxc::ShaderKey key;
    CHECK(replay_key("indirect_triangle", key));
    CHECK(key.num_ind_stages == 1u);
    CHECK(key.tev_stages[0].ind_matrix_index == 1u);
    const gx_vita::CgProgram cg = gx_vita::generate_cg(key);
    CHECK(!cg.ok);
    CHECK(cg.unsupported_reason != nullptr);
    CHECK(std::string(cg.unsupported_reason).find("indirect") !=
          std::string::npos);
    std::printf("indirect refused ok: %s\n", cg.unsupported_reason);
  }

  {
    // A KONST colour argument needs a uniform the bounded generator does not
    // emit: the program must be refused with a reason, never silently wrong.
    gxc::ShaderKey key{};
    key.textured = 1;
    key.tev_valid = 1;
    key.num_tex_gens = 1;
    key.num_tev_stages = 1;
    key.tex_gens[0].enabled = 1;
    key.tex_gens[0].texgentype = 0;
    key.tex_gens[0].sourcerow = 5;
    key.tev_stages[0].tevorders_texmap = 0;
    key.tev_stages[0].tevorders_texcoord = 0;
    key.tev_stages[0].tevorders_enable = 1;
    key.tev_stages[0].cc_a = 14; // KONST
    key.tev_stages[0].cc_clamp = 1;
    const gx_vita::CgProgram cg = gx_vita::generate_cg(key);
    CHECK(!cg.ok);
    CHECK(cg.unsupported_reason != nullptr);
    CHECK(cg.vertex.empty() || !cg.ok);
    std::printf("unsupported ok: %s\n", cg.unsupported_reason);
  }

  {
    gxc::ShaderKey key;
    CHECK(replay_key("cel_triangle", key));
    const gx_vita::CgProgram first = gx_vita::generate_cg(key);
    const gx_vita::CgProgram second = gx_vita::generate_cg(key);
    CHECK(first.ok && second.ok);
    CHECK(first.vertex == second.vertex);
    CHECK(first.fragment == second.fragment);
    gxc::ShaderKey other = key;
    other.num_tev_stages = 1;
    const gx_vita::CgProgram changed = gx_vita::generate_cg(other);
    CHECK(changed.ok);
    CHECK(changed.fragment != first.fragment);
    std::printf("determinism ok\n");
  }

  std::printf("gx_cg: all checks ok\n");
  return 0;
}
