// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-17 renderer tests: synthetic GX command batches through the host
// interface (ww_renderer.h), with a recording backend standing in for vitaGL.
//
// The interface is exercised exactly as the host calls it: create over the
// guest memory view, write captured FIFO batches (split mid-command included),
// present on the guest's present events. The fake backend records what vitaGL
// would receive, so a draw, a program, a copy and a presentation are checked
// as events, never inferred from a return code.
//
// Checks are explicit (no assert): a release build must fail loudly too.

#include "ww_renderer.h"
#include "ww_renderer_backend.h"

#include "../feasibility/gx/gx_fixture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
      return 1;                                                              \
    }                                                                        \
  } while (0)

namespace {

struct DrawRecord {
  std::uint32_t vertex_count = 0;
  std::size_t index_count = 0;
  std::uint16_t topology = 0;
  std::uint64_t program_key = 0;
  bool has_texture = false;
  std::uint32_t texture_width = 0;
  std::uint32_t texture_height = 0;
  std::uint32_t texmap_mask = 0;
  std::uint8_t depth_test = 0;
  std::uint16_t depth_func = 0;
  std::uint8_t depth_write = 0;
  std::uint8_t blend_enable = 0;
  std::uint16_t blend_src = 0;
  std::uint16_t blend_dst = 0;
  std::uint8_t cull_enable = 0;
  std::uint16_t cull_face = 0;
  std::uint8_t color_write = 0;
  std::uint8_t alpha_write = 0;
  float position_matrix[16] = {};
  float projection[16] = {};
  std::string cg_vertex;
  std::string cg_fragment;
};

struct CopyRecord {
  std::uint32_t dest_address = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t format = 0;
};

class RecordingBackend : public WwRendererBackend {
public:
  const char* init() override {
    ++inits;
    return init_error;
  }

  const char* submit_draw(const gx_vita::TranslatedDraw& draw,
                          const gx_vita::CgProgram& program) override {
    if (draw_error != nullptr)
      return draw_error;
    DrawRecord record;
    record.vertex_count = draw.geometry.vertex_count;
    record.index_count = draw.geometry.indices.size();
    record.topology = draw.pipeline.topology;
    record.program_key = draw.pipeline.program_key;
    record.has_texture = draw.texture.valid;
    record.texture_width = draw.texture.width;
    record.texture_height = draw.texture.height;
    record.texmap_mask = draw.texmap_mask;
    record.depth_test = draw.pipeline.depth_test;
    record.depth_func = draw.pipeline.depth_func;
    record.depth_write = draw.pipeline.depth_write;
    record.blend_enable = draw.pipeline.blend_enable;
    record.blend_src = draw.pipeline.blend_src;
    record.blend_dst = draw.pipeline.blend_dst;
    record.cull_enable = draw.pipeline.cull_enable;
    record.cull_face = draw.pipeline.cull_face;
    record.color_write = draw.pipeline.color_write;
    record.alpha_write = draw.pipeline.alpha_write;
    std::memcpy(record.position_matrix, draw.position_matrix,
                sizeof record.position_matrix);
    std::memcpy(record.projection, draw.projection, sizeof record.projection);
    record.cg_vertex = program.vertex;
    record.cg_fragment = program.fragment;
    draws.push_back(std::move(record));
    return nullptr;
  }

  const char* submit_copy(const gx_vita::TranslatedCopy& copy) override {
    if (copy_error != nullptr)
      return copy_error;
    CopyRecord record;
    record.dest_address = copy.dest_address;
    record.width = copy.width;
    record.height = copy.height;
    record.format = copy.format;
    copies.push_back(record);
    return nullptr;
  }

  const char* present() override {
    if (present_error != nullptr)
      return present_error;
    ++presents;
    return nullptr;
  }

  const char* finish() override {
    ++finishes;
    draws_at_finish = draws.size();
    return finish_supported ? finish_error : WwRendererBackend::finish();
  }

  void shutdown() override { ++shutdowns; }

