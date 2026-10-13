// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal PNG writer for framebuffer captures: 8-bit RGBA, filter 0 rows,
// stored (uncompressed) deflate blocks. No dependencies, so the same code
// writes the capture on the Vita and in host tests.

#ifndef WW02_PNG_CAPTURE_H
#define WW02_PNG_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Write `pixels` (width * height * 4 bytes, tightly packed RGBA8, top row
// first) as a new PNG file. Existing entries, including symlinks, are refused.
bool png_write_rgba8(const char* path, const uint8_t* pixels, uint32_t width,
                     uint32_t height);

// The caller owns the already-open stream, including flushing and closing it.
bool png_write_rgba8_stream(FILE* output, const uint8_t* pixels, uint32_t width,
                            uint32_t height);

#ifdef __cplusplus
}
#endif

#endif
