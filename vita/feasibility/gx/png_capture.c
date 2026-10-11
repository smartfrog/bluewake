// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal PNG writer. See png_capture.h.

#include "png_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];
static bool crc_ready = false;

static void crc_init(void) {
  for (uint32_t n = 0; n < 256u; ++n) {
    uint32_t c = n;
    for (int k = 0; k < 8; ++k)
      c = (c & 1u) != 0u ? 0xEDB88320u ^ (c >> 1u) : c >> 1u;
    crc_table[n] = c;
  }
  crc_ready = true;
}

static uint32_t crc_update(uint32_t state, const uint8_t* data, size_t size) {
  if (!crc_ready)
    crc_init();
  for (size_t i = 0; i < size; ++i)
    state = crc_table[(state ^ data[i]) & 0xFFu] ^ (state >> 8u);
  return state;
}

static uint32_t adler32_bytes(const uint8_t* data, size_t size) {
  uint32_t a = 1u, b = 0u;
  for (size_t i = 0; i < size; ++i) {
    a = (a + data[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16u) | a;
}

static bool write_u32be(FILE* f, uint32_t value) {
  const uint8_t bytes[4] = {(uint8_t)(value >> 24u), (uint8_t)(value >> 16u),
                            (uint8_t)(value >> 8u), (uint8_t)value};
  return fwrite(bytes, 1, 4, f) == 4;
}

static bool write_chunk(FILE* f, const char type[4], const uint8_t* data,
                        size_t size) {
  if (!write_u32be(f, (uint32_t)size))
    return false;
  if (fwrite(type, 1, 4, f) != 4)
    return false;
  if (size != 0u && fwrite(data, 1, size, f) != size)
    return false;
  uint32_t state = crc_update(0xFFFFFFFFu, (const uint8_t*)type, 4u);
  state = crc_update(state, data, size);
  return write_u32be(f, state ^ 0xFFFFFFFFu);
}

// zlib stream with stored deflate blocks: 2-byte header, per block a 5-byte
// header (final flag, LEN, ~LEN little-endian) and the raw bytes, then the
// Adler-32 of the raw bytes (big-endian).
static uint8_t* build_zlib_stored(const uint8_t* raw, size_t raw_size,
                                  size_t* out_size) {
  size_t blocks = raw_size / 65535u + 1u;
  size_t size = 2u + blocks * 5u + raw_size + 4u;
  uint8_t* out = (uint8_t*)malloc(size);
  if (out == nullptr)
    return nullptr;
  size_t pos = 0u;
  out[pos++] = 0x78u;
  out[pos++] = 0x01u;
  size_t remaining = raw_size;
  for (;;) {
    const size_t block = remaining > 65535u ? 65535u : remaining;
    const bool final = block == remaining;
    out[pos++] = final ? 1u : 0u;
    out[pos++] = (uint8_t)block;
    out[pos++] = (uint8_t)(block >> 8u);
    const uint16_t nlen = (uint16_t)(~(uint16_t)block);
    out[pos++] = (uint8_t)nlen;
    out[pos++] = (uint8_t)(nlen >> 8u);
    memcpy(out + pos, raw + (raw_size - remaining), block);
    pos += block;
    remaining -= block;
    if (final)
      break;
  }
  const uint32_t adler = adler32_bytes(raw, raw_size);
  out[pos++] = (uint8_t)(adler >> 24u);
  out[pos++] = (uint8_t)(adler >> 16u);
  out[pos++] = (uint8_t)(adler >> 8u);
  out[pos++] = (uint8_t)adler;
  *out_size = pos;
  return out;
}

bool png_write_rgba8(const char* path, const uint8_t* pixels, uint32_t width,
                     uint32_t height) {
  if (path == nullptr || pixels == nullptr || width == 0u || height == 0u)
    return false;

  const size_t row_bytes = (size_t)width * 4u;
  const size_t raw_size = (row_bytes + 1u) * height;
  uint8_t* raw = (uint8_t*)malloc(raw_size);
  if (raw == nullptr)
    return false;
  for (uint32_t y = 0; y < height; ++y) {
    uint8_t* row = raw + (size_t)y * (row_bytes + 1u);
    row[0] = 0u; // filter None
    memcpy(row + 1, pixels + (size_t)y * row_bytes, row_bytes);
  }
  size_t stream_size = 0u;
  uint8_t* stream = build_zlib_stored(raw, raw_size, &stream_size);
  free(raw);
  if (stream == nullptr)
    return false;

  FILE* f = fopen(path, "wb");
  if (f == nullptr) {
    free(stream);
    return false;
  }
  static const uint8_t signature[8] = {0x89u, 'P', 'N', 'G', 0x0Du,
                                       0x0Au, 0x1Au, 0x0Au};
  bool ok = fwrite(signature, 1, sizeof signature, f) == sizeof signature;
  uint8_t ihdr[13];
  ihdr[0] = (uint8_t)(width >> 24u);
  ihdr[1] = (uint8_t)(width >> 16u);
  ihdr[2] = (uint8_t)(width >> 8u);
  ihdr[3] = (uint8_t)width;
  ihdr[4] = (uint8_t)(height >> 24u);
  ihdr[5] = (uint8_t)(height >> 16u);
  ihdr[6] = (uint8_t)(height >> 8u);
  ihdr[7] = (uint8_t)height;
  ihdr[8] = 8u;  // bit depth
  ihdr[9] = 6u;  // colour type RGBA
  ihdr[10] = 0u; // deflate
  ihdr[11] = 0u; // filter method
  ihdr[12] = 0u; // no interlace
  ok = ok && write_chunk(f, "IHDR", ihdr, sizeof ihdr);
  ok = ok && write_chunk(f, "IDAT", stream, stream_size);
  ok = ok && write_chunk(f, "IEND", nullptr, 0u);
  free(stream);
  if (fclose(f) != 0)
    ok = false;
  return ok;
}