  int inits = 0;
  int presents = 0;
  int shutdowns = 0;
  int finishes = 0;
  std::size_t draws_at_finish = 0;
  bool finish_supported = true;
  const char* init_error = nullptr;
  const char* draw_error = nullptr;
  const char* copy_error = nullptr;
  const char* present_error = nullptr;
  const char* finish_error = nullptr;
  std::vector<DrawRecord> draws;
  std::vector<CopyRecord> copies;
};

// The host's memory view, as the tests provide it: the fixture's MEM1 behind a
// zeroed CPUState (the same pair the host hands the pinned resolver).
struct Fixture {
  Fixture() {
    gx_fixture::fill_texture_zones(memory);
    cpu.ram = memory.mem1.data();
    cpu.ram_size = static_cast<u32>(memory.mem1.size());
  }

  WwRendererConfig config() {
    WwRendererConfig config{};
    config.memory = &guest;
    config.cpu = &cpu;
    return config;
  }

  gx_fixture::FixtureContext memory;
  DolGuestMemory guest{};
  CPUState cpu{};
};

// A stream that ends the frame: one textured triangle then the game's display
// copy (BP 0x52 bit 14, GXCopyDisp).
std::vector<std::uint8_t> frame_with_display_copy() {
  std::vector<std::uint8_t> fifo = gx_fixture::build_textured_triangle();
  gx_fixture::push_bp(fifo, 0x52u, 1u << 14u);
  return fifo;
}

int finish_follows_submitted_draw_without_presenting() {
  Fixture fixture;
  RecordingBackend backend;
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  const auto fifo = gx_fixture::build_textured_triangle();
  CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
  CHECK(ww_renderer_finish(renderer) == nullptr);
  CHECK(backend.finishes == 1 && backend.draws_at_finish == 1u && backend.presents == 0);
  WwRendererStats stats{};
  ww_renderer_stats(renderer, &stats);
  CHECK(stats.draws == 1u && stats.presented == 0u);
  ww_renderer_destroy(renderer);
  return 0;
}

int finish_failure_is_latched() {
  Fixture fixture;
  RecordingBackend backend;
  backend.finish_error = "finish boom";
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  CHECK(std::strstr(ww_renderer_finish(renderer), "finish boom") != nullptr);
  CHECK(std::strstr(ww_renderer_present(renderer, nullptr), "finish boom") != nullptr);
  CHECK(backend.presents == 0);
  ww_renderer_destroy(renderer);
  return 0;
}

int backend_without_finish_cannot_confirm_completion() {
  Fixture fixture;
  RecordingBackend backend;
  backend.finish_supported = false;
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  CHECK(std::strstr(ww_renderer_finish(renderer), "confirmed GPU finish") != nullptr);
  CHECK(backend.presents == 0);
  ww_renderer_destroy(renderer);
  return 0;
}

int partial_fifo_cannot_confirm_completion() {
  Fixture fixture;
  RecordingBackend backend;
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  const std::uint8_t byte = 0x08u;
  CHECK(ww_renderer_write(renderer, &byte, 1u) == nullptr);
  CHECK(std::strstr(ww_renderer_finish(renderer), "complete captured commands") != nullptr);
  CHECK(backend.finishes == 0 && backend.presents == 0);
  ww_renderer_destroy(renderer);
  return 0;
}

int finish_cannot_present_a_held_frame() {
  Fixture fixture;
  RecordingBackend backend;
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  const auto fifo = frame_with_display_copy();
  CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
  CHECK(std::strstr(ww_renderer_finish(renderer), "unpresented display copy") != nullptr);
  CHECK(backend.finishes == 0 && backend.presents == 0);
  ww_renderer_destroy(renderer);
  return 0;
}

int refused_program_cannot_confirm_completion() {
  Fixture fixture;
  RecordingBackend backend;
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  const auto fifo = gx_fixture::build_indirect_triangle();
  CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
  CHECK(ww_renderer_finish(renderer) != nullptr);
  CHECK(backend.finishes == 0 && backend.presents == 0);
  ww_renderer_destroy(renderer);
  return 0;
}

} // namespace

