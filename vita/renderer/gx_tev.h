// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gxruntime/gxcore/shader.hpp"

#include <string>

namespace gx_vita::tev {

inline constexpr const char* color_args[] = {
    "prev.rgb", "prev.aaa", "c0.rgb", "c0.aaa", "c1.rgb", "c1.aaa",
    "c2.rgb", "c2.aaa", "tex.rgb", "tex.aaa", "ras.rgb", "ras.aaa",
    "float3(255.0,255.0,255.0)", "float3(128.0,128.0,128.0)", "konst.rgb",
    "float3(0.0,0.0,0.0)"};
inline constexpr const char* alpha_args[] = {
    "prev.a", "c0.a", "c1.a", "c2.a", "tex.a", "ras.a", "konst.a", "0.0"};
inline constexpr const char* destinations[] = {"prev", "c0", "c1", "c2"};

inline std::string alpha_condition(std::uint8_t comparison, const char* ref) {
  if (comparison == 0)
    return "false";
  if (comparison == 7)
    return "true";
  constexpr const char* operators[] = {"", " < ", " == ", " <= ",
                                      " > ", " != ", " >= "};
  return "prev.a" + std::string(operators[comparison]) + ref;
}

inline void emit_alpha_test(std::string& out,
                            const gxruntime::gxcore::ShaderKey& key) {
  constexpr const char* logic[] = {" && ", " || ", " != ", " == "};
  out += "  if (!((" + alpha_condition(key.alpha_comp0, "alpha_ref.x") + ")" +
         logic[key.alpha_logic] + "(" +
         alpha_condition(key.alpha_comp1, "alpha_ref.y") + "))) { discard; }\n";
}

inline std::string swizzle(const std::uint8_t* channels) {
  std::string result;
  for (int i = 0; i < 4; ++i)
    result += "rgba"[channels[i]];
  return result;
}

inline std::string konst_selector(std::uint8_t selector, bool alpha) {
  constexpr int fractions[] = {255, 223, 191, 159, 128, 96, 64, 32};
  std::string value;
  if (selector < 8)
    value = std::to_string(fractions[selector]) + ".0";
  else if (selector < 12 || (alpha && selector < 16))
    value = "0.0";
  else {
    value = "konst_colors[" + std::to_string(selector & 3) + "].";
    if (selector < 16)
      return value + "rgb";
    const char channel = "rgba"[(selector - 16) / 4];
    return value + (alpha ? std::string(1, channel) : std::string(3, channel));
  }
  return alpha ? value : "float3(" + value + "," + value + "," + value + ")";
}

// Float arithmetic keeps GX's integer steps without depending on native integer
// ALU support in the Vita Cg profile. The ranges here are exactly representable.
inline std::string regular_combine(bool alpha, std::uint8_t bias,
                                   std::uint8_t op, std::uint8_t scale) {
  const std::string component = alpha ? ".a" : ".rgb";
  const std::string a = "tevin_a" + component;
  const std::string b = "tevin_b" + component;
  const std::string c = "tevin_c" + component;
  const std::string d = "tevin_d" + component;
  const char* factor = scale == 1 ? "2.0" : scale == 2 ? "4.0" : "1.0";
  const std::string lerp = "(" + a + " * 256.0 + (" + b + " - " + a +
                           ") * (" + c + " + floor(" + c + " / 128.0)))";
  const char* rounding = scale == 3 ? "0.0" : op ? "127.0" : "128.0";
  const char* offset = bias == 1 ? "128.0" : bias == 2 ? "-128.0" : "0.0";
  std::string result = "((" + d + " + " + offset + ") * " + factor +
                       (op ? " - " : " + ") + "floor((" + lerp + " * " +
                       factor + " + " + rounding + ") / 256.0))";
  if (scale == 3)
    result = "floor(" + result + " * 0.5)";
  return result;
}

inline std::string compare_combine(bool alpha, std::uint8_t op,
                                   std::uint8_t mode) {
  const std::string cmp = op ? " == " : " > ";
  const std::string component = alpha ? ".a" : ".rgb";
  std::string condition;
  if (mode < 3) {
    const char* weights[] = {"float3(1.0,0.0,0.0)", "float3(1.0,256.0,0.0)",
                             "float3(1.0,256.0,65536.0)"};
    condition = "(dot(tevin_a.rgb," + std::string(weights[mode]) + ")" + cmp +
                "dot(tevin_b.rgb," + weights[mode] + "))";
  } else if (alpha) {
    condition = "(tevin_a.a" + cmp + "tevin_b.a)";
  } else {
    std::string result = "tevin_d.rgb + float3(";
    for (int i = 0; i < 3; ++i) {
      if (i != 0)
        result += ",";
      const std::string channel(1, "rgb"[i]);
      result += "(tevin_a." + channel + cmp + "tevin_b." + channel +
                ") ? tevin_c." + channel + " : 0.0";
    }
    return result + ")";
  }
  const std::string zero = alpha ? "0.0" : "float3(0.0,0.0,0.0)";
  return "tevin_d" + component + " + (" + condition + " ? tevin_c" +
         component + " : " + zero + ")";
}

inline bool valid_stage(const gxruntime::gxcore::TevStageKey& s) {
  if (s.cc_a > 15 || s.cc_b > 15 || s.cc_c > 15 || s.cc_d > 15 ||
      s.ac_a > 7 || s.ac_b > 7 || s.ac_c > 7 || s.ac_d > 7 ||
      s.cc_bias > 3 || s.ac_bias > 3 || s.cc_scale > 3 || s.ac_scale > 3 ||
      s.cc_dest > 3 || s.ac_dest > 3 || s.cc_op > 1 || s.ac_op > 1 ||
      s.ksel_kc > 31 || s.ksel_ka > 31 || s.tevorders_texmap > 7)
    return false;
  for (int i = 0; i < 4; ++i)
    if (s.ras_swap[i] > 3 || s.tex_swap[i] > 3)
      return false;
  return true;
}

inline void emit_stage(std::string& out,
                       const gxruntime::gxcore::TevStageKey& s,
                       const std::string& coordinate, bool preserve_raw = false) {
  if (s.tevorders_enable != 0) {
    out += "  tex = floor(tex2D(tex" + std::to_string(s.tevorders_texmap) +
           ", " + coordinate + ") * 255.0 + 0.5);\n";
    if (preserve_raw)
      out += "  rawtex = tex;\n";
    out += "  tex = tex." + swizzle(s.tex_swap) + ";\n";
  } else {
    out += "  tex = float4(255.0,255.0,255.0,255.0);\n";
  }
  if (s.tevorders_colorchan <= 1) {
    out += "  ras = floor(color" + std::to_string(s.tevorders_colorchan) +
           "." + swizzle(s.ras_swap) + " * 255.0 + 0.5);\n";
  } else {
    out += "  ras = float4(0.0,0.0,0.0,0.0);\n";
  }
  out += "  konst = float4(" + konst_selector(s.ksel_kc, false) + ", " +
         konst_selector(s.ksel_ka, true) + ");\n";
  const std::uint8_t color[] = {s.cc_a, s.cc_b, s.cc_c, s.cc_d};
  const std::uint8_t alpha[] = {s.ac_a, s.ac_b, s.ac_c, s.ac_d};
  for (int i = 0; i < 4; ++i) {
    const std::string name = "tevin_" + std::string(1, "abcd"[i]);
    out += "  " + name + " = float4(" + color_args[color[i]] + ", " +
           alpha_args[alpha[i]] + ");\n";
    if (i < 3)
      out += "  " + name + " -= floor(" + name + " / 256.0) * 256.0;\n";
  }
  const auto combine = [&](bool a) {
    const auto bias = a ? s.ac_bias : s.cc_bias;
    const auto op = a ? s.ac_op : s.cc_op;
    const auto scale = a ? s.ac_scale : s.cc_scale;
    return bias == 3 ? compare_combine(a, op, scale)
                     : regular_combine(a, bias, op, scale);
  };
  out += "  " + std::string(destinations[s.cc_dest]) + ".rgb = clamp(" +
         combine(false) + (s.cc_clamp ? ", 0.0, 255.0);\n"
                                     : ", -1024.0, 1023.0);\n");
  out += "  " + std::string(destinations[s.ac_dest]) + ".a = clamp(" +
         combine(true) + (s.ac_clamp ? ", 0.0, 255.0);\n"
                                   : ", -1024.0, 1023.0);\n");
}

} // namespace gx_vita::tev
