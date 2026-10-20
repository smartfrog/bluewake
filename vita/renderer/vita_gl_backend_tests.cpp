// SPDX-License-Identifier: GPL-3.0-or-later
#include "ww_renderer_backend.h"
#include "ww_renderer.h"
#include <vitaGL.h>

#include <cstdio>
#include <cstring>
#include <string>
#if defined(WW_PROGRAM_CACHE)
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#endif

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

int depth_clear_per_frame() {
  auto* backend = ww_renderer_vita_backend();
  glClearDepthf(0.25f);
  glDepthMask(GL_FALSE);
  CHECK(backend->init() == nullptr);
  CHECK(test_gl::clears.size() == 1);
  gx_vita::TranslatedDraw draw;
  gx_vita::CgProgram program;
  draw.pipeline.depth_write = 0;
  for (unsigned frame = 0; frame < 3; ++frame) {
    CHECK(backend->submit_draw(draw, program) == nullptr);
    CHECK(backend->submit_copy(copy_to(0x1000)) == nullptr);
    CHECK(backend->submit_draw(draw, program) == nullptr);
    CHECK(backend->finish() == nullptr);
    CHECK(test_gl::clears.size() == frame + 1);
    CHECK(backend->present() == nullptr);
    CHECK(test_gl::clears.size() == frame + 2);
    WwRendererFramePixels pixels;
    CHECK(backend->readback(pixels) == nullptr);
    CHECK(pixels.rgba.front() == 0x6b);
    CHECK(test_gl::clears.size() == frame + 2);
  }
  for (unsigned frame = 0; frame < test_gl::clears.size(); ++frame) {
    const auto& clear = test_gl::clears[frame];
    CHECK(clear.mask == GL_DEPTH_BUFFER_BIT);
    CHECK(clear.depth == 1.0f);
    CHECK(clear.depth_write == GL_TRUE);
    CHECK(clear.frame == frame);
    CHECK(clear.draws == frame * 2);
  }
  backend->shutdown();
  return 0;
}

int depth_clear_reports_error() {
  auto* backend = ww_renderer_vita_backend();
  CHECK(backend->init() == nullptr);
  test_gl::next_clear_error = GL_INVALID_VALUE;
  const char* error = backend->present();
  CHECK(error != nullptr);
  CHECK(std::strcmp(error, "vitaGL frame depth clear error 0x501") == 0);
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

#if defined(WW_PROGRAM_CACHE)
std::filesystem::path cache_directory(const char* name) {
  const auto path = std::filesystem::temp_directory_path() /
                    (std::string("bw-backend-cache-") + name + "-" +
                     std::to_string(::getpid()));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  setenv("WW_PROGRAM_CACHE_DIR", path.string().c_str(), 1);
  return path;
}

int draw_cached_program() {
  auto* backend = ww_renderer_vita_backend();
  CHECK(backend->init() == nullptr);
  gx_vita::TranslatedDraw draw;
  gx_vita::CgProgram program;
  program.ok = true;
  program.vertex = "vertex-source-for-cache";
  program.fragment = "fragment-source-for-cache";
  draw.pipeline.program_key = 0x1234;
  CHECK(backend->submit_draw(draw, program) == nullptr);
  backend->shutdown();
  return 0;
}

void reset_gl_counters() {
  test_gl::compiled_shaders = 0;
  test_gl::linked_programs = 0;
  test_gl::binary_saves = 0;
  test_gl::binary_loads = 0;
  test_gl::last_binary_size = 0;
  test_gl::fail_next_binary_link = false;
  test_gl::link_status = 1;
}

std::filesystem::path single_cache_entry(const std::filesystem::path& dir) {
  for (const auto& entry : std::filesystem::directory_iterator(dir))
    if (entry.path().extension() == ".bwsc")
      return entry.path();
  return {};
}

int cache_round_trip() {
  const auto dir = cache_directory("roundtrip");

  reset_gl_counters();
  CHECK(draw_cached_program() == 0);
  CHECK(test_gl::compiled_shaders == 2);
  CHECK(test_gl::linked_programs == 1);
  CHECK(test_gl::binary_saves == 1);
  CHECK(!single_cache_entry(dir).empty());

  reset_gl_counters();
  CHECK(draw_cached_program() == 0);
  CHECK(test_gl::compiled_shaders == 0);
  CHECK(test_gl::linked_programs == 0);
  CHECK(test_gl::binary_loads == 1);
  CHECK(test_gl::last_binary_size == 16);

  const auto entry = single_cache_entry(dir);
  CHECK(!entry.empty());
  std::FILE* file = std::fopen(entry.string().c_str(), "r+b");
  CHECK(file != nullptr);
  CHECK(std::fseek(file, -1, SEEK_END) == 0);
  const int last = std::fgetc(file);
  CHECK(last != EOF);
  CHECK(std::fseek(file, -1, SEEK_END) == 0);
  CHECK(std::fputc(last ^ 0xff, file) != EOF);
  CHECK(std::fclose(file) == 0);

  reset_gl_counters();
  CHECK(draw_cached_program() == 0);
  CHECK(test_gl::compiled_shaders == 2);
  CHECK(test_gl::binary_loads == 0);
  CHECK(test_gl::binary_saves == 1);

  reset_gl_counters();
  test_gl::fail_next_binary_link = true;
  CHECK(draw_cached_program() == 0);
  CHECK(test_gl::binary_loads == 1);
  CHECK(test_gl::compiled_shaders == 2);
  CHECK(test_gl::link_status == 1);

  std::filesystem::remove_all(dir);
  std::printf("cache: miss/compile/store, hit/reuse, corrupt fallback, "
              "link-failure fallback\n");
  return 0;
}
#endif
} // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string scenario = argv[1];
  if (scenario == "distinct") return distinct_destinations();
  if (scenario == "same") return same_destination();
  if (scenario == "draw") return draw_slots();
  if (scenario == "readback") return copy_preserves_readback();
  if (scenario == "error") return copy_reports_error();
  if (scenario == "depth-clear") return depth_clear_per_frame();
  if (scenario == "depth-clear-error") return depth_clear_reports_error();
  if (scenario == "shutdown") return releases_owned_resources();
#if defined(WW_PROGRAM_CACHE)
  if (scenario == "cache") return cache_round_trip();
#endif
  return 2;
}