int main() {
  CHECK(finish_follows_submitted_draw_without_presenting() == 0);
  CHECK(finish_failure_is_latched() == 0);
  CHECK(backend_without_finish_cannot_confirm_completion() == 0);
  CHECK(partial_fifo_cannot_confirm_completion() == 0);
  CHECK(finish_cannot_present_a_held_frame() == 0);
  CHECK(refused_program_cannot_confirm_completion() == 0);
  std::printf("renderer GPU finish contracts ok\n");
  {
    // Create refuses a missing memory view and a missing backend loudly.
    WwRenderer* renderer = nullptr;
    CHECK(std::strcmp(ww_renderer_create(&renderer, nullptr, nullptr),
                      "config is null") == 0);
    CHECK(renderer == nullptr);
    WwRendererConfig empty{};
    CHECK(ww_renderer_create(&renderer, &empty, nullptr) != nullptr);
    CHECK(renderer == nullptr);
    std::printf("renderer create refuses missing input ok\n");
  }

  {
    // A backend whose init fails fails the create, and the renderer is gone.
    Fixture fixture;
    RecordingBackend backend;
    backend.init_error = "gl context";
    WwRenderer* renderer = nullptr;
    const char* error =
        ww_renderer_create(&renderer, nullptr, nullptr); // config first
    CHECK(error != nullptr && renderer == nullptr);
    WwRendererConfig config = fixture.config();
    error = ww_renderer_create(&renderer, &config, &backend);
    CHECK(error != nullptr && renderer == nullptr);
    CHECK(std::strstr(error, "gl context") != nullptr);
    CHECK(backend.inits == 1);
    CHECK(backend.shutdowns == 0); // create failed: nothing to shut down
    std::printf("renderer backend init refusal ok: %s\n", error);
  }

  {
    // An empty batch is a valid no-op, and a present with no frame shows
    // nothing.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    CHECK(renderer != nullptr);
    CHECK(backend.inits == 1);
    CHECK(ww_renderer_write(renderer, nullptr, 0u) == nullptr);
    bool presented = true;
    CHECK(ww_renderer_present(renderer, &presented) == nullptr);
    CHECK(!presented);
    CHECK(backend.presents == 0);
    WwRendererStats stats{};
    ww_renderer_stats(renderer, &stats);
    CHECK(stats.fifo_bytes == 0u && stats.events == 0u && stats.draws == 0u);
    ww_renderer_destroy(renderer);
    CHECK(backend.shutdowns == 1);
    std::printf("renderer empty write and present ok\n");
  }

  {
    // One textured triangle, split mid-command across two batches: nothing
    // decodes from the partial command; the rest completes it into one draw
    // with the fixture's own facts, the generated Cg program, and the plan's
    // matrices.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const std::vector<std::uint8_t> fifo = gx_fixture::build_textured_triangle();
    const std::size_t split = fifo.size() / 2u;
    CHECK(ww_renderer_write(renderer, fifo.data(), split) == nullptr);
    CHECK(backend.draws.empty());
    CHECK(ww_renderer_write(renderer, fifo.data() + split,
                            fifo.size() - split) == nullptr);
    CHECK(backend.draws.size() == 1u);
    const DrawRecord& draw = backend.draws[0];
    CHECK(draw.vertex_count == 3u);
    CHECK(draw.index_count == 3u);
    CHECK(draw.topology == gx_vita::kGlTriangles);
    CHECK(draw.program_key != 0u);
    CHECK(draw.has_texture);
    CHECK(draw.texture_width == gx_fixture::kTextureWidth);
    CHECK(draw.texture_height == gx_fixture::kTextureHeight);
    CHECK(!draw.cg_vertex.empty() && !draw.cg_fragment.empty());
    // The fixture's identity position matrix, padded to 4 rows.
    CHECK(draw.position_matrix[0] == 1.0f && draw.position_matrix[5] == 1.0f &&
          draw.position_matrix[10] == 1.0f &&
          draw.position_matrix[15] == 1.0f &&
          draw.position_matrix[12] == 0.0f &&
          draw.position_matrix[13] == 0.0f &&
          draw.position_matrix[14] == 0.0f);
    // The fixture's perspective projection words {1, 1, 0.1, 1000, 0, 0}.
    CHECK(draw.projection[0] == 1.0f && draw.projection[2] == 1.0f);
    CHECK(draw.projection[5] == 0.1f && draw.projection[6] == 1000.0f);
    CHECK(draw.projection[10] == 0.0f && draw.projection[11] == 0.0f);
    CHECK(draw.projection[14] == -1.0f);
    WwRendererStats stats{};
    ww_renderer_stats(renderer, &stats);
    CHECK(stats.fifo_bytes == fifo.size());
    CHECK(stats.plans == 1u && stats.plans_skipped == 0u);
    CHECK(stats.draws == 1u && stats.programs == 1u);
    CHECK(stats.programs_distinct == 1u);
    CHECK(stats.display_copies == 0u && stats.presented == 0u);
    ww_renderer_destroy(renderer);
    std::printf("renderer split batch draw ok vertices=%u\n",
                draw.vertex_count);
  }

  {
    // A display copy ends the frame: the draws before it are submitted, the
    // present shows the frame once, and a second present shows nothing.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const std::vector<std::uint8_t> fifo = frame_with_display_copy();
    CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
    CHECK(backend.draws.size() == 1u);
    CHECK(backend.presents == 0);
    WwRendererStats stats{};
    ww_renderer_stats(renderer, &stats);
    CHECK(stats.display_copies == 1u);
    bool presented = false;
    CHECK(ww_renderer_present(renderer, &presented) == nullptr);
    CHECK(presented);
    CHECK(backend.presents == 1);
    CHECK(ww_renderer_present(renderer, &presented) == nullptr);
    CHECK(!presented);
    CHECK(backend.presents == 1);
    ww_renderer_stats(renderer, &stats);
    CHECK(stats.presented == 1u && stats.display_copies == 1u);
    ww_renderer_destroy(renderer);
    std::printf("renderer display copy presents once ok\n");
  }

  {
    // The frame after a display copy waits: its draws are not submitted into
    // the buffer being shown; the present swaps first, then resumes them.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    std::vector<std::uint8_t> fifo = frame_with_display_copy();
    const std::vector<std::uint8_t> next = gx_fixture::build_state_triangle();
    fifo.insert(fifo.end(), next.begin(), next.end());
    CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
    CHECK(backend.draws.size() == 1u); // only the frame being shown
    bool presented = false;
    CHECK(ww_renderer_present(renderer, &presented) == nullptr);
    CHECK(presented);
    CHECK(backend.draws.size() == 2u); // the held next frame resumed
    CHECK(backend.draws[1].depth_test == 1u);
    ww_renderer_destroy(renderer);
    std::printf("renderer holds next frame until present ok\n");
  }

  {
    // Pipeline state reaches the backend: the fixture's depth (LEqual,
    // update), SrcAlpha/InvSrcAlpha blend, cull and write masks.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const std::vector<std::uint8_t> fifo = gx_fixture::build_state_triangle();
    CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
    CHECK(backend.draws.size() == 1u);
    const DrawRecord& draw = backend.draws[0];
    CHECK(draw.depth_test == 1u && draw.depth_func == gx_vita::kGlLequal &&
          draw.depth_write == 1u);
    CHECK(draw.blend_enable == 1u && draw.blend_src == gx_vita::kGlSrcAlpha &&
          draw.blend_dst == gx_vita::kGlOneMinusSrcAlpha);
    CHECK(draw.cull_enable == 1u && draw.cull_face == gx_vita::kGlBack);
    CHECK(draw.color_write == 1u && draw.alpha_write == 1u);
    ww_renderer_destroy(renderer);
    std::printf("renderer pipeline state ok\n");
  }

  {
    // A multi-texmap draw (the cel fixture's two images) reaches the backend
    // with both samplers marked.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const std::vector<std::uint8_t> fifo = gx_fixture::build_cel_triangle();
    CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
    CHECK(backend.draws.size() == 1u);
    CHECK(backend.draws[0].texmap_mask == 0x3u);
    ww_renderer_destroy(renderer);
    std::printf("renderer multi-texmap draw ok\n");
  }

  {
    // A refused Cg program is counted and kept with its reason; no draw is
    // submitted for it.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const std::vector<std::uint8_t> fifo = gx_fixture::build_indirect_triangle();
    CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
    CHECK(backend.draws.empty());
    WwRendererStats stats{};
    ww_renderer_stats(renderer, &stats);
    CHECK(stats.plans == 1u && stats.programs_refused == 1u);
    CHECK(stats.refusal_count == 1u);
    CHECK(std::strcmp(stats.refusal_reasons[0],
                      "indirect TEV lookups not supported") == 0);
    ww_renderer_destroy(renderer);
    std::printf("renderer program refusal kept ok\n");
  }

  {
    // An EFB copy-to-texture is executed through the backend at its stream
    // position, after the draw before it.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const std::vector<std::uint8_t> fifo = gx_fixture::build_copy_triangle();
    CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
    CHECK(backend.draws.size() == 1u);
    CHECK(backend.copies.size() == 1u);
    CHECK(backend.copies[0].dest_address == gx_fixture::kCopyBase);
    CHECK(backend.copies[0].width == gx_fixture::kTextureWidth &&
          backend.copies[0].height == gx_fixture::kTextureHeight);
    WwRendererStats stats{};
    ww_renderer_stats(renderer, &stats);
    CHECK(stats.copies == 1u && stats.copies_refused == 0u);
    CHECK(stats.display_copies == 0u);
    ww_renderer_destroy(renderer);
    std::printf("renderer efb copy ok dest=%08X\n",
                backend.copies[0].dest_address);
  }

  {
    // A stream the pinned frontend rejects stops the host with the refusal,
    // naming its opcode, and nothing is counted as drawn.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const std::uint8_t bad[] = {0x80u, 0x00u, 0x01u};
    const char* error = ww_renderer_write(renderer, bad, sizeof bad);
    CHECK(error != nullptr);
    CHECK(std::strstr(error, "opcode=0x80") != nullptr);
    CHECK(backend.draws.empty());
    WwRendererStats stats{};
    ww_renderer_stats(renderer, &stats);
    CHECK(stats.draws == 0u && stats.plans == 0u);
    std::printf("renderer stream rejection ok: %s\n", error);
    ww_renderer_destroy(renderer);
  }

  {
    // A backend submit failure stops the host with the backend's reason.
    Fixture fixture;
    RecordingBackend backend;
    backend.draw_error = "draw boom";
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const std::vector<std::uint8_t> fifo = gx_fixture::build_textured_triangle();
    const char* error = ww_renderer_write(renderer, fifo.data(), fifo.size());
    CHECK(error != nullptr);
    CHECK(std::strstr(error, "draw boom") != nullptr);
    WwRendererStats stats{};
    ww_renderer_stats(renderer, &stats);
    CHECK(stats.draws == 0u); // a failed submit is not a submitted draw
    std::printf("renderer backend draw failure ok: %s\n", error);
    ww_renderer_destroy(renderer);
  }

  {
    // The gaps the pinned chain flags are kept for the host's report.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const std::vector<std::uint8_t> fifo = gx_fixture::build_textured_triangle();
    CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
    WwRendererStats stats{};
    ww_renderer_stats(renderer, &stats);
    bool found = false;
    for (std::uint32_t i = 0; i < stats.gap_count; ++i)
      found = found || std::strcmp(stats.gaps[i].name, "unresolved_tex_matrix") == 0;
    CHECK(found);
    ww_renderer_destroy(renderer);
    std::printf("renderer gaps kept ok count=%u\n", stats.gap_count);
  }

  {
    // Two frames, each ending in a display copy: two draws, two presents, in
    // order, with the program reused for equal shader keys.
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const std::vector<std::uint8_t> frame = frame_with_display_copy();
    CHECK(ww_renderer_write(renderer, frame.data(), frame.size()) == nullptr);
    bool presented = false;
    CHECK(ww_renderer_present(renderer, &presented) == nullptr && presented);
    CHECK(ww_renderer_write(renderer, frame.data(), frame.size()) == nullptr);
    CHECK(ww_renderer_present(renderer, &presented) == nullptr && presented);
    CHECK(backend.draws.size() == 2u);
    CHECK(backend.presents == 2);
    WwRendererStats stats{};
    ww_renderer_stats(renderer, &stats);
    CHECK(stats.draws == 2u && stats.programs == 2u &&
          stats.programs_distinct == 1u);
    CHECK(stats.display_copies == 2u && stats.presented == 2u);
    ww_renderer_destroy(renderer);
    std::printf("renderer two frames ok presents=2\n");
  }

  std::printf("ww_renderer: all checks ok\n");
  return 0;
}
