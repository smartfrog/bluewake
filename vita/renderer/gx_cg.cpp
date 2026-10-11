// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 bounded Cg generation. See gx_cg.h.

#include "gx_cg.h"

#include <cstdio>
#include <cstring>

namespace gx_vita {

namespace gxc = gxruntime::gxcore;

namespace {

// --- TEV argument expressions (supported subset) -----------------------------

bool color_arg_expr(std::uint8_t arg, std::string* out, const char** reason) {
  switch (arg) {
  case 0u:
    *out = "prev.rgb";
    return true;
  case 1u:
    *out = "prev.aaa";
    return true;
  case 8u:
    *out = "tex.rgb";
    return true;
  case 9u:
    *out = "tex.aaa";
    return true;
  case 10u:
    *out = "color0.rgb";
    return true;
  case 11u:
    *out = "color0.aaa";
    return true;
  case 12u:
    *out = "float3(1.0, 1.0, 1.0)";
    return true;
  case 13u:
    *out = "float3(0.5, 0.5, 0.5)";
    return true;
  case 15u:
    *out = "float3(0.0, 0.0, 0.0)";
    return true;
  default: // C0/C1/C2 registers (2-7) and KONST (14) need uniforms the
           // bounded generator does not emit yet.
    *reason = "unsupported TEV colour argument";
    return false;
  }
}

bool alpha_arg_expr(std::uint8_t arg, std::string* out, const char** reason) {
  switch (arg) {
  case 0u:
    *out = "prev.a";
    return true;
  case 4u:
    *out = "tex.a";
    return true;
  case 5u:
    *out = "color0.a";
    return true;
  case 7u:
    *out = "0.0";
    return true;
  default: // C0/C1/C2 registers (1-3) and KONST (6) need uniforms.
    *reason = "unsupported TEV alpha argument";
    return false;
  }
}

bool scale_factor(std::uint8_t scale, const char** out, const char** reason) {
  switch (scale) {
  case 0u:
    *out = "1.0";
    return true;
  case 1u:
    *out = "0.5";
    return true;
  case 2u:
    *out = "0.25";
    return true;
  default: // Divide2 (3) has a different rounding path in the hardware.
    *reason = "unsupported TEV scale";
    return false;
  }
}

// The coordinate a stage's texgen produces.
bool texgen_coord(const gxc::ShaderKey& key, std::uint8_t texcoord,
                  std::string* out, const char** reason) {
  if (texcoord >= key.num_tex_gens || texcoord >= gxc::kMaxTexGens) {
    *reason = "stage texcoord beyond the enabled texgens";
    return false;
  }
  const gxc::TexGenKey& texgen = key.tex_gens[texcoord];
  switch (static_cast<gxc::TexGenType>(texgen.texgentype)) {
  case gxc::TexGenType::Regular:
    if (texgen.sourcerow !=
        static_cast<std::uint8_t>(gxc::TexSourceRow::Tex0) + texcoord) {
      *reason = "regular texgen reads a source row other than its own uv";
      return false;
    }
    *out = "uv" + std::to_string(texcoord);
    return true;
  case gxc::TexGenType::Color0:
    *out = "float2(color0.r, 0.5)";
    return true;
  case gxc::TexGenType::Color1:
    *out = "float2(color1.r, 0.5)";
    return true;
  default:
    *reason = "unsupported texgen type";
    return false;
  }
}

} // namespace

CgProgram generate_cg(const gxc::ShaderKey& key) {
  CgProgram program;

  // Which vertex outputs the fragment needs: uv slots per stage texcoord and
  // the vertex colour for raster args or Colour texgens.
  bool need_uv[gxc::kMaxRawTexCoords + 3] = {};
  bool need_color0 = false;
  bool need_color1 = false;

  if (key.textured != 0u && key.tev_valid == 0u) {
    // Passthrough: texmap 0 on texcoord 0.
    need_uv[0] = true;
  }
  if (key.tev_valid != 0u) {
    for (std::uint32_t n = 0; n < key.num_tev_stages; ++n) {
      const gxc::TevStageKey& stage = key.tev_stages[n];
      // An active indirect lookup (indirect stage in range) is not generated:
      // refusing is the only honest answer until the indirection path exists.
      const bool samples_indirect =
          stage.ind_matrix_index != 0u || stage.ind_bump_alpha != 0u;
      if (samples_indirect && stage.ind_stage < key.num_ind_stages) {
        program.unsupported_reason = "indirect TEV lookups not supported";
        return program;
      }
      if (stage.cc_bias != 0u) {
        program.unsupported_reason = "TEV bias not supported";
        return program;
      }
      const std::uint8_t args[4] = {stage.cc_a, stage.cc_b, stage.cc_c,
                                    stage.cc_d};
      const std::uint8_t aargs[4] = {stage.ac_a, stage.ac_b, stage.ac_c,
                                     stage.ac_d};
      for (int i = 0; i < 4; ++i) {
        std::string expression;
        if (!color_arg_expr(args[i], &expression,
                            &program.unsupported_reason))
          return program;
        if (!alpha_arg_expr(aargs[i], &expression,
                            &program.unsupported_reason))
          return program;
      }
      const char* scale = nullptr;
      if (!scale_factor(stage.cc_scale, &scale,
                        &program.unsupported_reason))
        return program;
      if (stage.cc_scale != stage.ac_scale) {
        program.unsupported_reason = "colour and alpha scales differ";
        return program;
      }
      if (stage.tevorders_enable != 0u) {
        const std::uint8_t texcoord = stage.tevorders_texcoord;
        std::string coord;
        if (!texgen_coord(key, texcoord, &coord,
                          &program.unsupported_reason))
          return program;
        const gxc::TexGenKey& texgen = key.tex_gens[texcoord];
        if (texgen.texgentype == static_cast<std::uint8_t>(gxc::TexGenType::Color0))
          need_color0 = true;
        if (texgen.texgentype == static_cast<std::uint8_t>(gxc::TexGenType::Color1))
          need_color1 = true;
        if (texgen.texgentype == static_cast<std::uint8_t>(gxc::TexGenType::Regular) &&
            texcoord <= gxc::kMaxRawTexCoords)
          need_uv[texcoord] = true;
      }
      for (int i = 0; i < 4; ++i) {
        if (args[i] == 10u || args[i] == 11u || aargs[i] == 5u)
          need_color0 = true;
      }
    }
  }
  if (key.tev_valid == 0u && key.textured == 0u)
    need_color0 = true; // vertex-colour passthrough

  // --- Vertex program -------------------------------------------------------
  {
    std::string& out = program.vertex;
    out += "void main(float3 position : POSITION";
    if (need_color0)
      out += ", float4 color0 : COLOR0";
    if (need_color1)
      out += ", float4 color1 : COLOR1";
    for (std::uint32_t t = 0; t < gxc::kMaxRawTexCoords; ++t)
      if (need_uv[t])
        out += ", float2 uv" + std::to_string(t) + " : TEXCOORD" +
               std::to_string(t);
    out += ",\n          uniform float4x4 position_matrix"
           ", uniform float4x4 projection"
           ",\n          out float4 clip : POSITION";
    if (need_color0)
      out += ", out float4 out_color0 : COLOR0";
    if (need_color1)
      out += ", out float4 out_color1 : COLOR1";
    for (std::uint32_t t = 0; t < gxc::kMaxRawTexCoords; ++t)
      if (need_uv[t])
        out += ", out float2 out_uv" + std::to_string(t) + " : TEXCOORD" +
               std::to_string(t);
    out += ") {\n";
    out += "  clip = mul(mul(float4(position, 1.0), position_matrix), "
           "projection);\n";
    if (need_color0)
      out += "  out_color0 = color0;\n";
    if (need_color1)
      out += "  out_color1 = color1;\n";
    for (std::uint32_t t = 0; t < gxc::kMaxRawTexCoords; ++t)
      if (need_uv[t])
        out += "  out_uv" + std::to_string(t) + " = uv" +
               std::to_string(t) + ";\n";
    out += "}\n";
  }

  // --- Fragment program -----------------------------------------------------
  {
    std::string& out = program.fragment;
    out += "float4 main(";
    bool first = true;
    auto param = [&](const std::string& text) {
      if (!first)
        out += ", ";
      out += text;
      first = false;
    };
    if (need_color0)
      param("float4 color0 : COLOR0");
    if (need_color1)
      param("float4 color1 : COLOR1");
    for (std::uint32_t t = 0; t < gxc::kMaxRawTexCoords; ++t)
      if (need_uv[t])
        param("float2 uv" + std::to_string(t) + " : TEXCOORD" +
              std::to_string(t));
    if (key.textured != 0u) {
      const std::uint32_t used = gxc::used_texmap_mask(key);
      for (std::uint32_t t = 0; t < 8u; ++t)
        if ((used & (1u << t)) != 0u)
          param("uniform sampler2D tex" + std::to_string(t));
    }
    out += ") : COLOR {\n";

    if (key.tev_valid != 0u) {
      out += "  float4 prev = float4(0.0, 0.0, 0.0, 0.0);\n";
      out += "  float4 tex = float4(1.0, 1.0, 1.0, 1.0);\n";
      for (std::uint32_t n = 0; n < key.num_tev_stages; ++n) {
        const gxc::TevStageKey& stage = key.tev_stages[n];
        if (stage.tevorders_enable != 0u) {
          std::string coord;
          const char* reason = nullptr;
          (void)texgen_coord(key, stage.tevorders_texcoord, &coord, &reason);
          char line[128];
          std::snprintf(line, sizeof line, "  tex = tex2D(tex%u, %s);\n",
                        stage.tevorders_texmap, coord.c_str());
          out += line;
        } else {
          out += "  tex = float4(1.0, 1.0, 1.0, 1.0);\n";
        }
        std::string a_rgb, b_rgb, c_rgb, d_rgb, a_a, b_a, c_a, d_a;
        const char* reason = nullptr;
        (void)color_arg_expr(stage.cc_a, &a_rgb, &reason);
        (void)color_arg_expr(stage.cc_b, &b_rgb, &reason);
        (void)color_arg_expr(stage.cc_c, &c_rgb, &reason);
        (void)color_arg_expr(stage.cc_d, &d_rgb, &reason);
        (void)alpha_arg_expr(stage.ac_a, &a_a, &reason);
        (void)alpha_arg_expr(stage.ac_b, &b_a, &reason);
        (void)alpha_arg_expr(stage.ac_c, &c_a, &reason);
        (void)alpha_arg_expr(stage.ac_d, &d_a, &reason);
        const char* scale = nullptr;
        (void)scale_factor(stage.cc_scale, &scale, &reason);
        const char* op = stage.cc_op == 0u ? "+" : "-";
        char line[512];
        std::snprintf(line, sizeof line,
                      "  prev = float4(clamp(%s %s ((%s + (%s - %s) * %s) * "
                      "%s), 0.0, 1.0), clamp(%s %s ((%s + (%s - %s) * %s) * "
                      "%s), 0.0, 1.0));\n",
                      d_rgb.c_str(), op, a_rgb.c_str(), b_rgb.c_str(),
                      a_rgb.c_str(), c_rgb.c_str(), scale, d_a.c_str(), op,
                      a_a.c_str(), b_a.c_str(), a_a.c_str(), c_a.c_str(),
                      scale);
        out += line;
      }
      out += "  return prev;\n";
    } else if (key.textured != 0u) {
      out += "  return tex2D(tex0, uv0);\n";
    } else {
      out += "  return float4(color0.rgb, color0.a);\n";
    }
    out += "}\n";
  }

  program.ok = true;
  return program;
}

} // namespace gx_vita
