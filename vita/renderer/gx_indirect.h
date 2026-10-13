// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gxruntime/gxcore/shader.hpp"

#include <string>

namespace gx_vita::indirect {

namespace gxc = gxruntime::gxcore;

inline bool enabled(const gxc::ShaderKey& key) {
  if (key.num_ind_stages != 0)
    return true;
  for (std::uint32_t n = 0; n < key.num_tev_stages && n < gxc::kMaxTevStages; ++n) {
    const auto& s = key.tev_stages[n];
    if (s.ind_matrix_index || s.ind_bump_alpha || s.ind_wrap_s || s.ind_wrap_t ||
        s.ind_add_prev || s.ind_use_original_lod)
      return true;
  }
  return false;
}

inline bool samples(const gxc::ShaderKey& key, const gxc::TevStageKey& s) {
  return s.ind_stage < key.num_ind_stages && (s.ind_matrix_index || s.ind_bump_alpha);
}

inline std::uint32_t coord(const gxc::ShaderKey& key, std::uint32_t index) {
  return index < key.num_tex_gens ? index : 0;
}

inline const char* key_reason(const gxc::ShaderKey& key) {
  if (key.num_ind_stages > gxc::kMaxIndirectStages || key.num_tev_stages > gxc::kMaxTevStages)
    return "invalid indirect stage count";
  if (!enabled(key))
    return nullptr;
  if (!key.tev_valid)
    return "indirect texturing requires TEV state";
  for (std::uint32_t i = 0; i < key.num_ind_stages; ++i) {
    const auto& s = key.ind_stages[i];
    if (s.texmap > 7 || s.texcoord > 7 || s.scale_s > 15 || s.scale_t > 15)
      return "invalid indirect order fields";
  }
  for (std::uint32_t n = 0; n < key.num_tev_stages; ++n) {
    const auto& s = key.tev_stages[n];
    if (s.tevorders_texcoord > 7 || s.ind_stage > 3 || s.ind_format > 3 ||
        s.ind_bias > 7 || s.ind_bump_alpha > 3 ||
        s.ind_matrix_index > 3 || s.ind_matrix_id > 2 || s.ind_wrap_s > 7 ||
        s.ind_wrap_t > 7 || s.ind_add_prev > 1 || s.ind_use_original_lod > 1)
      return "invalid indirect TEV fields";
    if (s.ind_use_original_lod)
      return "original indirect LOD requires complete sampler state and captured mip levels";
    if (s.tevorders_enable && (!key.textured || !key.num_tex_gens))
      return "indirect direct lookup lacks texture coordinates/resources";
    if (samples(key, s) && (!key.textured || !key.num_tex_gens))
      return "indirect lookup lacks texture coordinates/resources";
  }
  return nullptr;
}

inline std::string wrap(const std::string& value, std::uint8_t mode) {
  if (mode == 0)
    return "gx_ind_wrap24(" + value + ")";
  if (mode >= 6)
    return "0.0";
  const auto period = (256u >> (mode - 1)) * 128u;
  const auto p = std::to_string(period) + ".0";
  return "(" + value + " - floor(" + value + " / " + p + ") * " + p + ")";
}

inline void emit_helpers(std::string& out) {
  out += "float gx_ind_trunc(float x) { return x < 0.0 ? -floor(-x) : floor(x); }\n"
         "float gx_ind_wrap24(float x) {\n"
         "  float r = x - floor(x / 16777216.0) * 16777216.0;\n"
         "  return r >= 8388608.0 ? r - 16777216.0 : r;\n"
         "}\n"
         "float gx_ind_scale(float x, float scale) {\n"
         "  if (scale <= 1.0) return floor(x * scale);\n"
         "  float period = 16777216.0 / scale;\n"
         "  float low = x - floor(x / period) * period;\n"
         "  return gx_ind_wrap24(low * scale);\n"
         "}\n"
         "float gx_ind_multiply(float x, float component) {\n"
         "  float high = floor(x / 65536.0);\n"
         "  float product = (x - high * 65536.0) * component;\n"
         "  float carry = floor(product / 65536.0);\n"
         "  float low = product - carry * 65536.0;\n"
         "  high = high * component + carry;\n"
         "  high -= floor(high / 65536.0) * 65536.0;\n"
         "  if (high >= 32768.0) high -= 65536.0;\n"
         "  return high * 256.0 + floor(low / 256.0);\n"
         "}\n";
}

inline void emit_coordinate(std::string& out, const gxc::ShaderKey& key,
                            const gxc::TevStageKey& s, std::uint32_t n) {
  const auto suffix = std::to_string(n);
  const auto base = "fixpoint" + std::to_string(coord(key, s.tevorders_texcoord));
  out += "  float indirect_s" + suffix + " = " + base + ".x;\n";
  out += "  float indirect_t" + suffix + " = " + base + ".y;\n";
  const auto x = "indirect_x" + suffix;
  const auto y = "indirect_y" + suffix;
  out += "  float " + x + " = 0.0, " + y + " = 0.0;\n";
  if (samples(key, s)) {
    const auto raw = "ind_raw" + std::to_string(s.ind_stage);
    constexpr unsigned alpha_shift[] = {0, 5, 4, 3};
    constexpr unsigned format_shift[] = {0, 3, 4, 5};
    if (s.ind_bump_alpha) {
      const auto input = raw + "." + "xyz"[s.ind_bump_alpha - 1];
      out += "  float indirect_alpha" + suffix + " = floor(" + input + " * " +
             std::to_string(1u << alpha_shift[s.ind_format]) + ".0 / 8.0) * 8.0;\n";
      out += "  alphabump = indirect_alpha" + suffix + " - floor(indirect_alpha" +
             suffix + " / 256.0) * 256.0;\n";
    }
    if (s.ind_matrix_index) {
      const auto input = "ind_input" + suffix;
      out += "  float3 " + input + " = floor(" + raw + " / " +
             std::to_string(1u << format_shift[s.ind_format]) + ".0);\n";
      for (unsigned component = 0; component < 3; ++component)
        if (s.ind_bias & (1u << component))
          out += "  " + input + "." + "xyz"[component] + " += " +
                 (s.ind_format == 0 ? "-128.0" : "1.0") + ";\n";
      const auto matrix = 2 * (s.ind_matrix_index - 1);
      for (unsigned axis = 0; axis < 2; ++axis) {
        const auto destination = axis == 0 ? x : y;
        const auto row = "indirect_matrices[" + std::to_string(matrix + axis) + "]";
        if (s.ind_matrix_id == 0)
          out += "  " + destination + " = floor((" + row + ".x * " + input + ".x + " +
                 row + ".y * " + input + ".y + " + row + ".z * " + input + ".z) / 8.0);\n";
        else
          out += "  " + destination + " = gx_ind_multiply(" +
                 (axis == 0 ? "indirect_s" : "indirect_t") + suffix + ", " + input +
                 (s.ind_matrix_id == 1 ? ".x" : ".y") + ");\n";
        out += "  " + destination + " = gx_ind_scale(" + destination + ", " +
               "indirect_matrices[" + std::to_string(matrix) + "].w);\n";
      }
    }
  }
  for (unsigned axis = 0; axis < 2; ++axis) {
    const auto destination = std::string("tevcoord.") + "xy"[axis];
    const auto input = wrap((axis == 0 ? "indirect_s" : "indirect_t") + suffix,
                            axis == 0 ? s.ind_wrap_s : s.ind_wrap_t);
    const auto translation = axis == 0 ? x : y;
    out += "  " + destination + " = gx_ind_wrap24(" + input + " + " + translation + ");\n";
    if (s.ind_add_prev)
      out += "  " + destination + " = gx_ind_wrap24(" + destination + " + previous_coord." +
             "xy"[axis] + ");\n";
  }
  out += "  previous_coord = tevcoord;\n";
  const auto dimensions = "texture_dimensions[" + std::to_string(s.tevorders_texmap) + "]";
  out += "  float indirect_u" + suffix + " = tevcoord.x / (" + dimensions + ".x * 128.0);\n";
  out += "  float indirect_v" + suffix + " = tevcoord.y / (" + dimensions + ".y * 128.0);\n";
}

inline std::string raster(const gxc::TevStageKey& s) {
  if (s.tevorders_colorchan != 5 && s.tevorders_colorchan != 6)
    return {};
  const std::string value = s.tevorders_colorchan == 5 ? "alphabump"
                            : "(alphabump + floor(alphabump / 32.0))";
  return "float4(" + value + "," + value + "," + value + "," + value + ")";
}

} // namespace gx_vita::indirect
