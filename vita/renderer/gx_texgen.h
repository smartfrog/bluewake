// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gxruntime/gxcore/shader.hpp"
#include "gx_lighting.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace gx_vita::texgen {

namespace gxc = gxruntime::gxcore;
inline constexpr std::uint32_t kFloatsPerVertex = 3 * gxc::kMaxTexGens;

inline const char* post_support_reason(std::uint8_t post_mask, std::uint8_t normalize_mask) {
  return (post_mask & normalize_mask) != 0
             ? "post-normalized texture matrix not supported" : nullptr;
}

inline const char* support_reason(const gxc::ShaderKey& key, const gxc::TexGenKey& gen,
                                  std::uint32_t destination) {
  const auto type = static_cast<gxc::TexGenType>(gen.texgentype);
  if (type == gxc::TexGenType::Color0 || type == gxc::TexGenType::Color1)
    return nullptr;
  if (type == gxc::TexGenType::EmbossMap) {
    if (!key.lit_valid)
      return "emboss light constants absent from pinned plan";
    if (gen.embosslightshift > 7 || gen.embosssourceshift >= key.num_tex_gens)
      return "invalid emboss source/light";
    if (gen.embosssourceshift >= destination)
      return "emboss source must precede destination";
    return nullptr;
  }
  if (type != gxc::TexGenType::Regular)
    return "invalid texgen type";
  if (gen.inputform > 1 || gen.projection > 1 || gen.sourcerow > 12)
    return "invalid regular texgen fields";
  if (gen.sourcerow >= 2 && gen.sourcerow < 5)
    return "regular color/binormal source not supported";
  if (gen.sourcerow >= 10 && key.raw_tex_hi_in_nbt == 0)
    return "raw texcoord exceeds Vita vertex layout";
  return nullptr;
}

inline bool translate(const gxc::DrawPlan& plan,
                      const std::vector<std::uint8_t>& vertices,
                      std::vector<float>& output, const char** reason) {
  const auto& key = plan.pipeline.shader;
  if (key.num_tex_gens > gxc::kMaxTexGens) {
    *reason = "invalid texgen count";
    return false;
  }
  for (std::uint32_t t = 0; t < key.num_tex_gens; ++t) {
    *reason = support_reason(key, key.tex_gens[t], t);
    if (*reason != nullptr)
      return false;
  }
  output.assign(static_cast<std::size_t>(plan.vertex_count) * kFloatsPerVertex, 0);
  for (std::uint32_t v = 0; v < plan.vertex_count; ++v) {
    const float* raw = plan.vertices.data() + v * gxc::kVertexFloats;
    float* generated = output.data() + v * kFloatsPerVertex;
    for (std::uint32_t t = 0; t < gxc::kMaxTexGens; ++t) {
      float* uv = generated + 3 * t;
      uv[2] = 1;
      if (t >= key.num_tex_gens) {
        if (t == 0)
          std::memcpy(uv, raw + gxc::kVertexUvOffset / 4, 2 * sizeof(float));
        continue;
      }
      const auto& gen = key.tex_gens[t];
      const auto type = static_cast<gxc::TexGenType>(gen.texgentype);
      if (type == gxc::TexGenType::EmbossMap) {
        lighting::VertexFrame frame;
        if (!lighting::vertex_frame(plan, raw,
                                    vertices.data() + v * gxc::kVertexStrideBytes,
                                    frame, reason))
          return false;
        float direction[3];
        for (int axis = 0; axis < 3; ++axis)
          direction[axis] = plan.constants.lights[gen.embosslightshift].pos[axis] - frame.position[axis];
        if (!lighting::normalize(direction)) {
          *reason = "non-finite emboss light direction";
          return false;
        }
        const float* tangent = key.has_vertex_tangent ? raw + gxc::kVertexTangentOffset / 4
                                                      : plan.constants.cached_tangent;
        const float* binormal = key.has_vertex_binormal ? raw + gxc::kVertexBinormalOffset / 4
                                                        : plan.constants.cached_binormal;
        float basis[2][3];
        for (int axis = 0; axis < 3; ++axis) {
          basis[0][axis] = lighting::dot(frame.normal_rows[axis], tangent);
          basis[1][axis] = lighting::dot(frame.normal_rows[axis], binormal);
        }
        std::memcpy(uv, generated + 3 * gen.embosssourceshift, 3 * sizeof(float));
        for (int axis = 0; axis < 2; ++axis) {
          uv[axis] += lighting::dot(direction, basis[axis]);
          if (!std::isfinite(uv[axis])) {
            *reason = "non-finite emboss coordinate";
            return false;
          }
        }
        continue;
      }
      if (type == gxc::TexGenType::Color0 || type == gxc::TexGenType::Color1) {
        const auto offset = type == gxc::TexGenType::Color0
                                ? gxc::kVertexColor0Offset : gxc::kVertexColor1Offset;
        std::memcpy(uv, vertices.data() + v * gxc::kVertexStrideBytes + offset,
                    2 * sizeof(float));
        continue;
      }
      float coord[4] = {0, 0, 1, 1};
      if (gen.sourcerow == 0) {
        std::memcpy(coord, raw, 3 * sizeof(float));
      } else if (gen.sourcerow == 1 && key.has_vertex_normal) {
        std::memcpy(coord, raw + gxc::kVertexNormalOffset / 4, 3 * sizeof(float));
      } else if (gen.sourcerow >= 5) {
        const auto source = gen.sourcerow - 5;
        if ((key.uv_mask & (1u << source)) != 0) {
          constexpr std::uint32_t high[] = {gxc::kVertexNormalOffset,
                                            gxc::kVertexBinormalOffset,
                                            gxc::kVertexTangentOffset};
          const auto offset = source < 5 ? gxc::kVertexUvOffset + 8 * source
                                          : high[source - 5];
          std::memcpy(coord, raw + offset / 4, 2 * sizeof(float));
        }
      }
      if (gen.inputform == 0)
        coord[2] = 1;
      const float (*rows)[4] = &plan.constants.texmatrices[3 * t];
      if (key.has_tex_mtx_idx && (key.tex_mtx_idx_mask & (1u << t))) {
        std::uint32_t packed;
        const auto offset = t < 4 ? gxc::kVertexTexMtxIdxOffset
                                  : gxc::kVertexTexMtxIdxHiOffset;
        std::memcpy(&packed, raw + offset / 4, sizeof packed);
        const auto row = (packed >> (8 * (t % 4))) & 255;
        if (row > (gen.projection ? 61u : 62u)) {
          *reason = "texture matrix row out of range";
          return false;
        }
        rows = &plan.constants.transformmatrices[row];
      }
      const std::uint32_t count = gen.projection ? 3 : 2;
      for (std::uint32_t axis = 0; axis < count; ++axis)
        uv[axis] = coord[0] * rows[axis][0] + coord[1] * rows[axis][1] +
                   coord[2] * rows[axis][2] + coord[3] * rows[axis][3];
      if (uv[2] == 0) {
        uv[0] = std::clamp(uv[0] * 0.5f, -1.f, 1.f);
        uv[1] = std::clamp(uv[1] * 0.5f, -1.f, 1.f);
      }
    }
  }
  return true;
}

} // namespace gx_vita::texgen
