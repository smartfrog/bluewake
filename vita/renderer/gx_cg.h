// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 Cg generation from a gxcore ShaderKey.
//
// Bounded on purpose: only the shapes the fixtures exercise generate a
// program, and everything else fails with a reason instead of emitting a
// silently wrong shader. Supported today:
//   - untextured draws: vertex colour passthrough;
//   - textured draws without a TEV: texmap 0 sampled with texcoord 0;
//   - regular TEV combines (up to kMaxTevStages) whose colour args are
//     Prev/Tex/Raster/Zero/One/Half and alpha args Prev/Tex/Raster/Zero/One,
//     with bias 0, add/subtract, clamp and 1x/2x/4x scale, where each stage's
//     texcoord is a Regular texgen reading its own uv, or a Color0/Color1
//     texgen reading the vertex colour.
// The combine is generated in float (a documented approximation of the
// hardware's integer TEV; the pinned WGSL generator remains the integer
// oracle).

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

} // namespace gx_vita
