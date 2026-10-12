// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gxruntime/gxcore/shader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace gx_vita::lighting {

namespace gxc = gxruntime::gxcore;

inline float dot(const float* a, const float* b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline bool normalize(float* value) {
  const float squared = dot(value, value);
  if (!std::isfinite(squared))
    return false;
  if (squared > 0) {
    const float inverse = 1.f / std::sqrt(squared);
    for (int i = 0; i < 3; ++i)
      value[i] *= inverse;
  }
  return true;
}

inline bool round_integer(float value, std::int64_t& result) {
  if (!std::isfinite(value))
    return false;
  const double input = value;
  double rounded = std::floor(input);
  const double fraction = input - rounded;
  if (fraction > 0.5 || (fraction == 0.5 && std::fmod(rounded, 2.0) != 0))
    rounded += 1;
  if (rounded < std::numeric_limits<std::int32_t>::min() ||
      rounded > std::numeric_limits<std::int32_t>::max())
    return false;
  result = static_cast<std::int64_t>(rounded);
  return true;
}

inline bool light_factor(const gxc::LightChanKey& channel, const gxc::GpuLight& light,
                         const float* position, const float* normal, float& factor) {
  float direction[3];
  for (int i = 0; i < 3; ++i)
    direction[i] = light.pos[i] - position[i];
  const float squared = dot(direction, direction);
  if (!normalize(direction))
    return false;
  if (dot(direction, direction) == 0)
    std::memcpy(direction, normal, 3 * sizeof(float));
  float attenuation = 1;
  if (channel.attnfunc == 1) {
    const float cosine = dot(normal, direction) >= 0 ? std::max(0.f, dot(normal, light.dir)) : 0;
    const float terms[3] = {1, cosine, cosine * cosine};
    float distance_coefficients[3];
    std::memcpy(distance_coefficients, light.distatt, sizeof distance_coefficients);
    if (channel.diffusefunc != 0 && !normalize(distance_coefficients))
      return false;
    const float denominator = dot(distance_coefficients, terms);
    if (denominator == 0)
      return false;
    attenuation = std::max(0.f, dot(light.cosatt, terms)) / denominator;
  } else if (channel.attnfunc == 3) {
    const float cosine = std::max(0.f, dot(direction, light.dir));
    const float cosine_terms[3] = {1, cosine, cosine * cosine};
    const float distance_terms[3] = {1, std::sqrt(squared), squared};
    const float denominator = dot(light.distatt, distance_terms);
    if (denominator == 0)
      return false;
    attenuation = std::max(0.f, dot(light.cosatt, cosine_terms)) / denominator;
  }
  float diffuse = 1;
  if (channel.diffusefunc == 1)
    diffuse = dot(direction, normal);
  else if (channel.diffusefunc == 2)
    diffuse = std::max(0.f, dot(direction, normal));
  factor = attenuation * diffuse;
  return std::isfinite(factor);
}

inline bool translate(const gxc::DrawPlan& plan, std::vector<std::uint8_t>& vertices,
                      const char** reason) {
  const auto& key = plan.pipeline.shader;
  if (key.num_color_chans > 2) {
    *reason = "invalid color channel count";
    return false;
  }
  const bool active[] = {gxc::channel_lit_path(key, 0), gxc::channel_lit_path(key, 1)};
  if (!active[0] && !active[1])
    return true;
  bool needs_lights = false;
  for (int channel = 0; channel < 2; ++channel) {
    if (!active[channel])
      continue;
    for (int part = 0; part < 2; ++part) {
      const auto& config = key.litchan[channel + 2 * part];
      needs_lights |= config.enablelighting && config.light_mask != 0;
      if (config.matsource > 1 || config.ambsource > 1 ||
          config.diffusefunc > 2 || config.attnfunc > 3) {
        *reason = "invalid XF lighting channel";
        return false;
      }
    }
  }
  for (std::uint32_t v = 0; v < plan.vertex_count; ++v) {
    const float* raw = plan.vertices.data() + v * gxc::kVertexFloats;
    auto* output = vertices.data() + v * gxc::kVertexStrideBytes;
    float position[3]{};
    float normal[3]{};
    if (needs_lights) {
      const float (*normal_rows)[4] = &plan.constants.posnormalmatrix[3];
      if (key.has_pos_mtx_idx) {
        std::memcpy(position, output, sizeof position);
        std::uint32_t row;
        std::memcpy(&row, raw + gxc::kVertexPosMtxOffset / 4, sizeof row);
        row &= 31;
        if (row > 29) {
          *reason = "lighting normal matrix row out of range";
          return false;
        }
        normal_rows = &plan.constants.normalmatrices[row];
      } else {
        for (int axis = 0; axis < 3; ++axis)
          position[axis] = dot(plan.constants.posnormalmatrix[axis], raw) +
                           plan.constants.posnormalmatrix[axis][3];
      }
      const float* normal_input = key.has_vertex_normal
                                     ? raw + gxc::kVertexNormalOffset / 4
                                     : plan.constants.cached_normal;
      for (int axis = 0; axis < 3; ++axis)
        normal[axis] = dot(normal_rows[axis], normal_input);
      if (!normalize(normal)) {
        *reason = "non-finite lighting normal";
        return false;
      }
    }
    for (int channel = 0; channel < 2; ++channel) {
      if (!active[channel])
        continue;
      const auto offset = gxc::kVertexColor0Offset + 16 * channel;
      const float* base = raw + offset / 4;
      float color[4];
      for (int part = 0; part < 2; ++part) {
        const auto& config = key.litchan[channel + 2 * part];
        const int begin = part == 0 ? 0 : 3;
        const int end = part == 0 ? 3 : 4;
        std::int64_t material[4]{}, accumulator[4]{};
        for (int c = begin; c < end; ++c) {
          std::int64_t vertex_color;
          if (!round_integer(base[c] * 255.f, vertex_color)) {
            *reason = "invalid lighting vertex color";
            return false;
          }
          material[c] = config.matsource ? vertex_color : plan.constants.materials[channel + 2][c];
          accumulator[c] = config.enablelighting
                               ? (config.ambsource ? vertex_color : plan.constants.materials[channel][c])
                               : 255;
        }
        if (config.enablelighting) {
          for (int l = 0; l < 8; ++l) {
            if ((config.light_mask & (1u << l)) == 0)
              continue;
            float factor;
            const auto& light = plan.constants.lights[l];
            if (!light_factor(config, light, position, normal, factor)) {
              *reason = "invalid lighting attenuation";
              return false;
            }
            for (int c = begin; c < end; ++c) {
              std::int64_t contribution;
              if (!round_integer(factor * light.color[c], contribution)) {
                *reason = "invalid lighting contribution";
                return false;
              }
              accumulator[c] += contribution;
            }
          }
        }
        for (int c = begin; c < end; ++c) {
          const auto acc = std::clamp<std::int64_t>(accumulator[c], 0, 255);
          color[c] = static_cast<float>((material[c] * (acc + (acc >> 7))) >> 8) / 255.f;
        }
      }
      std::memcpy(output + offset, color, sizeof color);
    }
  }
  return true;
}

} // namespace gx_vita::lighting
