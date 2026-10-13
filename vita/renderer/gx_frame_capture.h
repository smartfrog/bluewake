// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ww_renderer.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace gx_capture {

inline bool valid(const WwRendererFrame& frame) {
  return frame.rgba != nullptr && frame.width > 0 && frame.height > 0 &&
         frame.width <= 4096 && frame.height <= 4096 &&
         frame.size == std::size_t(frame.width) * frame.height * 4;
}

const char* write_png(const WwRendererFrame& frame, const char* path, bool display_opaque = false);
const char* write_png(const WwRendererFrame& frame, FILE* output, bool display_opaque = false);

}
