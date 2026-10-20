// SPDX-License-Identifier: GPL-3.0-or-later
#include "gx_draw_census.h"
#include "gx_vita.h"
#include "gx_texgen.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace gx_vita::census {
namespace {
namespace gxc = gxruntime::gxcore;

bool unit_coordinates(const TranslatedDraw& draw, const gxc::ShaderKey& key,
                      std::uint32_t texcoord) {
  if (texcoord >= gxc::kMaxTexGens ||
      (key.tev_valid && texcoord >= key.num_tex_gens) ||
      draw.geometry.texcoords.size() != 4 * texgen::kFloatsPerVertex)
    return false;
  for (unsigned v = 0; v < 4; ++v) {
    const float* uv = draw.geometry.texcoords.data() + v * texgen::kFloatsPerVertex + 3 * texcoord;
    const float q = key.tex_gens[texcoord].projection ? uv[2] : 1.f;
    if (!std::isfinite(q) || q <= 0)
      return false;
    for (unsigned axis = 0; axis < 2; ++axis) {
      const float coordinate = uv[axis] / q;
      if (!std::isfinite(coordinate) || coordinate < 0 || coordinate > 1)
        return false;
    }
  }
  return true;
}

void transform(const float* matrix, const float* in, float* out) {
  for (unsigned row = 0; row < 4; ++row) {
    out[row] = 0;
    for (unsigned column = 0; column < 4; ++column)
      out[row] += matrix[column * 4 + row] * in[column];
  }
}

unsigned bit_count(std::uint32_t mask) {
  unsigned count = 0;
  for (unsigned t = 0; t < 8; ++t)
    count += (mask >> t) & 1u;
  return count;
}
} // namespace

AlphaStats alpha_stats(const TextureUpload& texture) {
  AlphaStats stats;
  const std::uint64_t texels = static_cast<std::uint64_t>(texture.width) * texture.height;
  if (!texture.valid || !texels || texels > UINT32_MAX ||
      texture.rgba.size() % 4 || texture.rgba.size() / 4 != texels)
    return stats;
  stats.valid = true;
  stats.texels = static_cast<std::uint32_t>(texels);
  stats.minimum = 255;
  for (std::size_t i = 3; i < texture.rgba.size(); i += 4) {
    const auto alpha = texture.rgba[i];
    stats.minimum = std::min(stats.minimum, static_cast<std::uint32_t>(alpha));
    stats.maximum = std::max(stats.maximum, static_cast<std::uint32_t>(alpha));
    stats.nonzero += alpha != 0;
    stats.sum += alpha;
  }
  return stats;
}

const AlphaSample* AlphaCache::observe(TextureIdentity identity, const TextureUpload& texture,
                                      std::uint32_t present) {
  for (unsigned i = 0; i < size_; ++i)
    if (entries_[i].identity == identity)
      return &entries_[i];
  if (size_ == entries_.size())
    return nullptr;
  auto& sample = entries_[size_++];
  sample.identity = identity;
  sample.present = present;
  sample.stats = alpha_stats(texture);
  return &sample;
}

GeometryStats geometry_stats(const TranslatedDraw& draw) {
  GeometryStats out;
  if (draw.geometry.vertex_count != 4 || draw.geometry.indices.size() != 6 ||
      draw.geometry.vertices.size() != 4 * gxc::kVertexStrideBytes)
    return out;
  std::fill_n(out.minimum, 3, std::numeric_limits<float>::infinity());
  std::fill_n(out.maximum, 3, -std::numeric_limits<float>::infinity());
  out.w_minimum = std::numeric_limits<float>::infinity();
  out.w_maximum = -std::numeric_limits<float>::infinity();
  float ndc[4][3];
  unsigned corners = 0;
  bool full = true;
  for (unsigned v = 0; v < 4; ++v) {
    float position[4] = {0,0,0,1}, view[4], clip[4];
    std::memcpy(position, draw.geometry.vertices.data() + v * gxc::kVertexStrideBytes,
                3 * sizeof(float));
    transform(draw.position_matrix, position, view);
    transform(draw.projection, view, clip);
    // Match the vertex shader's GX-to-GL clip-Z remap in gx_cg.cpp.
    clip[2] = 2 * clip[2] + clip[3];
    out.w_minimum = std::min(out.w_minimum, clip[3]);
    out.w_maximum = std::max(out.w_maximum, clip[3]);
    if (!std::isfinite(clip[3]) || clip[3] == 0)
      return out;
    for (unsigned axis = 0; axis < 3; ++axis) {
      ndc[v][axis] = clip[axis] / clip[3];
      if (!std::isfinite(ndc[v][axis]))
        return out;
      out.minimum[axis] = std::min(out.minimum[axis], ndc[v][axis]);
      out.maximum[axis] = std::max(out.maximum[axis], ndc[v][axis]);
    }
    full &= clip[3] > 0 && std::fabs(std::fabs(ndc[v][0]) - 1) <= 0.02f &&
            std::fabs(std::fabs(ndc[v][1]) - 1) <= 0.02f;
    corners |= 1u << ((ndc[v][0] > 0 ? 1 : 0) + (ndc[v][1] > 0 ? 2 : 0));
  }
  for (unsigned triangle = 0; triangle < 2; ++triangle) {
    const auto* indices = draw.geometry.indices.data() + triangle * 3;
    if (indices[0] >= 4 || indices[1] >= 4 || indices[2] >= 4)
      return out;
    const auto *a = ndc[indices[0]], *b = ndc[indices[1]], *c = ndc[indices[2]];
    out.twice_area[triangle] = (b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0]);
  }
  out.valid = true;
  out.fullscreen = full && corners == 15;
  return out;
}

