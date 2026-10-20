// SPDX-License-Identifier: GPL-3.0-or-later
#include "gx_draw_census.h"
#include "gx_vita.h"
#include "gx_texgen.h"

#include <cmath>
#include <cstring>

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

bool fullscreen_quad(const TranslatedDraw& draw) {
  if (draw.geometry.vertices.size() != 4 * gxc::kVertexStrideBytes)
    return false;
  unsigned corners = 0;
  for (unsigned v = 0; v < 4; ++v) {
    float position[4] = {0, 0, 0, 1}, view[4], clip[4];
    std::memcpy(position, draw.geometry.vertices.data() + v * gxc::kVertexStrideBytes,
                3 * sizeof(float));
    transform(draw.position_matrix, position, view);
    transform(draw.projection, view, clip);
    if (!std::isfinite(clip[3]) || clip[3] <= 0)
      return false;
    const float x = clip[0] / clip[3], y = clip[1] / clip[3];
    if (!std::isfinite(x) || !std::isfinite(y) ||
        std::fabs(std::fabs(x) - 1) > 0.02f || std::fabs(std::fabs(y) - 1) > 0.02f)
      return false;
    corners |= 1u << ((x > 0 ? 1 : 0) + (y > 0 ? 2 : 0));
  }
  return corners == 15;
}

unsigned bit_count(std::uint32_t mask) {
  unsigned count = 0;
  for (unsigned t = 0; t < 8; ++t)
    count += (mask >> t) & 1u;
  return count;
}
} // namespace

DrawInfo describe(const gxc::DrawPlan& plan, const TranslatedDraw& draw) {
  DrawInfo info;
  const auto& key = plan.pipeline.shader;
  info.required_mask = gxc::used_texmap_mask(key);
  info.is_quad = draw.geometry.vertex_count == 4 && draw.geometry.indices.size() == 6 &&
              draw.pipeline.topology == kGlTriangles;
  info.fullscreen = info.is_quad && fullscreen_quad(draw);
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
