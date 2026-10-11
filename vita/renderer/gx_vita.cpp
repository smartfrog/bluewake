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

// The texmap a single-texmap draw samples (its sampler is the one indexed by
// that texmap; texmap 0 is not assumed).
std::uint32_t single_texmap(const gxc::ShaderKey& key) {
  const std::uint32_t used = gxc::used_texmap_mask(key);
  for (std::uint32_t t = 0; t < 8u; ++t)
    if ((used & (1u << t)) != 0u)
      return t;
  return 0u;
}

void translate_pipeline(const gxc::DrawPlan& plan, PipelineState& out) {
  const gxc::PipelineKey& key = plan.pipeline;
  out.depth_test = key.depth_test;
  out.depth_func = compare_mode_token(key.depth_func);
  out.depth_write = key.depth_update;
  out.blend_enable = key.blend_enable;
  out.blend_src = blend_factor_token(key.src_factor);
  out.blend_dst = blend_factor_token(key.dst_factor);
  out.blend_src_alpha = blend_factor_token(key.src_factor_alpha);
  out.blend_dst_alpha = blend_factor_token(key.dst_factor_alpha);
  out.blend_equation = key.blend_subtract != 0u ? kGlFuncSubtract : kGlFuncAdd;
  out.use_dst_alpha = key.shader.use_dst_alpha;
  out.dst_alpha = key.shader.dst_alpha;
  // CullMode::All (3) culls both faces: GL culls one face per draw, so the
  // caller drops such draws through the plan's cull_all state instead.
  out.cull_enable = key.cull_mode != 0u ? 1u : 0u;
  out.cull_face = key.cull_mode == 2u ? kGlFront : kGlBack;
  out.color_write = key.color_update;
  out.alpha_write = key.alpha_update;
  switch (key.primitive_topology) {
  case 1u:
    out.topology = kGlLines;
    break;
  case 2u:
    out.topology = kGlPoints;
    break;
  default:
    out.topology = kGlTriangles;
    break;
  }
  out.program_key = shader_key_hash(key.shader);
}

void translate_geometry(const gxc::DrawPlan& plan, Geometry& out) {
  out.vertex_count = plan.vertex_count;
  out.vertices.resize(plan.vertices.size() * sizeof(float));
  std::memcpy(out.vertices.data(), plan.vertices.data(), out.vertices.size());
  out.indices = plan.indices;
}

// Decode one texture to RGBA8 with the pinned codecs. A CI texture without
// its palette stays invalid (the caller counts the gap) instead of decoding
// wrong colours.
void decode_texture_upload(std::uint32_t format, std::uint32_t width,
                           std::uint32_t height, const void* data,
                           std::uint32_t available, bool has_tlut,
                           std::uint32_t tlut_format,
                           std::uint32_t tlut_entries, const void* tlut_data,
                           std::uint32_t tlut_available, TextureUpload& out) {
  if (data == nullptr || width == 0u || height == 0u)
    return;
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  std::vector<std::uint8_t> rgba;
  if (gxc::is_ci_format(format)) {
    if (!has_tlut || tlut_data == nullptr)
      return;
    rgba = gxc::decode_ci(format, width, height, bytes, available, tlut_format,
                          tlut_entries, static_cast<const std::uint8_t*>(tlut_data),
                          tlut_available);
  } else {
    rgba = gxc::decode_texture(format, width, height, bytes, available);
  }
  if (rgba.size() != static_cast<std::size_t>(width) * height * 4u)
    return;
  out.valid = true;
  out.width = width;
  out.height = height;
  out.rgba = std::move(rgba);
}

void translate_sampler(const gxc::PlanSampler& sampler, TextureUpload& out) {
  out.wrap_s = wrap_mode_token(sampler.wrap_s);
  out.wrap_t = wrap_mode_token(sampler.wrap_t);
  out.mag_filter = sampler.mag_filter != 0u ? kGlLinear : kGlNearest;
  out.min_filter = sampler.min_filter != 0u ? kGlLinear : kGlNearest;
}

void translate_flat_texture(const gxc::DrawPlan& plan, TextureUpload& out) {
  if (!plan.has_texture)
    return;
  decode_texture_upload(plan.tex_format, plan.tex_width, plan.tex_height,
                        plan.tex_data, plan.tex_available, plan.has_tlut,
                        plan.tlut_format, plan.tlut_entries, plan.tlut_data,
                        plan.tlut_available, out);
  translate_sampler(plan.samplers[single_texmap(plan.pipeline.shader)], out);
}

void translate_per_texmap_textures(const gxc::DrawPlan& plan,
                                   TranslatedDraw& out) {
  out.texmap_mask = plan.texmap_mask;
  for (std::uint32_t t = 0; t < 8u; ++t) {
    if ((plan.texmap_mask & (1u << t)) == 0u)
      continue;
    const gxc::PlanTexture& src = plan.textures[t];
    TextureUpload& dst = out.textures[t];
    if (!src.valid)
      continue;
    decode_texture_upload(src.format, src.width, src.height, src.data,
                          src.available, src.has_tlut, src.tlut_format,
                          src.tlut_entries, src.tlut_data, src.tlut_available,
                          dst);
    translate_sampler(plan.samplers[t], dst);
  }
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
  if (plan.texmap_mask != 0u)
    translate_per_texmap_textures(plan, out);
  else
    translate_flat_texture(plan, out.texture);
  out.ok = true;
  return out;
}

} // namespace gx_vita