DrawInfo describe(const gxc::DrawPlan& plan, const TranslatedDraw& draw) {
  DrawInfo info;
  const auto& key = plan.pipeline.shader;
  info.required_mask = gxc::used_texmap_mask(key);
  info.is_quad = draw.geometry.vertex_count == 4 && draw.geometry.indices.size() == 6 &&
              draw.pipeline.topology == kGlTriangles;
  if (info.is_quad)
    info.geometry = geometry_stats(draw);
  info.fullscreen = info.geometry.fullscreen;
  for (unsigned t = 0; t < 8; ++t) {
    if ((info.required_mask & (1u << t)) == 0)
      continue;
    const auto& texture = draw.texmap_mask ? draw.textures[t] : draw.texture;
    if (!texture.valid)
      info.decode_failed_mask |= 1u << t;
    const unsigned format = plan.texmap_mask ? plan.textures[t].format : plan.tex_format;
    if (!info.is_quad || !plan.has_texture || draw.pipeline.depth_test ||
        draw.pipeline.depth_write || (format != 3 && format != 9))
      continue;
    if (!key.tev_valid && t == 0 && unit_coordinates(draw, key, 0))
      info.logo_mask |= 1u;
    for (unsigned n = 0; n < key.num_tev_stages && n < gxc::kMaxTevStages; ++n) {
      const auto& stage = key.tev_stages[n];
      if (stage.tevorders_enable && stage.tevorders_texmap == t &&
          unit_coordinates(draw, key, stage.tevorders_texcoord))
        info.logo_mask |= 1u << t;
    }
  }
  if (info.logo_mask) {
    unsigned slot = 0;
    while (!(info.logo_mask & (1u << slot))) ++slot;
    info.logo_texmap = slot;
    info.logo_texture = plan.texmap_mask
        ? TextureIdentity{plan.textures[slot].address, plan.textures[slot].format,
                          plan.textures[slot].width, plan.textures[slot].height}
        : TextureIdentity{plan.tex_address, plan.tex_format, plan.tex_width, plan.tex_height};
    info.alpha_test[0] = key.alpha_comp0;
    info.alpha_test[1] = key.alpha_comp1;
    info.alpha_test[2] = key.alpha_logic;
    info.alpha_stage_count = key.tev_valid ? std::min<std::uint32_t>(key.num_tev_stages, 16) : 0;
    for (unsigned n = 0; n < info.alpha_stage_count; ++n) {
      const auto& s = key.tev_stages[n];
      info.alpha_stages[n] = s.ac_a | (s.ac_b << 3) | (s.ac_c << 6) | (s.ac_d << 9) |
          (s.ac_bias << 12) | (s.ac_op << 14) | (s.ac_clamp << 15) | (s.ac_scale << 16) |
          (s.ac_dest << 18) | (s.tevorders_colorchan << 20) | (s.ras_swap[3] << 23) |
          (s.tex_swap[3] << 25) | (static_cast<std::uint32_t>(s.ksel_ka) << 27);
    }
    if (draw.geometry.vertices.size() == 4 * gxc::kVertexStrideBytes) {
      for (unsigned channel = 0; channel < 2; ++channel) {
        const auto offset = channel ? gxc::kVertexColor1Offset : gxc::kVertexColor0Offset;
        for (unsigned v = 0; v < 4; ++v)
          std::memcpy(&info.raster_alpha[channel][v], draw.geometry.vertices.data() +
                      v * gxc::kVertexStrideBytes + offset + 3 * sizeof(float), sizeof(float));
      }
    }
  }
  return info;
}

void count_bind(Counts& counts, BindStatus status) {
  switch (status) {
  case BindStatus::Valid: ++counts.bind_valid; break;
  case BindStatus::Skipped: ++counts.bind_skipped; break;
  case BindStatus::BindFailed: ++counts.bind_failed; break;
  case BindStatus::UploadFailed: ++counts.upload_failed; break;
  }
}

void count_draw(Counts& counts, const DrawInfo& info,
                std::uint32_t valid_mask, std::uint32_t skipped_mask) {
  if (!info.is_quad) ++counts.other;
  else if (info.required_mask) ++counts.textured_quad;
  else ++counts.untextured_quad;
  counts.fullscreen += info.fullscreen;
  counts.no_valid_texture += info.required_mask && !(info.required_mask & valid_mask);
  counts.incomplete_texture += (info.required_mask & valid_mask) != info.required_mask;
  counts.decode_failed += bit_count(info.decode_failed_mask);
  counts.required_skipped += bit_count(info.required_mask & skipped_mask);
  if (info.logo_mask) {
    ++counts.logo;
    counts.logo_valid += (info.logo_mask & valid_mask) == info.logo_mask;
    counts.logo_skipped += (info.logo_mask & skipped_mask) != 0;
  }
}

} // namespace gx_vita::census
