// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 GX -> vitaGL translation.
//
// One gxcore DrawPlan becomes the exact command set a vitaGL submission would
// issue: pipeline state in the GL token space, the decoded geometry, and the
// texture decoded to RGBA8 by the pinned codecs. The structs are
// backend-neutral, so the host records and checks them without a GPU and the
// Vita side submits the same values through vitaGL.

#pragma once

#include "gxruntime/gxcore/gxcore.hpp"
#include "gxruntime/gxcore/texture_decode.hpp"

#include <cstdint>
#include <vector>

namespace gx_vita {

// GL token values used by vitaGL (GLES2 numbers, stable across GL versions).
inline constexpr std::uint16_t kGlZero = 0x0000;
inline constexpr std::uint16_t kGlOne = 0x0001;
inline constexpr std::uint16_t kGlSrcAlpha = 0x0302;
inline constexpr std::uint16_t kGlOneMinusSrcAlpha = 0x0303;
inline constexpr std::uint16_t kGlDstColor = 0x0306;
inline constexpr std::uint16_t kGlOneMinusDstColor = 0x0307;
inline constexpr std::uint16_t kGlSrcColor = 0x0300;
inline constexpr std::uint16_t kGlOneMinusSrcColor = 0x0301;
inline constexpr std::uint16_t kGlDstAlpha = 0x0304;
inline constexpr std::uint16_t kGlOneMinusDstAlpha = 0x0305;
inline constexpr std::uint16_t kGlNever = 0x0200;
inline constexpr std::uint16_t kGlLess = 0x0201;
inline constexpr std::uint16_t kGlEqual = 0x0202;
inline constexpr std::uint16_t kGlLequal = 0x0203;
inline constexpr std::uint16_t kGlGreater = 0x0204;
inline constexpr std::uint16_t kGlNequal = 0x0205;
inline constexpr std::uint16_t kGlGequal = 0x0206;
inline constexpr std::uint16_t kGlAlways = 0x0207;
inline constexpr std::uint16_t kGlBack = 0x0405;
inline constexpr std::uint16_t kGlFront = 0x0404;
inline constexpr std::uint16_t kGlRepeat = 0x2901;
inline constexpr std::uint16_t kGlClampToEdge = 0x812F;
inline constexpr std::uint16_t kGlMirroredRepeat = 0x8370;
inline constexpr std::uint16_t kGlNearest = 0x2600;
inline constexpr std::uint16_t kGlLinear = 0x2601;

// Pipeline state as vitaGL consumes it. program_key hashes the gxcore
// ShaderKey (Cg program selection); equal keys mean equal Cg sources.
struct PipelineState {
  std::uint8_t depth_test = 0;
  std::uint16_t depth_func = kGlAlways;
  std::uint8_t depth_write = 0;
  std::uint8_t blend_enable = 0;
  std::uint16_t blend_src = kGlOne;
  std::uint16_t blend_dst = kGlZero;
  std::uint8_t cull_enable = 0;
  std::uint16_t cull_face = kGlBack;
  std::uint8_t color_write = 0;
  std::uint8_t alpha_write = 0;
  std::uint64_t program_key = 0;
};

struct Geometry {
  std::vector<std::uint8_t> vertices; // gxcore fixed layout, float32
  std::vector<std::uint16_t> indices; // triangle list
  std::uint32_t vertex_count = 0;
};

struct TextureUpload {
  bool valid = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba; // decoded RGBA8, width * height * 4 bytes
  std::uint16_t wrap_s = kGlRepeat;
  std::uint16_t wrap_t = kGlRepeat;
  std::uint16_t mag_filter = kGlLinear;
  std::uint16_t min_filter = kGlLinear;
};

struct TranslatedDraw {
  bool ok = false;
  const char* skip_reason = nullptr; // set when ok == false
  PipelineState pipeline;
  Geometry geometry;
  TextureUpload texture;
};

// Translate one draw plan. ok == false means the plan itself was not usable
// (plan.ok == false); feature gaps are reported by the caller's GapCounters,
// never silently dropped here.
TranslatedDraw translate(const gxruntime::gxcore::DrawPlan& plan);

// FNV-1a 64 over the ShaderKey bytes; 0 is reserved for "no key".
std::uint64_t shader_key_hash(const gxruntime::gxcore::ShaderKey& key);

// The GL token for a gxcore blend factor / compare mode / wrap mode.
std::uint16_t blend_factor_token(std::uint8_t factor);
std::uint16_t compare_mode_token(std::uint8_t mode);
std::uint16_t wrap_mode_token(std::uint8_t wrap);

} // namespace gx_vita
