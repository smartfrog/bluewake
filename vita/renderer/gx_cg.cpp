// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 bounded Cg generation. See gx_cg.h.

#include "gx_cg.h"
#include "gx_tev.h"
#include "gx_ztexture.h"
#include "gx_texgen.h"
#include "gx_fog.h"
#include "gx_indirect.h"

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
  *reason = gx_vita::texgen::support_reason(key, texgen, texcoord);
  if (*reason != nullptr)
    return false;
  const std::string uv = "uv" + std::to_string(texcoord);
  *out = texgen.projection ? "(" + uv + ".xy / max(" + uv + ".z, 0.000001))"
                           : uv + ".xy";
  return true;
}

} // namespace

CgProgram generate_cg(const gxc::ShaderKey& key) {
  CgProgram program;
  if (key.num_tex_gens > gxc::kMaxTexGens ||
      key.num_tev_stages > gxc::kMaxTevStages ||
      (key.tev_valid != 0 && key.num_tev_stages == 0) ||
      key.alpha_comp0 > 7 || key.alpha_comp1 > 7 || key.alpha_logic > 3) {
    program.unsupported_reason = "invalid GX shader key";
    return program;
  }
  program.unsupported_reason = fog::key_reason(key);
  if (program.unsupported_reason != nullptr)
    return program;
  program.unsupported_reason = indirect::key_reason(key);
  if (program.unsupported_reason != nullptr)
    return program;
  const bool use_indirect = indirect::enabled(key);
  if (key.ztex_op > 2 || key.ztex_type > 2 ||
      (key.ztex_op != 0 && (key.tev_valid == 0 || key.textured == 0))) {
    program.unsupported_reason = "invalid ZTexture configuration";
    return program;
  }
  if (key.use_dst_alpha != 0) {
    program.unsupported_reason = "destination alpha requires dual-source blending";
    return program;
  }

  // Which vertex outputs the fragment needs: uv slots per stage texcoord and
  // the vertex colour for raster args or Colour texgens.
  bool need_uv[gxc::kMaxTexGens] = {};
  bool need_color0 = false;
  bool need_color1 = false;

  if (key.textured != 0u && key.tev_valid == 0u) {
    // Passthrough: texmap 0 on texcoord 0.
    need_uv[0] = true;
  }
  if (key.tev_valid != 0u) {
    for (std::uint32_t n = 0; n < key.num_tev_stages; ++n) {
      const gxc::TevStageKey& stage = key.tev_stages[n];
      if (!tev::valid_stage(stage)) {
        program.unsupported_reason = "invalid TEV stage fields";
        return program;
      }
      if (stage.tevorders_enable != 0u || (use_indirect && key.num_tex_gens != 0)) {
        const std::uint8_t texcoord = use_indirect
            ? indirect::coord(key, stage.tevorders_texcoord) : stage.tevorders_texcoord;
        std::string coord;
        if (!texgen_coord(key, texcoord, &coord,
                          &program.unsupported_reason))
          return program;
        const gxc::TexGenKey& texgen = key.tex_gens[texcoord];
        if (texgen.texgentype == static_cast<std::uint8_t>(gxc::TexGenType::Color0))
          need_color0 = true;
        if (texgen.texgentype == static_cast<std::uint8_t>(gxc::TexGenType::Color1))
          need_color1 = true;
        need_uv[texcoord] = true;
      }
      need_color0 |= stage.tevorders_colorchan == 0;
      need_color1 |= stage.tevorders_colorchan == 1;
      if (indirect::samples(key, stage)) {
        const auto texcoord = indirect::coord(key, key.ind_stages[stage.ind_stage].texcoord);
        std::string coord;
        if (!texgen_coord(key, texcoord, &coord, &program.unsupported_reason))
          return program;
        need_uv[texcoord] = true;
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
    for (std::uint32_t t = 0; t < gxc::kMaxTexGens; ++t)
      if (need_uv[t])
        out += ", float3 uv" + std::to_string(t) + " : TEXCOORD" +
               std::to_string(t);
    out += ",\n          uniform float4x4 position_matrix"
           ", uniform float4x4 projection"
           ",\n          out float4 clip : POSITION";
    if (need_color0)
      out += ", out float4 out_color0 : COLOR0";
    if (need_color1)
      out += ", out float4 out_color1 : COLOR1";
    for (std::uint32_t t = 0; t < gxc::kMaxTexGens; ++t)
      if (need_uv[t])
        out += ", out float3 out_uv" + std::to_string(t) + " : TEXCOORD" +
               std::to_string(t);
    out += ") {\n";
    out += "  clip = mul(mul(float4(position, 1.0), position_matrix), "
           "projection);\n";
    // GX clip Z is [-w,0]; vitaGL uses ordinary GL depth, not reversed Z.
    out += "  clip.z = 2.0 * clip.z + clip.w;\n";
    if (need_color0)
      out += "  out_color0 = color0;\n";
    if (need_color1)
      out += "  out_color1 = color1;\n";
    for (std::uint32_t t = 0; t < gxc::kMaxTexGens; ++t)
      if (need_uv[t])
        out += "  out_uv" + std::to_string(t) + " = uv" +
               std::to_string(t) + ";\n";
    out += "}\n";
  }

  // --- Fragment program -----------------------------------------------------
  {
    std::string& out = program.fragment;
    if (use_indirect)
      indirect::emit_helpers(out);
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
    for (std::uint32_t t = 0; t < gxc::kMaxTexGens; ++t)
      if (need_uv[t])
        param("float3 uv" + std::to_string(t) + " : TEXCOORD" +
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
      param("uniform float2 alpha_ref");
    }
    if (use_indirect) {
      param("uniform float4 texture_dimensions[8]");
      param("uniform float4 indirect_matrices[6]");
    }
    if (key.ztex_op != 0) {
      param("uniform float2 ztexture_bias");
      param("out float fragment_depth : DEPTH");
    }
    if (key.ztex_op == 1 || key.fog_fsel != 0)
      param("float4 screen : WPOS");
    if (key.fog_fsel != 0) {
      param("uniform float4 fog_params");
      param("uniform float4 fog_color");
      if (key.fog_proj == 0)
        param("uniform float2 fog_depth");
      if (key.fog_range != 0) {
        param("uniform float4 fog_range[3]");
        param("uniform float2 fog_screen");
      }
    }
    out += ") : COLOR {\n";

    if (key.tev_valid != 0u) {
      out += "  float4 prev = tev_colors[0];\n"
             "  float4 c0 = tev_colors[1];\n"
             "  float4 c1 = tev_colors[2];\n"
             "  float4 c2 = tev_colors[3];\n"
             "  float4 tex, ras, konst, tevin_a, tevin_b, tevin_c, tevin_d;\n";
      if (key.ztex_op != 0)
        out += "  float4 rawtex = float4(0.0,0.0,0.0,0.0);\n";
      if (use_indirect) {
        out += "  float alphabump = 0.0;\n"
               "  float2 tevcoord = float2(0.0,0.0);\n"
               "  float2 previous_coord = tevcoord;\n";
        if (key.num_tex_gens == 0)
          out += "  float2 fixpoint0 = float2(0.0,0.0);\n";
        for (std::uint32_t t = 0; t < key.num_tex_gens; ++t) {
          if (!need_uv[t])
            continue;
          std::string coordinate;
          const char* reason = nullptr;
          (void)texgen_coord(key, t, &coordinate, &reason);
          out += "  float2 indirect_coord" + std::to_string(t) + " = " + coordinate + ";\n";
          out += "  float2 fixpoint" + std::to_string(t) + " = float2(";
          for (int axis = 0; axis < 2; ++axis) {
            if (axis) out += ",";
            out += "gx_ind_trunc(indirect_coord" + std::to_string(t) + "." + "xy"[axis] +
                   " * texture_dimensions[" + std::to_string(t) + "]." + "zw"[axis] + " * 128.0)";
          }
          out += ");\n";
        }
        for (std::uint32_t i = 0; i < key.num_ind_stages; ++i) {
          bool used = false;
          for (std::uint32_t n = 0; n < key.num_tev_stages; ++n)
            used |= indirect::samples(key, key.tev_stages[n]) && key.tev_stages[n].ind_stage == i;
          if (!used)
            continue;
          const auto& stage = key.ind_stages[i];
          const auto base = "fixpoint" + std::to_string(indirect::coord(key, stage.texcoord));
          const auto dimensions = "texture_dimensions[" + std::to_string(stage.texmap) + "]";
          out += "  float2 indirect_uv" + std::to_string(i) + " = float2(";
          for (int axis = 0; axis < 2; ++axis) {
            if (axis) out += ",";
            out += "floor(" + base + "." + "xy"[axis] + " / " +
                   std::to_string(1u << (axis ? stage.scale_t : stage.scale_s)) + ".0) / (" +
                   dimensions + "." + "xy"[axis] + " * 128.0)";
          }
          out += ");\n";
          out += "  float3 ind_raw" + std::to_string(i) + " = floor(tex2D(tex" +
                 std::to_string(stage.texmap) + ", indirect_uv" + std::to_string(i) +
                 ").abg * 255.0 + 0.5);\n";
        }
      }
      for (std::uint32_t n = 0; n < key.num_tev_stages; ++n) {
        const gxc::TevStageKey& stage = key.tev_stages[n];
        std::string coord;
        if (use_indirect) {
          indirect::emit_coordinate(out, key, stage, n);
          coord = "float2(indirect_u" + std::to_string(n) + ",indirect_v" + std::to_string(n) + ")";
        } else if (stage.tevorders_enable != 0u) {
          const char* reason = nullptr;
          (void)texgen_coord(key, stage.tevorders_texcoord, &coord, &reason);
        }
        tev::emit_stage(out, stage, coord, key.ztex_op != 0,
                        use_indirect ? indirect::raster(stage) : std::string{});
      }
      const auto& last = key.tev_stages[key.num_tev_stages - 1];
      out += "  prev = float4(" + std::string(tev::destinations[last.cc_dest]) +
             ".rgb, " + tev::destinations[last.ac_dest] + ".a);\n";
      tev::emit_alpha_test(out, key);
      if (key.ztex_op != 0)
        ztexture::emit_depth(out, key);
      if (key.fog_fsel != 0)
        fog::emit(out, key);
      out += "  return prev / 255.0;\n";
    } else if (key.textured != 0u) {
      out += key.num_tex_gens > 0 && key.tex_gens[0].projection
                 ? "  return tex2D(tex0, uv0.xy / max(uv0.z, 0.000001));\n"
                 : "  return tex2D(tex0, uv0.xy);\n";
    } else {
      out += "  return float4(color0.rgb, color0.a);\n";
    }
    out += "}\n";
  }

  program.ok = true;
  return program;
}

} // namespace gx_vita
