// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 bounded Cg generation tests.
//
// The cel and textured keys come from the real fixtures through the pinned
// frontend; the unsupported and untextured keys are built by hand. Checks are
// explicit (no assert).

#include "gx_cg.h"
#include "gx_dst_alpha.h"

#include "../feasibility/gx/gx_fixture.hpp"

#include <cstdio>
#include <fstream>
#include <filesystem>
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
#if defined(WW_RENDERER_DST_ALPHA_ADMIT)
  gxc::PipelineKey pipeline{};
#endif
  bool have = false;
};

void capture_key(const gxc::DrawPlan& plan, void* user) {
  auto* capture = static_cast<KeyCapture*>(user);
  capture->key = plan.pipeline.shader;
#if defined(WW_RENDERER_DST_ALPHA_ADMIT)
  capture->pipeline = plan.pipeline;
#endif
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

#if defined(WW_RENDERER_DST_ALPHA_ADMIT)
bool replay_pipeline(const char* scenario, gxc::PipelineKey& pipeline) {
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
  pipeline = capture.pipeline;
  return true;
}
#endif

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

int texgen_boundary_coverage() {
  for (std::uint8_t coord = 5; coord < 8; ++coord) {
    auto key = tev_key();
    key.textured = 1;
    key.num_tex_gens = coord + 1;
    key.tex_gens[coord].sourcerow = 5 + coord;
    key.tev_stages[0].tevorders_enable = 1;
    key.tev_stages[0].tevorders_texcoord = coord;
    const auto cg = gx_vita::generate_cg(key);
    CHECK(!cg.ok);
    CHECK(std::string(cg.unsupported_reason) == "raw texcoord exceeds Vita vertex layout");
  }
  return 0;
}

int cross_source_texgen_coverage() {
  auto key = tev_key();
  key.textured = 1;
  key.num_tex_gens = 2;
  key.tex_gens[1].sourcerow = 5;
  key.tev_stages[0].tevorders_enable = 1;
  key.tev_stages[0].tevorders_texcoord = 1;
  const auto cg = gx_vita::generate_cg(key);
  CHECK(cg.ok);
  CHECK(contains(cg.fragment, "tex2D(tex0, uv1.xy)"));
  CHECK(contains(cg.vertex, "float3 uv1 : TEXCOORD1"));
  CHECK(!contains(cg.vertex, "float3 uv0"));
  return 0;
}

int unsupported_pixel_state_coverage() {
  for (int feature = 0; feature < 2; ++feature) {
    auto key = tev_key();
    if (feature == 0)
      key.fog_fsel = 1;
    else
      key.ztex_op = 2;
    const auto cg = gx_vita::generate_cg(key);
    CHECK(!cg.ok);
    CHECK(cg.unsupported_reason != nullptr);
    std::printf("unsupported ok: %s\n", cg.unsupported_reason);
  }
  return 0;
}

// The shader-key entry has no blend facts, so it keeps refusing every BP 0x42
// key with the original reason. This is the flag-off contract (and the
// shader-only contract while the admission is compiled in).
int dst_alpha_shader_key_is_refused() {
  auto key = tev_key();
  key.use_dst_alpha = 1;
  key.dst_alpha = 0x80;
  const auto cg = gx_vita::generate_cg(key);
  CHECK(!cg.ok);
  CHECK(std::string(cg.unsupported_reason) ==
        "destination alpha requires dual-source blending");
  std::printf("dst alpha shader-key refusal kept\n");
  return 0;
}

#if defined(WW_RENDERER_DST_ALPHA_ADMIT)
// The colour-updates-off class: the colour channel cannot observe the
// fragment alpha, so one exact pass writes the constant, with no extra draw or
// program variant.
int dst_alpha_colour_update_off_is_admitted() {
  gxc::PipelineKey pipeline;
  CHECK(replay_pipeline("dstalpha_stamp_triangle", pipeline));
  CHECK(pipeline.shader.use_dst_alpha == 1u);
  CHECK(pipeline.shader.dst_alpha == 0x80u);
  CHECK(pipeline.blend_enable == 1u && pipeline.color_update == 0u);
  CHECK(pipeline.src_factor == 4u && pipeline.dst_factor == 5u);
  CHECK(gx_vita::dst_alpha::mono_pass_refusal(pipeline) == nullptr);
  const auto cg = gx_vita::generate_cg(pipeline);
  CHECK(cg.ok);
  CHECK(contains(cg.fragment,
                 "float4(tex2D(tex0, uv0.xy).rgb, 128.0 / 255.0)"));
  std::printf("dst alpha colour-updates-off admitted\n");
  return 0;
}

// With blending off the colour channel cannot read the fragment alpha either.
// The TEV path still runs the alpha test, ZTexture depth and fog before the
// constant alpha replaces the output.
int dst_alpha_blend_off_tev_keeps_alpha_test_order() {
  auto shader = tev_key();
  shader.textured = 1;
  shader.num_tex_gens = 1;
  shader.tex_gens[0].sourcerow = 5;
  shader.tev_stages[0].tevorders_enable = 1;
  shader.ztex_op = 2;
  shader.ztex_type = 2;
  shader.fog_fsel = 2;
  shader.alpha_comp0 = 4; // greater than the reference
  shader.use_dst_alpha = 1;
  shader.dst_alpha = 0x40;
  gxc::PipelineKey pipeline;
  pipeline.shader = shader;
  pipeline.color_update = 1;
  CHECK(gx_vita::dst_alpha::mono_pass_refusal(pipeline) == nullptr);
  const auto cg = gx_vita::generate_cg(pipeline);
  CHECK(cg.ok);
  const auto stamp = cg.fragment.find("64.0 / 255.0");
  CHECK(stamp != std::string::npos);
  CHECK(cg.fragment.find("discard;") < stamp);
  CHECK(cg.fragment.find("fragment_depth =") < stamp);
  CHECK(cg.fragment.find("float fog_z") < stamp);
  std::printf("dst alpha blend-off TEV admission keeps the ordering\n");
  return 0;
}

// Blending off wins over the colour factors: the colour channel never reads
// them, so SRCALPHA/INVSRCALPHA still admit in the mono-pass.
int dst_alpha_blend_off_with_source_alpha_factors_is_admitted() {
  gxc::PipelineKey pipeline;
  pipeline.shader.use_dst_alpha = 1;
  pipeline.shader.dst_alpha = 0x80;
  pipeline.alpha_update = 1;
  pipeline.blend_enable = 0;
  pipeline.color_update = 1;
  pipeline.src_factor = 4; // SRCALPHA
  pipeline.dst_factor = 5; // INVSRCALPHA
  CHECK(gx_vita::dst_alpha::mono_pass_refusal(pipeline) == nullptr);
  const auto cg = gx_vita::generate_cg(pipeline);
  CHECK(cg.ok);
  CHECK(contains(cg.fragment, "128.0 / 255.0"));
  std::printf("dst alpha blend-off with source-alpha factors admitted\n");
  return 0;
}

// The OR protects the mixed pair: DSTALPHA reads the framebuffer (allowed),
// but INVSRCALPHA still reads the computed fragment alpha, so one pass cannot
// serve both alphas.
int dst_alpha_mixed_factor_pair_is_refused() {
  gxc::PipelineKey pipeline;
  pipeline.shader.use_dst_alpha = 1;
  pipeline.shader.dst_alpha = 0x80;
  pipeline.alpha_update = 1;
  pipeline.blend_enable = 1;
  pipeline.color_update = 1;
  pipeline.src_factor = 6; // DSTALPHA
  pipeline.dst_factor = 5; // INVSRCALPHA
  CHECK(gx_vita::dst_alpha::mono_pass_refusal(pipeline) != nullptr);
  const auto cg = gx_vita::generate_cg(pipeline);
  CHECK(!cg.ok);
  CHECK(std::string(cg.unsupported_reason) ==
        "destination alpha conflicts with the colour blend source alpha (two-pass split required)");
  std::printf("dst alpha mixed factor pair refused\n");
  return 0;
}

// The untextured passthrough path has no TEV alpha to preserve: the constant
// replaces the vertex alpha in the same single pass.
int dst_alpha_untextured_is_admitted() {
  gxc::PipelineKey pipeline;
  pipeline.shader.use_dst_alpha = 1;
  pipeline.shader.dst_alpha = 0x20;
  pipeline.alpha_update = 1;
  CHECK(gx_vita::dst_alpha::mono_pass_refusal(pipeline) == nullptr);
  const auto cg = gx_vita::generate_cg(pipeline);
  CHECK(cg.ok);
  CHECK(contains(cg.fragment, "float4(color0.rgb, 32.0 / 255.0)"));
  std::printf("dst alpha untextured admitted\n");
  return 0;
}

// The conflict class: colour updates on and a colour factor that reads the
// computed alpha. One pass cannot serve both alphas; keep it refused.
int dst_alpha_conflict_is_refused() {
  gxc::PipelineKey pipeline;
  CHECK(replay_pipeline("dstalpha_triangle", pipeline));
  CHECK(pipeline.blend_enable == 1u && pipeline.color_update == 1u);
  CHECK(pipeline.src_factor == 4u && pipeline.dst_factor == 5u);
  CHECK(gx_vita::dst_alpha::mono_pass_refusal(pipeline) != nullptr);
  const auto cg = gx_vita::generate_cg(pipeline);
  CHECK(!cg.ok);
  CHECK(std::string(cg.unsupported_reason) ==
        "destination alpha conflicts with the colour blend source alpha (two-pass split required)");
  gxc::PipelineKey framebuffer_alpha = pipeline;
  framebuffer_alpha.src_factor = 6; // DSTALPHA reads the framebuffer, not the fragment
  framebuffer_alpha.dst_factor = 7;
  CHECK(gx_vita::dst_alpha::mono_pass_refusal(framebuffer_alpha) == nullptr);
  CHECK(gx_vita::generate_cg(framebuffer_alpha).ok);
  std::printf("dst alpha conflict refused\n");
  return 0;
}

// Subtract mode forces ONE/ONE on the reference line, but this path has not
// been verified against it: keep every subtract draw refused.
int dst_alpha_subtract_is_refused() {
  gxc::PipelineKey pipeline;
  pipeline.shader.use_dst_alpha = 1;
  pipeline.shader.dst_alpha = 0x80;
  pipeline.alpha_update = 1;
  pipeline.blend_subtract = 1;
  CHECK(gx_vita::dst_alpha::mono_pass_refusal(pipeline) != nullptr);
  const auto cg = gx_vita::generate_cg(pipeline);
  CHECK(!cg.ok);
  CHECK(std::string(cg.unsupported_reason) ==
        "destination alpha with blend subtract stays refused until the ONE/ONE rule is verified here");
  std::printf("dst alpha subtract refused\n");
  return 0;
}
#endif

int fog_mode_coverage() {
  for (std::uint8_t mode : {2, 4, 5, 6, 7}) {
    for (std::uint8_t projection : {0, 1}) {
      for (std::uint8_t range : {0, 1}) {
        auto key = tev_key();
        key.fog_fsel = mode;
        key.fog_proj = projection;
        key.fog_range = range;
        const auto cg = gx_vita::generate_cg(key);
        CHECK(cg.ok);
        CHECK(contains(cg.fragment, "float4 screen : WPOS"));
        CHECK(contains(cg.fragment, "uniform float4 fog_params"));
        CHECK(contains(cg.fragment, "uniform float4 fog_color"));
        CHECK(contains(cg.fragment, "uniform float2 fog_depth") == (projection == 0));
        CHECK(contains(cg.fragment, "uniform float4 fog_range[3]") == (range != 0));
        CHECK(cg.fragment.find("discard;") < cg.fragment.find("float fog_z"));
        CHECK(contains(cg.fragment, "prev.r = floor("));
        CHECK(!contains(cg.fragment, "prev.a = floor("));
      }
    }
  }
  return 0;
}

int fog_uses_ztexture_depth() {
  for (std::uint8_t operation : {1, 2}) {
    auto key = tev_key();
    key.textured = 1;
    key.num_tex_gens = 1;
    key.tex_gens[0].sourcerow = 5;
    key.tev_stages[0].tevorders_enable = 1;
    key.ztex_op = operation;
    key.ztex_type = 2;
    key.fog_fsel = 2;
    const auto cg = gx_vita::generate_cg(key);
    CHECK(cg.ok);
    CHECK(contains(cg.fragment, "float fog_z = floor(clamp(fragment_depth * 16777216.0"));
    CHECK(cg.fragment.find("fragment_depth =") < cg.fragment.find("float fog_z"));
    const auto screen = cg.fragment.find("float4 screen : WPOS");
    CHECK(cg.fragment.find("float4 screen : WPOS", screen + 1) == std::string::npos);
  }
  return 0;
}

int fog_key_bounds() {
  for (std::uint8_t mode : {1, 3, 8}) {
    auto key = tev_key();
    key.fog_fsel = mode;
    const auto cg = gx_vita::generate_cg(key);
    CHECK(!cg.ok && std::string(cg.unsupported_reason) == "invalid fog mode");
  }
  const auto cg = gx_vita::generate_cg(tev_key());
  CHECK(cg.ok && !contains(cg.fragment, "fog_params"));
  return 0;
}

int indirect_key_coverage() {
  for (std::uint8_t format = 0; format < 4; ++format) {
    for (std::uint8_t matrix = 0; matrix < 3; ++matrix) {
      auto key = tev_key();
      key.textured = 1;
      key.num_tex_gens = 1;
      key.tex_gens[0].sourcerow = 5;
      key.num_ind_stages = 4;
      key.ind_stages[3].texmap = 7;
      key.ind_stages[3].scale_s = 15;
      key.tev_stages[0].tevorders_enable = 1;
      key.tev_stages[0].ind_stage = 3;
      key.tev_stages[0].ind_matrix_index = 3;
      key.tev_stages[0].ind_matrix_id = matrix;
      key.tev_stages[0].ind_format = format;
      key.tev_stages[0].ind_bump_alpha = 3;
      key.tev_stages[0].tevorders_colorchan = 6;
      const auto cg = gx_vita::generate_cg(key);
      CHECK(cg.ok);
      CHECK(contains(cg.fragment, "uniform sampler2D tex7"));
      CHECK(contains(cg.fragment, "tex2D(tex7,"));
      CHECK(contains(cg.fragment, ").abg * 255.0"));
      CHECK(contains(cg.fragment, "floor(fixpoint0.x / 32768.0)"));
      CHECK(contains(cg.fragment, "gx_ind_trunc(indirect_coord0.x * texture_dimensions[0].z * 128.0)"));
      CHECK(contains(cg.fragment, "indirect_matrices[4]"));
      CHECK(contains(cg.fragment, "alphabump + floor(alphabump / 32.0)"));
    }
  }
  return 0;
}

int indirect_preserves_wrap_without_lookup() {
  auto key = tev_key();
  key.textured = 1;
  key.num_tex_gens = 1;
  key.tex_gens[0].sourcerow = 5;
  key.tev_stages[0].tevorders_enable = 1;
  key.tev_stages[0].ind_wrap_s = 6;
  key.tev_stages[0].ind_add_prev = 1;
  const auto cg = gx_vita::generate_cg(key);
  CHECK(cg.ok);
  CHECK(contains(cg.fragment, "tevcoord.x = gx_ind_wrap24(0.0 + indirect_x0)"));
  CHECK(contains(cg.fragment, "previous_coord.x"));
  CHECK(!contains(cg.fragment, "float3 ind_raw"));
  return 0;
}

int indirect_rejects_original_lod() {
  auto key = tev_key();
  key.num_ind_stages = 1;
  key.tev_stages[0].ind_use_original_lod = 1;
  const auto cg = gx_vita::generate_cg(key);
  CHECK(!cg.ok && std::string(cg.unsupported_reason) == "original indirect LOD requires complete sampler state and captured mip levels");
  return 0;
}

int ztexture_output_coverage() {
  for (std::uint8_t type = 0; type < 3; ++type) {
    for (std::uint8_t operation : {1, 2}) {
      auto key = tev_key();
      key.textured = 1;
      key.num_tex_gens = 1;
      key.tex_gens[0].sourcerow = 5;
      key.tev_stages[0].tevorders_enable = 1;
      key.ztex_type = type;
      key.ztex_op = operation;
      const auto cg = gx_vita::generate_cg(key);
      CHECK(cg.ok);
      CHECK(contains(cg.fragment, "out float fragment_depth : DEPTH"));
      CHECK(contains(cg.fragment, "uniform float2 ztexture_bias"));
      CHECK(contains(cg.fragment, "rawtex = tex;"));
      const char* sample[] = {"float z_high = 0.0", "float z_high = rawtex.a",
                              "float z_high = rawtex.r * 256.0 + rawtex.g"};
      CHECK(contains(cg.fragment, sample[type]));
      CHECK(contains(cg.fragment, "fragment_depth = (z_high * 256.0 + z_low) / 16777216.0"));
      CHECK(cg.fragment.find("discard;") < cg.fragment.find("fragment_depth ="));
      CHECK(contains(cg.fragment, "float4 screen : WPOS") == (operation == 1));
      CHECK(contains(cg.vertex, "clip.z = 2.0 * clip.z + clip.w"));
    }
  }
  return 0;
}

int ztexture_last_unswapped_sample() {
  auto key = tev_key();
  key.textured = 1;
  key.num_tex_gens = 1;
  key.tex_gens[0].sourcerow = 5;
  key.tev_stages[0].tevorders_enable = 1;
  key.tev_stages[0].tex_swap[0] = 2;
  key.tev_stages[0].tex_swap[2] = 0;
  key.num_tev_stages = 2;
  key.tev_stages[1] = key.tev_stages[0];
  key.tev_stages[1].tevorders_enable = 0;
  key.ztex_op = 2;
  key.ztex_type = 2;
  const auto cg = gx_vita::generate_cg(key);
  CHECK(cg.ok);
  const auto raw = cg.fragment.find("rawtex = tex;");
  CHECK(raw < cg.fragment.find("tex = tex.bgra"));
  CHECK(cg.fragment.find("rawtex = tex;", raw + 1) == std::string::npos);
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--write-indirect-shaders") {
    const std::filesystem::path directory(argv[2]);
    std::filesystem::create_directories(directory);
    for (std::uint8_t format = 0; format < 4; ++format) {
      for (std::uint8_t bias = 0; bias < 8; ++bias) {
        for (std::uint8_t matrix = 0; matrix < 4; ++matrix) {
          auto key = tev_key();
          key.textured = 1;
          key.num_tex_gens = 1;
          key.tex_gens[0].sourcerow = 5;
          key.num_ind_stages = 4;
          auto& stage = key.tev_stages[0];
          stage.tevorders_enable = 1;
          stage.ind_stage = bias % 4;
          stage.ind_format = format;
          stage.ind_bias = bias;
          stage.ind_bump_alpha = bias % 4;
          stage.ind_matrix_index = matrix == 3 ? 0 : 1 + bias % 3;
          stage.ind_matrix_id = matrix == 3 ? 0 : matrix;
          stage.ind_wrap_s = bias;
          stage.ind_wrap_t = (bias + 3) % 8;
          stage.ind_add_prev = bias % 2;
          const auto cg = gx_vita::generate_cg(key);
          CHECK(cg.ok);
          const auto name = std::to_string(format) + "-" + std::to_string(bias) + "-" +
                            std::to_string(matrix);
          std::ofstream file(directory / (name + ".cg"));
          file << cg.fragment;
          file.close();
          CHECK(file.good());
        }
      }
    }
    return 0;
  }
  if (argc == 3 && std::string(argv[1]) == "--write-fog-shaders") {
    const std::filesystem::path directory(argv[2]);
    std::filesystem::create_directories(directory);
    for (std::uint8_t mode : {2, 4, 5, 6, 7}) {
      for (std::uint8_t projection : {0, 1}) {
        for (std::uint8_t range : {0, 1}) {
          for (std::uint8_t depth : {0, 1}) {
            auto key = tev_key();
            key.fog_fsel = mode;
            key.fog_proj = projection;
            key.fog_range = range;
            if (depth) {
              key.ztex_op = 2;
              key.textured = 1;
              key.num_tex_gens = 1;
              key.tex_gens[0].sourcerow = 5;
              key.tev_stages[0].tevorders_enable = 1;
            }
            const auto cg = gx_vita::generate_cg(key);
            CHECK(cg.ok);
            const auto name = std::to_string(mode) + "-" + std::to_string(projection) +
                              "-" + std::to_string(range) + "-" + std::to_string(depth);
            std::ofstream file(directory / (name + ".cg"));
            file << cg.fragment;
            file.close();
            CHECK(file.good());
          }
        }
      }
    }
    return 0;
  }
  if (argc == 3 && std::string(argv[1]) == "--write-ztexture-math") {
    std::ofstream file(argv[2]);
    CHECK(file.good());
    file << "#include <algorithm>\n#include <cmath>\n#include <cstdint>\n#include <cstdio>\n"
            "using std::floor; using std::clamp;\n"
            "struct Float2 { float x,y; }; struct Float4 { float r,g,b,a,z; };\n";
    for (std::uint8_t type = 0; type < 3; ++type) {
      for (std::uint8_t op : {1, 2}) {
        auto key = tev_key();
        key.textured = 1;
        key.num_tex_gens = 1;
        key.tex_gens[0].sourcerow = 5;
        key.tev_stages[0].tevorders_enable = 1;
        key.ztex_type = type;
        key.ztex_op = op;
        const auto cg = gx_vita::generate_cg(key);
        CHECK(cg.ok);
        const auto start = cg.fragment.find("  float z_high");
        const auto end = cg.fragment.find("  return prev", start);
        CHECK(start != std::string::npos && end != std::string::npos);
        file << "float depth_" << int(type) << '_' << int(op)
             << "(Float4 rawtex, Float2 ztexture_bias, Float4 screen) { float fragment_depth;\n"
             << cg.fragment.substr(start, end - start) << "return fragment_depth; }\n";
      }
    }
    file << "int main() {\n"
            "  using Fn = float(*)(Float4,Float2,Float4);\n"
            "  Fn fn[3][2] = {{depth_0_1,depth_0_2},{depth_1_1,depth_1_2},{depth_2_1,depth_2_2}};\n"
            "  std::uint32_t seed = 0x10203040;\n"
            "  for (int type=0;type<3;++type) for(int op=0;op<2;++op) for(int i=0;i<4096;++i) {\n"
            "    auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed; };\n"
            "    std::uint32_t r=next()&255,g=next()&255,b=next()&255,a=next()&255;\n"
            "    std::uint32_t bias=next()&0xFFFFFF, raster=next()&0xFFFFFF;\n"
            "    if(i<4) {r=g=b=a=255;bias=i;raster=0xFFFFFF;}\n"
            "    std::uint32_t sample=type==0?a:type==1?r+256*a:65536*r+256*g+b;\n"
            "    std::uint32_t expected=(sample+bias+(op==0?raster:0))&0xFFFFFF;\n"
            "    float actual=fn[type][op]({float(r),float(g),float(b),float(a),0},\n"
            "        {float(bias>>8),float(bias&255)},{0,0,0,0,float(raster)/16777216.f});\n"
            "    if(actual!=float(expected)/16777216.f) {\n"
            "      std::fprintf(stderr,\"type=%d op=%d sample=%u bias=%u raster=%u actual=%g expected=%g\\n\",\n"
            "          type,op+1,sample,bias,raster,actual,float(expected)/16777216.f);return 1;}\n"
            "  }\n"
            "  std::puts(\"generated ZTexture math matches 24576 integer-oracle cases\");\n"
            "}\n";
    CHECK(file.good());
    return 0;
  }
  CHECK(argc == 1);
  CHECK(indirect_key_coverage() == 0);
  CHECK(indirect_preserves_wrap_without_lookup() == 0);
  CHECK(indirect_rejects_original_lod() == 0);
  CHECK(fog_mode_coverage() == 0);
  CHECK(fog_uses_ztexture_depth() == 0);
  CHECK(fog_key_bounds() == 0);
  CHECK(ztexture_output_coverage() == 0);
  CHECK(ztexture_last_unswapped_sample() == 0);
  CHECK(tev_argument_coverage() == 0);
  CHECK(tev_arithmetic_coverage() == 0);
  CHECK(tev_konst_and_swap_coverage() == 0);
  CHECK(tev_compare_coverage() == 0);
  CHECK(alpha_test_coverage() == 0);
  CHECK(texgen_boundary_coverage() == 0);
  CHECK(cross_source_texgen_coverage() == 0);
  CHECK(unsupported_pixel_state_coverage() == 0);
  CHECK(dst_alpha_shader_key_is_refused() == 0);
#if defined(WW_RENDERER_DST_ALPHA_ADMIT)
  CHECK(dst_alpha_colour_update_off_is_admitted() == 0);
  CHECK(dst_alpha_blend_off_tev_keeps_alpha_test_order() == 0);
  CHECK(dst_alpha_blend_off_with_source_alpha_factors_is_admitted() == 0);
  CHECK(dst_alpha_mixed_factor_pair_is_refused() == 0);
  CHECK(dst_alpha_untextured_is_admitted() == 0);
  CHECK(dst_alpha_conflict_is_refused() == 0);
  CHECK(dst_alpha_subtract_is_refused() == 0);
#endif
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
    CHECK(contains(cg.fragment, "tex2D(tex1, uv1.xy)"));
    CHECK(contains(cg.fragment, "tex2D(tex0, uv0.xy)"));
    CHECK(contains(cg.fragment, "return prev / 255.0;"));
    std::printf("cel ok\n");
  }

  {
    gxc::ShaderKey key;
    CHECK(replay_key("textured_triangle", key));
    const gx_vita::CgProgram cg = gx_vita::generate_cg(key);
    CHECK(cg.ok);
    CHECK(contains(cg.fragment, "uniform sampler2D tex0"));
    CHECK(contains(cg.fragment, "return tex2D(tex0, uv0.xy);"));
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
    // The original indirect fixture now generates a sampled coordinate path.
    gxc::ShaderKey key;
    CHECK(replay_key("indirect_triangle", key));
    CHECK(key.num_ind_stages == 1u);
    CHECK(key.tev_stages[0].ind_matrix_index == 1u);
    const gx_vita::CgProgram cg = gx_vita::generate_cg(key);
    CHECK(cg.ok);
    CHECK(contains(cg.fragment, "indirect_s0"));
    std::printf("indirect generated ok\n");
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
