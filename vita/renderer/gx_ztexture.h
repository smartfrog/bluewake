// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gxruntime/gxcore/shader.hpp"

#include <string>

namespace gx_vita::ztexture {

inline void emit_depth(std::string& out, const gxruntime::gxcore::ShaderKey& key) {
  constexpr const char* sample[] = {"0.0", "rawtex.a", "rawtex.r * 256.0 + rawtex.g"};
  constexpr const char* low[] = {"rawtex.a", "rawtex.r", "rawtex.b"};
  out += "  float z_high = " + std::string(sample[key.ztex_type]) + ";\n";
  out += "  float z_low = " + std::string(low[key.ztex_type]) + ";\n";
  if (key.ztex_op == 1) {
    out += "  float raster_z = floor(clamp(screen.z * 16777216.0, 0.0, 16777215.0));\n"
           "  float raster_high = floor(raster_z / 256.0);\n"
           "  z_high += raster_high;\n"
           "  z_low += raster_z - raster_high * 256.0;\n";
  }
  // Split the 24-bit sum so Cg float ALU never loses the low bit above 2^24.
  out += "  z_high += ztexture_bias.x;\n"
         "  z_low += ztexture_bias.y;\n"
         "  z_high += floor(z_low / 256.0);\n"
         "  z_low -= floor(z_low / 256.0) * 256.0;\n"
         "  z_high -= floor(z_high / 65536.0) * 65536.0;\n"
         "  fragment_depth = (z_high * 256.0 + z_low) / 16777216.0;\n";
}

} // namespace gx_vita::ztexture
