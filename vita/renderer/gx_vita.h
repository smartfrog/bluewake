// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 GX -> vitaGL translation.
//
// One gxcore DrawPlan becomes the exact command set a vitaGL submission would
// issue: pipeline state in the GL token space, the decoded geometry, and the
// textures decoded to RGBA8 by the pinned codecs. The structs are
// backend-neutral, so the host records and checks them without a GPU and the
// Vita side submits the same values through vitaGL.
//
// Texture rules (mirroring gxcore.cpp's plan build): the flat `texture` field
// is the single-texmap case and the sampler is the one for that texmap; when
// the draw samples more than one texmap or a nonzero texmap the
// per-texmap `textures[]` set and `texmap_mask` are authoritative. Samplers
// are indexed by texmap, never assumed to be 0.

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
inline constexpr std::uint16_t kGlCcw = 0x0901;
inline constexpr std::uint16_t kGlCw = 0x0900;
inline constexpr std::uint16_t kGlRepeat = 0x2901;
inline constexpr std::uint16_t kGlClampToEdge = 0x812F;
inline constexpr std::uint16_t kGlMirroredRepeat = 0x8370;
inline constexpr std::uint16_t kGlNearest = 0x2600;
inline constexpr std::uint16_t kGlLinear = 0x2601;
inline constexpr std::uint16_t kGlFuncAdd = 0x8006;
inline constexpr std::uint16_t kGlFuncSubtract = 0x800A;
inline constexpr std::uint16_t kGlPoints = 0x0000;
inline constexpr std::uint16_t kGlLines = 0x0001;
inline constexpr std::uint16_t kGlTriangles = 0x0004;

// Pipeline state as vitaGL consumes it. program_key hashes the gxcore
// ShaderKey (Cg program selection); equal keys mean equal Cg sources.
struct PipelineState {
  std::uint8_t depth_test = 0;
  std::uint16_t depth_func = kGlAlways;
  std::uint8_t depth_write = 0;
  float depth_near = 0;
  float depth_far = 1;
  std::uint8_t blend_enable = 0;
  std::uint16_t blend_src = kGlOne;
  std::uint16_t blend_dst = kGlZero;
  // Separate alpha factors (glBlendFuncSeparate), already Dolphin-mapped by
  // gxcore (GX colour factors collapse to their alpha equivalents).
  std::uint16_t blend_src_alpha = kGlOne;
  std::uint16_t blend_dst_alpha = kGlZero;
  std::uint16_t blend_equation = kGlFuncAdd; // SUBTRACT -> kGlFuncSubtract
  // BP 0x42 constant destination alpha: the Cg fragment shader replaces the
  // output alpha with dst_alpha/255 when use_dst_alpha is set (vitaGL has no
  // blend colour, so this is a shader contract, not a GL state).
  std::uint8_t use_dst_alpha = 0;
  std::uint8_t dst_alpha = 0;
  std::uint8_t cull_enable = 0;
  std::uint16_t cull_face = kGlBack;
  // GX's winding maps to the GL default (counter-clockwise front faces);
  // the value is explicit here so a hardware check can change it in one place.
  std::uint16_t front_face = kGlCcw;
  std::uint8_t color_write = 0;
  std::uint8_t alpha_write = 0;
  std::uint16_t topology = kGlTriangles; // 0 triangles, 1 lines, 2 points
  std::uint64_t program_key = 0;
};

struct Geometry {
  std::vector<std::uint8_t> vertices; // gxcore fixed layout, float32
  std::vector<float> texcoords; // eight generated STQ triples per vertex
  std::vector<std::uint16_t> indices; // triangle/line/point list
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
  // The draw's transform, in the row-dot layout the generated Cg consumes
  // (clip = mul(mul(pos4, position_matrix), projection), row vectors):
  // position_matrix rows 0..2 are the XF position-matrix rows from the plan's
  // vertex constants, row 3 is (0,0,0,1); projection is the plan's
  // Dolphin-layout projection (rows are dot rows).
  float position_matrix[16]{};
  float projection[16]{};
  float tev_colors[4][4]{};
  float konst_colors[4][4]{};
  float alpha_ref[2]{};
  float ztexture_bias[2]{}; // exact high-16/low-8 parts of BP F4
  // Single texmap-0 case: the draw's one texture and sampler.
  TextureUpload texture;
  // Bit t set means textures[t] must be bound. Includes single nonzero texmaps;
  // only a single texmap-0 draw uses the flat texture field.
  std::uint32_t texmap_mask = 0;
  TextureUpload textures[8];
};

// Translate one draw plan. ok == false means the plan itself was not usable
// (plan.ok == false); feature gaps are reported by the caller's GapCounters,
// never silently dropped here.
TranslatedDraw translate(const gxruntime::gxcore::DrawPlan& plan);

// One EFB copy-to-texture as vitaGL would execute it: resolve the EFB region
// into the destination texture (render-to-texture or glCopyTexSubImage2D),
// then apply the clear the hardware performs after the copy, honouring the
// write masks at the copy. The display copy (format 0xF, GXCopyDisp) has no
// texture destination and is refused with a reason.
struct TranslatedCopy {
  bool ok = false;
  const char* skip_reason = nullptr; // set when ok == false
  std::uint32_t dest_address = 0;
  std::uint32_t byte_size = 0;
  std::uint32_t format = 0; // true GXTexFmt incl. Z (0x10) / CTF (0x20) bits
  std::uint32_t src_x = 0;
  std::uint32_t src_y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t dest_width = 0;
  std::uint32_t dest_height = 0;
  bool clear = false;
  bool color_update = true;
  bool alpha_update = true;
  bool depth_update = true;
  std::uint32_t clear_r = 0;
  std::uint32_t clear_g = 0;
  std::uint32_t clear_b = 0;
  std::uint32_t clear_a = 0;
  std::uint32_t clear_z = 0;
  bool efb_has_alpha = true;
};

TranslatedCopy translate_copy(const gxruntime::gxcore::EfbCopyCommand& cmd);

// --- EFB readback into guest memory ----------------------------------------
//
// The Vita backend reads an EFB rectangle as tightly packed RGBA8
// (glReadPixels / vglReadPixels); this copies it into the guest range the
// game will read, row by row, honouring the guest stride, and reports each
// written span through the dirty-mark callback so the recompiled game's
// memory view is coherent. Returns false (and writes nothing) when the
// rectangle is empty or does not fit the available guest bytes.

struct EfbReadback {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

using DirtyMarkFn = void (*)(void* user, std::uint32_t offset,
                             std::uint32_t size);

bool copy_readback_to_guest(const std::uint8_t* pixels,
                            const EfbReadback& rect,
                            std::uint32_t guest_stride, std::uint8_t* guest,
                            std::size_t available, DirtyMarkFn mark = nullptr,
                            void* mark_user = nullptr);

// FNV-1a 64 over the ShaderKey bytes; 0 is reserved for "no key".
std::uint64_t shader_key_hash(const gxruntime::gxcore::ShaderKey& key);

// The GL token for a gxcore blend factor / compare mode / wrap mode.
std::uint16_t blend_factor_token(std::uint8_t factor);
std::uint16_t compare_mode_token(std::uint8_t mode);
std::uint16_t wrap_mode_token(std::uint8_t wrap);

} // namespace gx_vita
