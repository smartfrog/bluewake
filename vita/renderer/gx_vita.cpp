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

bool translate_geometry(const gxc::DrawPlan& plan, Geometry& out,
                        const char** reason) {
  if (plan.vertices.size() !=
      static_cast<std::size_t>(plan.vertex_count) * gxc::kVertexFloats) {
    *reason = "decoded vertex count does not match layout";
    return false;
  }
  out.vertex_count = plan.vertex_count;
  out.vertices.resize(plan.vertices.size() * sizeof(float));
  std::memcpy(out.vertices.data(), plan.vertices.data(), out.vertices.size());
  out.indices = plan.indices;
  if (plan.pipeline.shader.has_pos_mtx_idx != 0u) {
    for (std::uint32_t v = 0; v < plan.vertex_count; ++v) {
      const float* vertex = plan.vertices.data() + v * gxc::kVertexFloats;
      std::uint32_t row;
      std::memcpy(&row, vertex + gxc::kVertexPosMtxOffset / 4, sizeof row);
      if (row > 61u) {
        *reason = "position matrix row out of range";
        return false;
      }
      float position[3];
      for (std::uint32_t axis = 0; axis < 3; ++axis) {
        const float* matrix = plan.constants.transformmatrices[row + axis];
        position[axis] = matrix[0] * vertex[0] + matrix[1] * vertex[1] +
                         matrix[2] * vertex[2] + matrix[3];
      }
      std::memcpy(out.vertices.data() + v * gxc::kVertexStrideBytes,
                  position, sizeof position);
    }
  }
  return true;
}

// The plan's vertex constants carry the transform the generated Cg consumes:
// the XF position matrix (rows 0..2, padded to 4 rows with (0,0,0,1)) and the
// Dolphin-layout projection (row-dot form).
void translate_transform(const gxc::DrawPlan& plan, TranslatedDraw& out) {
  for (std::uint32_t row = 0; row < 3u; ++row)
    std::memcpy(&out.position_matrix[row * 4u],
                plan.constants.posnormalmatrix[row], 4u * sizeof(float));
  out.position_matrix[12] = 0.0f;
  out.position_matrix[13] = 0.0f;
  out.position_matrix[14] = 0.0f;
  out.position_matrix[15] = 1.0f;
  if (plan.pipeline.shader.has_pos_mtx_idx != 0u) {
    // PNMTXIDX positions were transformed individually during translation.
    std::memset(out.position_matrix, 0, sizeof out.position_matrix);
    for (std::uint32_t i = 0; i < 4; ++i)
      out.position_matrix[5 * i] = 1.f;
  }
  std::memcpy(out.projection, &plan.constants.projection[0][0],
              16u * sizeof(float));
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
  if (!translate_geometry(plan, out.geometry, &out.skip_reason))
    return out;
  translate_transform(plan, out);
  if (plan.texmap_mask != 0u)
    translate_per_texmap_textures(plan, out);
  else
    translate_flat_texture(plan, out.texture);
  out.ok = true;
  return out;
}

TranslatedCopy translate_copy(const gxc::EfbCopyCommand& cmd) {
  TranslatedCopy out;
  if (cmd.format == 0xFu) { // GXCopyDisp: no texture destination
    out.skip_reason = "display copy has no texture destination";
    return out;
  }
  out.ok = true;
  out.dest_address = cmd.dest_address;
  out.byte_size = cmd.byte_size;
  out.format = cmd.format;
  out.src_x = cmd.src_x;
  out.src_y = cmd.src_y;
  out.width = cmd.width;
  out.height = cmd.height;
  out.dest_width = cmd.destination_width;
  out.dest_height = cmd.destination_height;
  out.clear = cmd.clear;
  out.color_update = cmd.color_update;
  out.alpha_update = cmd.alpha_update;
  out.depth_update = cmd.depth_update;
  out.clear_r = cmd.clear_r;
  out.clear_g = cmd.clear_g;
  out.clear_b = cmd.clear_b;
  out.clear_a = cmd.clear_a;
  out.clear_z = cmd.clear_z;
  out.efb_has_alpha = cmd.efb_has_alpha;
  return out;
}

bool copy_readback_to_guest(const std::uint8_t* pixels,
                            const EfbReadback& rect,
                            std::uint32_t guest_stride, std::uint8_t* guest,
                            std::size_t available, DirtyMarkFn mark,
                            void* mark_user) {
  if (pixels == nullptr || guest == nullptr || rect.width == 0u ||
      rect.height == 0u)
    return false;
  const std::size_t row_bytes = static_cast<std::size_t>(rect.width) * 4u;
  if (guest_stride < row_bytes)
    return false;
  const std::size_t needed =
      static_cast<std::size_t>(rect.height - 1u) * guest_stride + row_bytes;
  if (needed > available)
    return false;
  for (std::uint32_t row = 0; row < rect.height; ++row) {
    std::memcpy(guest + static_cast<std::size_t>(row) * guest_stride,
                pixels + static_cast<std::size_t>(row) * row_bytes, row_bytes);
    if (mark != nullptr)
      mark(mark_user, row * guest_stride, static_cast<std::uint32_t>(row_bytes));
  }
  return true;
}

} // namespace gx_vita
