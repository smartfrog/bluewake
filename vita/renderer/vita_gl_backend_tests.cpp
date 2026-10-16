// SPDX-License-Identifier: GPL-3.0-or-later
#include "ww_renderer_backend.h"
#include "ww_renderer.h"
#include <vitaGL.h>

#include <cstdio>
#include <cstring>
#include <string>

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
  return 1; } } while (0)

namespace {
gx_vita::TranslatedCopy copy_to(std::uint32_t address) {
  gx_vita::TranslatedCopy copy;
  copy.ok = true;
  copy.dest_address = address;
  copy.width = copy.dest_width = 640;
  copy.height = copy.dest_height = 528;
  return copy;
}

int distinct_destinations() {
  auto* backend = ww_renderer_vita_backend();
  CHECK(backend->init() == nullptr);
  for (unsigned i = 0; i < 1000; ++i) {
    CHECK(backend->submit_copy(copy_to(0x1000 + i * 32)) == nullptr);
  }
  std::printf("distinct destinations: live textures=%zu storage bytes=%zu copy calls=%u\n",
              test_gl::textures.size(), test_gl::texture_bytes(), test_gl::copies);
  CHECK(test_gl::textures.size() == 8);
  CHECK(test_gl::texture_bytes() == 0);
  CHECK(test_gl::generated_textures == 8);
  CHECK(test_gl::copies == 0);
  backend->shutdown();
  return 0;
}

int same_destination() {
  auto* backend = ww_renderer_vita_backend();
  CHECK(backend->init() == nullptr);
  for (unsigned i = 0; i < 1000; ++i)
    CHECK(backend->submit_copy(copy_to(0x1000)) == nullptr);
  std::printf("same destination: live textures=%zu storage bytes=%zu\n",
              test_gl::textures.size(), test_gl::texture_bytes());
  CHECK(test_gl::textures.size() == 8);
  CHECK(test_gl::generated_textures == 8);
  CHECK(test_gl::texture_bytes() == 0);
  backend->shutdown();
  return 0;
}

int draw_slots() {
  auto* backend = ww_renderer_vita_backend();
  CHECK(backend->init() == nullptr);
  gx_vita::TranslatedDraw draw;
  gx_vita::CgProgram program;
  draw.texmap_mask = 0xff;
  for (unsigned slot = 0; slot < 8; ++slot) {
    auto& texture = draw.textures[slot];
    texture.valid = true;
    texture.width = texture.height = 1;
    texture.rgba = {std::uint8_t(slot), 0x22, 0x33, 0xff};
  }
  CHECK(backend->submit_draw(draw, program) == nullptr);
  const auto slots = test_gl::textures;
  CHECK(backend->submit_copy(copy_to(0x1000)) == nullptr);
  CHECK(backend->submit_draw(draw, program) == nullptr);
  CHECK(test_gl::generated_textures == 8);
  CHECK(test_gl::draws == 2);
  for (unsigned slot = 0; slot < 8; ++slot) {
    const auto name = test_gl::bound[slot];
    CHECK(test_gl::textures.at(name).upload == draw.textures[slot].rgba);
    CHECK(slots.at(name).upload == draw.textures[slot].rgba);
  }
  backend->shutdown();
  return 0;
}

int copy_preserves_readback() {
  auto* backend = ww_renderer_vita_backend();
  CHECK(backend->init() == nullptr);
  CHECK(backend->present() == nullptr);
  WwRendererFramePixels before, after;
  CHECK(backend->readback(before) == nullptr);
  CHECK(backend->submit_copy(copy_to(0x1000)) == nullptr);
  CHECK(backend->readback(after) == nullptr);
  CHECK(after.width == 960 && after.height == 544);
  CHECK(after.rgba.size() == std::size_t(960) * 544 * 4);
  CHECK(after.rgba == before.rgba);
  CHECK(after.rgba.front() == 0x6b);
  CHECK(test_gl::read_buffer == GL_BACK);
  CHECK(test_gl::swaps == 1);
  backend->shutdown();
  return 0;
}

int copy_reports_error() {
  auto* backend = ww_renderer_vita_backend();
  CHECK(backend->init() == nullptr);
  test_gl::error = GL_INVALID_VALUE;
  const char* error = backend->submit_copy(copy_to(0x1000));
  CHECK(error != nullptr);
  CHECK(std::strcmp(error, "vitaGL efb copy error 0x501") == 0);
  backend->shutdown();
  return 0;
}

int releases_owned_resources() {
  auto* backend = ww_renderer_vita_backend();
  for (unsigned cycle = 0; cycle < 2; ++cycle) {
    CHECK(backend->init() == nullptr);
    CHECK(backend->submit_copy(copy_to(0x1000 + cycle * 32)) == nullptr);
    backend->shutdown();
    CHECK(test_gl::textures.empty());
    CHECK(test_gl::buffers.empty());
    CHECK(test_gl::invalid_deletions == 0);
    CHECK(test_gl::generated_textures == 8 * (cycle + 1));
    CHECK(test_gl::deleted_textures == test_gl::generated_textures);
  }
  return 0;
}
} // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string scenario = argv[1];
  if (scenario == "distinct") return distinct_destinations();
  if (scenario == "same") return same_destination();
  if (scenario == "draw") return draw_slots();
  if (scenario == "readback") return copy_preserves_readback();
  if (scenario == "error") return copy_reports_error();
  if (scenario == "shutdown") return releases_owned_resources();
  return 2;
}
