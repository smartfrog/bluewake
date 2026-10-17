// SPDX-License-Identifier: GPL-3.0-or-later
// Destination-alpha admission for the bounded Cg generator.
//
// gxcore admits BP 0x42 (constant destination alpha) only when the enable bit,
// the alpha update and an RGBA6 target all hold, and then forces the alpha
// blend factors to ONE/ZERO so the stored alpha is the constant
// (gxcore.cpp:1247-1256, next to the factor forcing at 1249-1252). The WGSL
// generator - the pinned semantics reference - lowers every such draw with
// dual-source blending because one pass must serve two alphas: the constant
// for the alpha-channel write and the computed alpha for the colour blend's
// SRCALPHA/INVSRCALPHA factors.
//
// The mono-pass needs no second alpha when the colour channel cannot observe
// the fragment alpha at all: blending off, colour updates off, or colour
// factors outside SRCALPHA/INVSRCALPHA. This header is the single definition of
// that admission; the WGSL generator consumes it when RecompCore carries it.
// Subtract-mode draws stay refused until the ONE/ONE rule is verified on this
// path.

#pragma once

#include "gxruntime/gxcore/gxcore.hpp"

#include <cstdint>

namespace gx_vita {
namespace dst_alpha {

// GX blend factors SRCALPHA (4) and INVSRCALPHA (5) read the fragment alpha.
inline bool factor_reads_source_alpha(std::uint8_t factor) {
  return factor == 4u || factor == 5u;
}

// nullptr when one exact pass carries the draw: the alpha-channel factors are
// already ONE/ZERO and only the fragment's alpha output changes, so no extra
// draw or program variant is needed. Otherwise the reason to keep it refused.
inline const char* mono_pass_refusal(
    const gxruntime::gxcore::PipelineKey& key) {
  if (key.shader.use_dst_alpha == 0u)
    return nullptr;
  // GC subtract mode forces ONE/ONE on the reference line, but the Vita path
  // has not been verified against that rule: fail closed for now.
  if (key.blend_subtract != 0u)
    return "destination alpha with blend subtract stays refused until the ONE/ONE rule is verified here";
  if (key.blend_enable == 0u || key.color_update == 0u)
    return nullptr;
  if (factor_reads_source_alpha(key.src_factor) ||
      factor_reads_source_alpha(key.dst_factor))
    return "destination alpha conflicts with the colour blend source alpha (two-pass split required)";
  return nullptr;
}

} // namespace dst_alpha
} // namespace gx_vita
