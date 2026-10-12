// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 bounded Cg generation. See gx_cg.h.

#include "gx_cg.h"
#include "gx_tev.h"

#include <cstdio>
#include <cstring>

namespace gx_vita {

namespace gxc = gxruntime::gxcore;

namespace {

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
  if (key.num_tex_gens > gxc::kMaxTexGens ||
      key.num_tev_stages > gxc::kMaxTevStages ||
      (key.tev_valid != 0 && key.num_tev_stages == 0)) {
    program.unsupported_reason = "invalid GX shader stage count";
    return program;
  }

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
      if (!tev::valid_stage(stage)) {
        program.unsupported_reason = "invalid TEV stage fields";
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
      need_color0 |= stage.tevorders_colorchan == 0;
      need_color1 |= stage.tevorders_colorchan == 1;
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
    if (key.tev_valid != 0) {
      param("uniform float4 tev_colors[4]");
      param("uniform float4 konst_colors[4]");
    }
    out += ") : COLOR {\n";

    if (key.tev_valid != 0u) {
      out += "  float4 prev = tev_colors[0];\n"
             "  float4 c0 = tev_colors[1];\n"
             "  float4 c1 = tev_colors[2];\n"
             "  float4 c2 = tev_colors[3];\n"
             "  float4 tex, ras, konst, tevin_a, tevin_b, tevin_c, tevin_d;\n";
      for (std::uint32_t n = 0; n < key.num_tev_stages; ++n) {
        const gxc::TevStageKey& stage = key.tev_stages[n];
        std::string coord;
        if (stage.tevorders_enable != 0u) {
          const char* reason = nullptr;
          (void)texgen_coord(key, stage.tevorders_texcoord, &coord, &reason);
        }
        tev::emit_stage(out, stage, coord);
      }
      const auto& last = key.tev_stages[key.num_tev_stages - 1];
      out += "  prev = float4(" + std::string(tev::destinations[last.cc_dest]) +
             ".rgb, " + tev::destinations[last.ac_dest] + ".a);\n";
      out += "  return prev / 255.0;\n";
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
