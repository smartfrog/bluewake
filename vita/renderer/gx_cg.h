// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 Cg generation from a gxcore ShaderKey.
//
// Bounded on purpose: only the shapes the fixtures exercise generate a
// program, and everything else fails with a reason instead of emitting a
// silently wrong shader. Supported today:
//   - untextured draws: vertex colour passthrough;
//   - textured draws without a TEV: texmap 0 sampled with texcoord 0;
//   - TEV combines (up to kMaxTevStages), all register/KONST arguments,
//     separate colour/alpha destinations, bias, scale, compare and swap tables;
//     each stage's
//     texcoord is a generated STQ coordinate from the translation's texture
//     matrix pass (position/normal/raw UV or Color0/Color1 source).
//   - BP F3 alpha comparisons on the TEV path.
//   - late ZTexture U8/U16/U24 ADD/REPLACE with 24-bit wrap and DEPTH output.
//   - GX linear/exponential/backwards fog, native-X range table and RGB blend.
//   - four indirect orders, formats, bias, bump alpha, matrices, wrap/add-prev.
//   - emboss coordinates prepared by CPU translation when the plan has lights.
//   - BP 0x42 constant destination alpha in the mono-pass when the colour pass
//     cannot observe the fragment alpha (WW_RENDERER_DST_ALPHA_ADMIT; off by
//     default).
// Integer TEV steps use exactly representable float arithmetic and floor;
// the pinned WGSL generator remains the semantics reference. This does not
// imply GPU pixel parity for sampling, lighting or unsupported texgens.

#pragma once

#include "gxruntime/gxcore/gxcore.hpp"

#include <string>

namespace gx_vita {

struct CgProgram {
  bool ok = false;
  const char* unsupported_reason = nullptr; // set when ok == false
  std::string vertex;
  std::string fragment;
};

// Generate the Cg program pair for a shader key; equal keys yield
// byte-identical sources.
CgProgram generate_cg(const gxruntime::gxcore::ShaderKey& key);

#if defined(WW_RENDERER_DST_ALPHA_ADMIT)
// Full-pipeline entry: admits the BP 0x42 constant destination alpha in the
// mono-pass when gx_dst_alpha.h says the colour channel cannot observe the
// fragment alpha. The shader-key entry above has no blend facts, so it keeps
// refusing every destination-alpha key.
CgProgram generate_cg(const gxruntime::gxcore::PipelineKey& key);
#endif

} // namespace gx_vita
