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

gxc::ShaderKey tev_key() {
  gxc::ShaderKey key{};
  key.tev_valid = 1;
  key.num_tev_stages = 1;
  auto& stage = key.tev_stages[0];
  stage.cc_a = stage.cc_b = stage.cc_c = stage.cc_d = 15;
  stage.ac_a = stage.ac_b = stage.ac_c = stage.ac_d = 7;
  stage.cc_clamp = stage.ac_clamp = 1;
  for (int i = 0; i < 4; ++i)
    stage.ras_swap[i] = stage.tex_swap[i] = i;
  return key;
}

int tev_argument_coverage() {
  const char* color[] = {"prev.rgb", "prev.aaa", "c0.rgb", "c0.aaa",
                        "c1.rgb", "c1.aaa", "c2.rgb", "c2.aaa",
                        "tex.rgb", "tex.aaa", "ras.rgb", "ras.aaa",
                        "float3(255.0", "float3(128.0", "konst.rgb", "float3(0.0"};
  for (std::uint8_t arg = 0; arg < 16; ++arg) {
    auto key = tev_key();
    key.tev_stages[0].cc_d = arg;
    const auto cg = gx_vita::generate_cg(key);
    CHECK(cg.ok);
    CHECK(contains(cg.fragment, color[arg]));
  }
  const char* alpha[] = {"prev.a", "c0.a", "c1.a", "c2.a",
                        "tex.a", "ras.a", "konst.a", "0.0"};
  for (std::uint8_t arg = 0; arg < 8; ++arg) {
    auto key = tev_key();
    key.tev_stages[0].ac_d = arg;
    const auto cg = gx_vita::generate_cg(key);
    CHECK(cg.ok);
    CHECK(contains(cg.fragment, alpha[arg]));
  }
  return 0;
}

int tev_arithmetic_coverage() {
  const char* scale[] = {"1.0", "2.0", "4.0", "0.5"};
  for (std::uint8_t bias = 0; bias < 3; ++bias) {
    for (std::uint8_t s = 0; s < 4; ++s) {
      auto key = tev_key();
      key.tev_stages[0].cc_bias = bias;
      key.tev_stages[0].cc_scale = s;
      key.tev_stages[0].ac_scale = (s + 1) % 4;
      const auto cg = gx_vita::generate_cg(key);
      CHECK(cg.ok);
      CHECK(contains(cg.fragment, scale[s]));
      CHECK(contains(cg.fragment, "floor("));
    }
  }
  auto key = tev_key();
  key.tev_stages[0].cc_dest = 3;
  key.tev_stages[0].ac_dest = 2;
  key.tev_stages[0].cc_clamp = 0;
  key.tev_stages[0].ac_op = 1;
  const auto cg = gx_vita::generate_cg(key);
  CHECK(cg.ok);
  CHECK(contains(cg.fragment, "c2.rgb = clamp("));
  CHECK(contains(cg.fragment, "c1.a = clamp("));
  CHECK(contains(cg.fragment, "-1024.0, 1023.0"));
  CHECK(contains(cg.fragment, "prev = float4(c2.rgb, c1.a)"));
  CHECK(contains(cg.fragment, "127.0"));
  return 0;
}

int tev_konst_and_swap_coverage() {
  auto key = tev_key();
  auto& stage = key.tev_stages[0];
  stage.cc_d = 14;
  stage.ac_d = 6;
  stage.ksel_kc = 14;
  stage.ksel_ka = 29;
  stage.tevorders_colorchan = 1;
  stage.cc_a = 10;
  stage.ras_swap[0] = 2;
  stage.ras_swap[2] = 0;
  const auto cg = gx_vita::generate_cg(key);
  CHECK(cg.ok);
  CHECK(contains(cg.fragment, "uniform float4 tev_colors[4]"));
  CHECK(contains(cg.fragment, "uniform float4 konst_colors[4]"));
  CHECK(contains(cg.fragment, "konst_colors[2].rgb"));
  CHECK(contains(cg.fragment, "konst_colors[1].a"));
  CHECK(contains(cg.fragment, "color1.bgra"));
  return 0;
}

int tev_compare_coverage() {
  for (std::uint8_t mode = 0; mode < 4; ++mode) {
    for (std::uint8_t op = 0; op < 2; ++op) {
      auto key = tev_key();
      key.tev_stages[0].cc_bias = key.tev_stages[0].ac_bias = 3;
      key.tev_stages[0].cc_scale = key.tev_stages[0].ac_scale = mode;
      key.tev_stages[0].cc_op = key.tev_stages[0].ac_op = op;
      const auto cg = gx_vita::generate_cg(key);
      CHECK(cg.ok);
      CHECK(contains(cg.fragment, op ? " == " : " > "));
      CHECK(contains(cg.fragment, "? tevin_c"));
    }
  }
  return 0;
}

int alpha_test_coverage() {
  const char* operators[] = {"false", " < ", " == ", " <= ",
                             " > ", " != ", " >= ", "true"};
  for (std::uint8_t comparison = 0; comparison < 8; ++comparison) {
    auto key = tev_key();
    key.alpha_comp0 = comparison;
    const auto cg = gx_vita::generate_cg(key);
    CHECK(cg.ok);
    CHECK(contains(cg.fragment, "uniform float2 alpha_ref"));
    CHECK(contains(cg.fragment, "discard;"));
    CHECK(contains(cg.fragment, operators[comparison]));
  }
  const char* logic[] = {" && ", " || ", " != ", " == "};
  for (std::uint8_t op = 0; op < 4; ++op) {
    auto key = tev_key();
    key.alpha_comp0 = 4;
    key.alpha_comp1 = 1;
    key.alpha_logic = op;
    const auto cg = gx_vita::generate_cg(key);
    CHECK(cg.ok);
    CHECK(contains(cg.fragment, logic[op]));
    CHECK(contains(cg.fragment, "prev.a > alpha_ref.x"));
    CHECK(contains(cg.fragment, "prev.a < alpha_ref.y"));
  }
  return 0;
}

} // namespace

int main() {
  CHECK(tev_argument_coverage() == 0);
  CHECK(tev_arithmetic_coverage() == 0);
  CHECK(tev_konst_and_swap_coverage() == 0);
  CHECK(tev_compare_coverage() == 0);
  CHECK(alpha_test_coverage() == 0);
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
    CHECK(contains(cg.fragment, "return prev / 255.0;"));
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
