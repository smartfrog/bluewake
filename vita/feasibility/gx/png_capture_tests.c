// SPDX-License-Identifier: GPL-3.0-or-later
// Host check for the PNG writer: writes a 3x2 pattern PNG for
// test_png_capture.py to parse and verify.

#include "png_capture.h"

#include <stdio.h>

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: png_capture_tests <out.png>\n");
    return 2;
  }
  const unsigned char pixels[3 * 2 * 4] = {
      1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
      13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
  };
  if (!png_write_rgba8(argv[1], pixels, 3, 2))
    return 1;
  printf("png_capture: wrote %s\n", argv[1]);
  return 0;
}
