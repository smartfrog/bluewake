// SPDX-License-Identifier: GPL-3.0-or-later
#include "gx_frame_capture.h"

#include <cstdio>
#include <string>
#include <vector>

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

int main(int argc, char** argv) {
  std::vector<std::uint8_t> pixels;
  for (unsigned value = 13; value <= 24; ++value)
    pixels.push_back(value);
  for (unsigned value = 1; value <= 12; ++value)
    pixels.push_back(value);
  WwRendererFrame frame{3, 2, pixels.data(), pixels.size()};
  if (argc == 2 || argc == 3) {
    CHECK(argc == 2 || std::string(argv[2]) == "--display-opaque");
    CHECK(gx_capture::write_png(frame, argv[1], argc == 3) == nullptr);
    return 0;
  }
  CHECK(argc == 1);
  CHECK(gx_capture::valid(frame));
  CHECK(gx_capture::write_png(frame, "/nonexistent/ww18-capture.png") != nullptr);
  frame.size = 23;
  CHECK(!gx_capture::valid(frame));
  CHECK(gx_capture::write_png(frame, "/nonexistent/ww18-capture.png") != nullptr);
  std::puts("frame PNG orientation and validation checks ok");
  return 0;
}
