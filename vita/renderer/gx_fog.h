// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gxruntime/gxcore/shader.hpp"

#include <cmath>
#include <cstring>
#include <string>

namespace gx_vita::fog {

namespace gxc = gxruntime::gxcore;

struct Uniforms {
  float params[4]{}; // A, C, normalized center, native width
  float color[4]{}; // integer RGB, alpha unused
  float depth[2]{}; // B magnitude, 2^B shift
  float range[3][4]{};
  float viewport[2]{}; // native width and left edge for WPOS conversion
};

inline const char* key_reason(const gxc::ShaderKey& key) {
  if (key.fog_fsel == 0)
    return nullptr;
  if (key.fog_fsel != 2 && (key.fog_fsel < 4 || key.fog_fsel > 7))
    return "invalid fog mode";
  if (key.fog_proj > 1 || key.fog_range > 1 || key.tev_valid == 0)
    return "invalid fog configuration";
  return nullptr;
}

inline bool translate(const gxc::DrawPlan& plan, Uniforms& out, const char** reason) {
  const auto& key = plan.pipeline.shader;
  *reason = key_reason(key);
  if (*reason != nullptr)
    return false;
  if (key.fog_fsel == 0)
    return true;
  const auto& constants = plan.pixel_constants;
  std::memcpy(out.params, constants.fogf, sizeof out.params);
  if (!std::isfinite(out.params[0]) || std::isnan(out.params[1]) ||
      (std::isinf(out.params[1]) && out.params[0] != 0) ||
      (key.fog_proj == 0 && !std::isfinite(out.params[0] * 16777216.f))) {
    *reason = "invalid fog coefficients";
    return false;
  }
  // Dolphin's inf/inf register case is predecoded as A=0, C=+/-inf.
  if (std::isinf(out.params[1]))
    out.params[1] = std::signbit(out.params[1]) ? -1.f : 1.f;
  for (int channel = 0; channel < 3; ++channel) {
    const auto color = constants.fogcolor[channel];
    if (color < 0 || color > 255) {
      *reason = "invalid fog color";
      return false;
    }
    out.color[channel] = color;
  }
  if (key.fog_proj == 0) {
    if (constants.fogi[1] < 0 || constants.fogi[1] > 0xFFFFFF ||
        constants.fogi[3] < 0 || constants.fogi[3] > 0xFFFFFF) {
      *reason = "invalid fog depth coefficients";
      return false;
    }
    out.depth[0] = constants.fogi[1];
    out.depth[1] = std::ldexp(1.f, constants.fogi[3] & 31);
  }
  if (key.fog_range != 0) {
    if (!plan.viewport_valid || !std::isfinite(plan.viewport[0]) || plan.viewport[0] <= 0 ||
        !std::isfinite(plan.viewport[3]) || !std::isfinite(out.params[2]) ||
        !std::isfinite(out.params[3]) || out.params[3] <= 0) {
      *reason = "invalid fog range viewport";
      return false;
    }
    // The pin uses 4*XF.wd; Dolphin uses 2*XF.wd. Preserve the decoded center.
    out.params[2] = 2.f * out.params[2] + 1.f;
    out.params[3] *= 0.5f;
    out.viewport[0] = 2.f * plan.viewport[0];
    out.viewport[1] = plan.viewport[3] - 342.f - plan.viewport[0];
    if (!std::isfinite(out.params[2]) || out.params[3] <= 0 ||
        !std::isfinite(out.viewport[0]) || !std::isfinite(out.viewport[1])) {
      *reason = "invalid fog range viewport";
      return false;
    }
    for (int index = 0; index < 10; ++index) {
      const float k = constants.fogrange[index / 4][index % 4];
      if (!std::isfinite(k) || k <= 0) {
        *reason = "invalid fog range coefficient";
        return false;
      }
      out.range[index / 4][index % 4] = k;
    }
  }
  return true;
}

inline void emit(std::string& out, const gxc::ShaderKey& key) {
  const char* depth = key.ztex_op != 0 ? "fragment_depth" : "screen.z";
  out += "  float fog_z = floor(clamp(" + std::string(depth) +
         " * 16777216.0, 0.0, 16777215.0));\n";
  if (key.fog_proj == 0) {
    out += "  float fog_denominator = fog_depth.x - floor(fog_z / fog_depth.y);\n"
           "  float fog_ze = (fog_params.x * 16777216.0) / "
           "(fog_denominator == 0.0 ? 1.0 : fog_denominator);\n";
  } else {
    out += "  float fog_ze = fog_params.x * fog_z / 16777216.0;\n";
  }
  if (key.fog_range != 0) {
    out += "  float fog_x = screen.x * fog_screen.x + fog_screen.y;\n"
           "  float fog_offset = 2.0 * (fog_x / fog_params.w) - 1.0 - fog_params.z;\n"
           "  float fog_index = clamp(9.0 - abs(fog_offset) * 9.0, 0.0, 9.0);\n"
           "  float fog_k = fog_range[0].x;\n";
    constexpr const char* components[] = {"x", "y", "z", "w"};
    for (int index = 1; index < 10; ++index)
      out += "  fog_k = lerp(fog_k, fog_range[" + std::to_string(index / 4) + "]." +
             components[index % 4] + ", clamp(fog_index - " +
             std::to_string(index - 1) + ".0, 0.0, 1.0));\n";
    out += "  fog_ze *= sqrt(fog_offset * fog_offset + fog_k * fog_k) / fog_k;\n";
  }
  out += "  float fog_value = clamp(fog_ze - fog_params.y, 0.0, 1.0);\n";
  if (key.fog_proj == 0)
    out += "  if (fog_denominator == 0.0 && fog_params.x != 0.0) "
           "fog_value = fog_params.x > 0.0 ? 1.0 : 0.0;\n";
  switch (key.fog_fsel) {
  case 4:
    out += "  fog_value = 1.0 - exp2(-8.0 * fog_value);\n";
    break;
  case 5:
    out += "  fog_value = 1.0 - exp2(-8.0 * fog_value * fog_value);\n";
    break;
  case 6:
    out += "  fog_value = exp2(-8.0 * (1.0 - fog_value));\n";
    break;
  case 7:
    out += "  fog_value = 1.0 - fog_value;\n"
           "  fog_value = exp2(-8.0 * fog_value * fog_value);\n";
    break;
  }
  // Explicit nearest-even agrees with WGSL round, including half-step ties.
  out += "  float fog_scaled = fog_value * 256.0;\n"
         "  float fog_integer = floor(fog_scaled);\n"
         "  float fog_fraction = fog_scaled - fog_integer;\n"
         "  float fog_alpha = fog_integer + ((fog_fraction > 0.5 || "
         "(fog_fraction == 0.5 && fog_integer - floor(fog_integer * 0.5) * 2.0 != 0.0)) "
         "? 1.0 : 0.0);\n";
  for (const char* channel : {"r", "g", "b"})
    out += "  prev." + std::string(channel) + " = floor((prev." + channel +
           " * (256.0 - fog_alpha) + fog_color." + channel + " * fog_alpha) / 256.0);\n";
}

} // namespace gx_vita::fog
