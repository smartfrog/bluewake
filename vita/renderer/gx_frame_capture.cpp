// SPDX-License-Identifier: GPL-3.0-or-later
#include "gx_frame_capture.h"
#include "../feasibility/gx/png_capture.h"

#include <cstring>
#include <vector>
#include <new>

namespace gx_capture {

const char* write_png(const WwRendererFrame& frame, FILE* output, bool display_opaque) try {
  if (!valid(frame) || output == nullptr)
    return "invalid PNG frame/stream";
  std::vector<std::uint8_t> top_first(frame.size);
  const std::size_t row = std::size_t(frame.width) * 4;
  for (std::uint32_t y = 0; y < frame.height; ++y)
    std::memcpy(top_first.data() + y * row,
                frame.rgba + (frame.height - 1 - y) * row, row);
  if (display_opaque)
    for (std::size_t alpha = 3; alpha < top_first.size(); alpha += 4)
      top_first[alpha] = 255;
  return png_write_rgba8_stream(output, top_first.data(), frame.width, frame.height)
              ? nullptr : "PNG write failed";
} catch (const std::bad_alloc&) {
  return "PNG allocation failed";
}

const char* write_png(const WwRendererFrame& frame, const char* path, bool display_opaque) {
  if (!valid(frame) || path == nullptr || path[0] == '\0')
    return "invalid PNG frame/path";
  FILE* output = fopen(path, "wbx");
  if (output == nullptr)
    return "PNG exclusive open failed";
  const char* error = write_png(frame, output, display_opaque);
  const int closed = fclose(output);
  if (error != nullptr)
    return closed != 0 ? "PNG write and close failed" : error;
  return closed != 0 ? "PNG close failed" : nullptr;
}

}
