// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 GX -> vitaGL translation. See gx_vita.h.

#include "gx_vita.h"

#include <cstring>

namespace gx_vita {

namespace gxc = gxruntime::gxcore;

std::uint16_t blend_factor_token(std::uint8_t factor) {
  switch (static_cast<gxc::SrcBlendFactor>(factor)) {
  case gxc::SrcBlendFactor::Zero:
    return kGlZero;
  case gxc::SrcBlendFactor::One:
    return kGlOne;
  case gxc::SrcBlendFactor::DstClr:
    return kGlDstColor;
  case gxc::SrcBlendFactor::InvDstClr:
    return kGlOneMinusDstColor;
  case gxc::SrcBlendFactor::SrcAlpha:
    return kGlSrcAlpha;
  case gxc::SrcBlendFactor::InvSrcAlpha:
    return kGlOneMinusSrcAlpha;
  case gxc::SrcBlendFactor::DstAlpha:
    return kGlDstAlpha;
  case gxc::SrcBlendFactor::InvDstAlpha:
    return kGlOneMinusDstAlpha;
  }
  return kGlOne;
}

std::uint16_t compare_mode_token(std::uint8_t mode) {
  switch (static_cast<gxc::CompareMode>(mode)) {
  case gxc::CompareMode::Never:
    return kGlNever;
  case gxc::CompareMode::Less:
    return kGlLess;
  case gxc::CompareMode::Equal:
    return kGlEqual;
  case gxc::CompareMode::LEqual:
    return kGlLequal;
  case gxc::CompareMode::Greater:
    return kGlGreater;
  case gxc::CompareMode::NEqual:
    return kGlNequal;
  case gxc::CompareMode::GEqual:
    return kGlGequal;
  case gxc::CompareMode::Always:
    return kGlAlways;
  }
  return kGlAlways;
}

std::uint16_t wrap_mode_token(std::uint8_t wrap) {
  switch (wrap) {
  case 0u: // clamp
    return kGlClampToEdge;
  case 2u: // mirror
    return kGlMirroredRepeat;
  default: // repeat
    return kGlRepeat;
  }
}

std::uint64_t shader_key_hash(const gxc::ShaderKey& key) {
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(&key);
  std::uint64_t hash = 0xCBF29CE484222325ull;
  for (std::size_t i = 0; i < sizeof(key); ++i) {
    hash ^= bytes[i];
    hash *= 0x100000001B3ull;
  }
  return hash == 0u ? 1u : hash;
}

namespace {

void translate_pipeline(const gxc::DrawPlan& plan, PipelineState& out) {
  const gxc::PipelineKey& key = plan.pipeline;
  out.depth_test = key.depth_test;
  out.depth_func = compare_mode_token(key.depth_func);
  out.depth_write = key.depth_update;
  out.blend_enable = key.blend_enable;
  out.blend_src = blend_factor_token(key.src_factor);
  out.blend_dst = blend_factor_token(key.dst_factor);
  // CullMode::All (3) culls both faces: GL culls one face per draw, so the
  // caller drops such draws through the plan's cull_all state instead.
  out.cull_enable = key.cull_mode != 0u ? 1u : 0u;
  out.cull_face = key.cull_mode == 2u ? kGlFront : kGlBack;
  out.color_write = key.color_update;
  out.alpha_write = key.alpha_update;
  out.program_key = shader_key_hash(key.shader);
}

void translate_geometry(const gxc::DrawPlan& plan, Geometry& out) {
  out.vertex_count = plan.vertex_count;
  out.vertices.resize(plan.vertices.size() * sizeof(float));
  std::memcpy(out.vertices.data(), plan.vertices.data(), out.vertices.size());
  out.indices = plan.indices;
}

// Decode the bound texture to RGBA8 with the pinned codecs. A CI texture
// without its palette stays invalid (the caller counts the gap) instead of
// decoding wrong colours.
void translate_texture(const gxc::DrawPlan& plan, TextureUpload& out) {
  if (!plan.has_texture || plan.tex_data == nullptr)
    return;
  const auto* data = static_cast<const std::uint8_t*>(plan.tex_data);
  std::vector<std::uint8_t> rgba;
  if (gxc::is_ci_format(plan.tex_format)) {
    if (!plan.has_tlut || plan.tlut_data == nullptr)
      return;
    rgba = gxc::decode_ci(plan.tex_format, plan.tex_width, plan.tex_height,
                          data, plan.tex_available, plan.tlut_format,
                          plan.tlut_entries,
                          static_cast<const std::uint8_t*>(plan.tlut_data),
                          plan.tlut_available);
  } else {
    rgba = gxc::decode_texture(plan.tex_format, plan.tex_width, plan.tex_height,
                               data, plan.tex_available);
  }
  if (rgba.size() != static_cast<std::size_t>(plan.tex_width) *
                          plan.tex_height * 4u)
    return;
  out.valid = true;
  out.width = plan.tex_width;
  out.height = plan.tex_height;
  out.rgba = std::move(rgba);
  const gxc::PlanSampler& sampler = plan.samplers[0];
  out.wrap_s = wrap_mode_token(sampler.wrap_s);
  out.wrap_t = wrap_mode_token(sampler.wrap_t);
  out.mag_filter = sampler.mag_filter != 0u ? kGlLinear : kGlNearest;
  out.min_filter = sampler.min_filter != 0u ? kGlLinear : kGlNearest;
}

} // namespace

TranslatedDraw translate(const gxc::DrawPlan& plan) {
  TranslatedDraw out;
  if (!plan.ok) {
    out.skip_reason = plan.skip_reason != nullptr ? plan.skip_reason
                                                  : "plan not ok";
    return out;
  }
  translate_pipeline(plan, out.pipeline);
  translate_geometry(plan, out.geometry);
  translate_texture(plan, out.texture);
  out.ok = true;
  return out;
}

} // namespace gx_vita
