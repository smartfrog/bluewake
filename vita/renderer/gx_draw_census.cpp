// SPDX-License-Identifier: GPL-3.0-or-later
#include "gx_draw_census.h"
#include "gx_vita.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace gx_vita::census {
namespace {
namespace gxc = gxruntime::gxcore;

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

void inventory(Counts& counts, const DrawEntry& draw) {
  for (unsigned i = 0; i < counts.entry_count; ++i) {
    if (counts.entries[i].draw == draw) {
      ++counts.entries[i].count;
      return;
    }
  }
  if (counts.entry_count == counts.entries.size()) {
    ++counts.omitted;
    return;
  }
  counts.entries[counts.entry_count++] = {draw, 1};
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
    auto& entry = info.entries[t];
    entry.texture = plan.texmap_mask
        ? TextureIdentity{plan.textures[t].address, plan.textures[t].width,
                          plan.textures[t].height, plan.textures[t].format}
        : TextureIdentity{plan.tex_address, plan.tex_width, plan.tex_height, plan.tex_format};
    entry.slot = t;
    entry.four_vertices = draw.geometry.vertex_count == 4;
    entry.fullscreen = info.fullscreen;
    entry.depth_test = draw.pipeline.depth_test != 0;
    entry.depth_write = draw.pipeline.depth_write != 0;
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
  counts.textured += info.required_mask != 0;
  for (unsigned t = 0; t < info.entries.size(); ++t)
    if (info.required_mask & (1u << t))
      inventory(counts, info.entries[t]);
}

std::string format_frame(const Counts& c, std::uint32_t present, bool partial) {
  char prefix[512];
  std::snprintf(prefix, sizeof prefix,
      "[draw-census] present=%u partial=%u tq=%u uq=%u other=%u tx=%u rest=%u "
      "refused=%u invalid=%u skip=%u bindfail=%u uploadfail=%u draws=",
      present, unsigned(partial), c.textured_quad, c.untextured_quad, c.other, c.textured,
      c.omitted, c.refused, c.incomplete_texture, c.required_skipped, c.bind_failed, c.upload_failed);
  std::string line = prefix;
  if (!c.entry_count)
    line += '-';
  for (unsigned i = 0; i < c.entry_count; ++i) {
    const auto& entry = c.entries[i];
    const auto& d = entry.draw;
    const auto& t = d.texture;
    char record[128];
    std::snprintf(record, sizeof record, "%s%08x@%u,%ux%u,%u,%u,%u,%u%u*%u",
        i ? "|" : "", t.address, d.slot, t.width, t.height, t.format,
        unsigned(d.four_vertices), unsigned(d.fullscreen), unsigned(d.depth_test),
        unsigned(d.depth_write), entry.count);
    line += record;
  }
  return line;
}

} // namespace gx_vita::census
