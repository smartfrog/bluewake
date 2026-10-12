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
//     texcoord is a Regular texgen reading a raw uv0..4, or a Color0/Color1
//     texgen reading the vertex colour.
//   - BP F3 alpha comparisons on the TEV path.
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

} // namespace gx_vita
