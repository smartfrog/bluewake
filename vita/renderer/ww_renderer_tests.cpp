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
#include "gx_ztexture_fixture.h"

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
  gx_vita::Geometry geometry;
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
  float tev_colors[4][4]{};
  float konst_colors[4][4]{};
  float alpha_ref[2]{};
  float ztexture_bias[2]{};
  float depth_near = 0;
  float depth_far = 1;
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
    record.geometry = draw.geometry;
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
    record.depth_near = draw.pipeline.depth_near;
    record.depth_far = draw.pipeline.depth_far;
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
    std::memcpy(record.tev_colors, draw.tev_colors, sizeof record.tev_colors);
    std::memcpy(record.konst_colors, draw.konst_colors, sizeof record.konst_colors);
    std::memcpy(record.alpha_ref, draw.alpha_ref, sizeof record.alpha_ref);
    std::memcpy(record.ztexture_bias, draw.ztexture_bias, sizeof record.ztexture_bias);
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

  void shutdown() override { ++shutdowns; }

  int inits = 0;
  int presents = 0;
  int shutdowns = 0;
  const char* init_error = nullptr;
  const char* draw_error = nullptr;
  const char* copy_error = nullptr;
  const char* present_error = nullptr;
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

float vertex_float(const DrawRecord& draw, std::uint32_t vertex,
                   std::uint32_t offset) {
  float value;
  std::memcpy(&value, draw.geometry.vertices.data() +
                         vertex * gx_fixture::gxc::kVertexStrideBytes + offset,
              sizeof value);
  return value;
}

int primitive_coverage() {
  struct Case {
    std::uint8_t opcode;
    std::uint16_t count;
    std::uint16_t topology;
    std::vector<std::uint16_t> indices;
  };
  const Case cases[] = {
      {0x80, 4, gx_vita::kGlTriangles, {0, 1, 2, 2, 3, 0}},
      {0x90, 3, gx_vita::kGlTriangles, {0, 1, 2}},
      {0x98, 5, gx_vita::kGlTriangles, {0, 1, 2, 2, 1, 3, 2, 3, 4}},
      {0xA0, 5, gx_vita::kGlTriangles, {0, 1, 2, 0, 2, 3, 0, 3, 4}},
      {0xA8, 4, gx_vita::kGlLines, {0, 1, 2, 3}},
      {0xB0, 4, gx_vita::kGlLines, {0, 1, 1, 2, 2, 3}},
      {0xB8, 2, gx_vita::kGlPoints, {0, 1}},
  };
  for (const Case& test : cases) {
    for (std::uint8_t vat = 0; vat < 8; ++vat) {
      Fixture fixture;
      RecordingBackend backend;
      WwRenderer* renderer = nullptr;
      WwRendererConfig config = fixture.config();
      CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
      std::vector<std::uint8_t> fifo;
      gx_fixture::push_common_state(fifo);
      gx_fixture::push_cp(fifo, 0x60, 0);
      gx_fixture::push_cp(fifo, 0x70 + vat, 1u | (4u << 1));
      gx_fixture::push_cp(fifo, 0x80 + vat, 0);
      gx_fixture::push_cp(fifo, 0x90 + vat, 0);
      std::vector<float> vertices;
      for (std::uint16_t v = 0; v < test.count; ++v)
        vertices.insert(vertices.end(), {float(v), float(v + 1), -float(v)});
      gx_fixture::push_draw(fifo, test.opcode | vat, test.count, vertices);
      for (std::uint8_t byte : fifo)
        CHECK(ww_renderer_write(renderer, &byte, 1) == nullptr);
      CHECK(backend.draws.size() == 1);
      const DrawRecord& draw = backend.draws[0];
      CHECK(draw.geometry.indices == test.indices);
      CHECK(draw.topology == test.topology);
      CHECK(draw.vertex_count == test.count);
      CHECK(vertex_float(draw, test.count - 1, 0) == float(test.count - 1));
      WwRendererStats stats{};
      ww_renderer_stats(renderer, &stats);
      CHECK(stats.plans_skipped == 0 && stats.programs_refused == 0);
      ww_renderer_destroy(renderer);
    }
  }
  std::printf("renderer primitive coverage ok: 7 families x 8 VAT, byte-split\n");
  return 0;
}

int indexed_vertex_coverage() {
  for (std::uint8_t index_type : {2, 3}) {
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    std::vector<std::uint8_t> array;
    for (float value : {2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f})
      gx_fixture::push_f32(array, value);
    std::memcpy(fixture.memory.mem1.data() + 0x2000, array.data(), array.size());
    std::vector<std::uint8_t> fifo;
    gx_fixture::push_common_state(fifo);
    gx_fixture::push_cp(fifo, 0x50, index_type << 9);
    gx_fixture::push_cp(fifo, 0x60, 0);
    gx_fixture::push_cp(fifo, 0xA0, 0x2000);
    gx_fixture::push_cp(fifo, 0xB0, 12);
    gx_fixture::push_u8(fifo, 0x90);
    gx_fixture::push_u16(fifo, 3);
    for (std::uint16_t index : {2, 0, 1}) {
      if (index_type == 2)
        gx_fixture::push_u8(fifo, index);
      else
        gx_fixture::push_u16(fifo, index);
    }
    const char* error = ww_renderer_write(renderer, fifo.data(), fifo.size());
    if (error != nullptr)
      std::fprintf(stderr, "indexed position: %s\n", error);
    CHECK(error == nullptr);
    CHECK(backend.draws.size() == 1);
    CHECK(vertex_float(backend.draws[0], 0, 0) == 8.f);
    CHECK(vertex_float(backend.draws[0], 1, 4) == 3.f);
    CHECK(vertex_float(backend.draws[0], 2, 8) == 7.f);
    ww_renderer_destroy(renderer);
  }
  std::printf("renderer indexed position supply ok: index8/index16\n");
  return 0;
}

int indexed_xf_coverage() {
  for (std::uint8_t opcode : {0x20, 0x28, 0x30, 0x38}) {
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    std::vector<std::uint8_t> matrix;
    for (float value : {1.f, 0.f, 0.f, 4.f, 0.f, 1.f, 0.f, 5.f,
                        0.f, 0.f, 1.f, 6.f})
      gx_fixture::push_f32(matrix, value);
    std::memcpy(fixture.memory.mem1.data() + 0x2000, matrix.data(), matrix.size());
    std::vector<std::uint8_t> fifo;
    gx_fixture::push_common_state(fifo);
    const std::uint8_t array = 12 + (opcode - 0x20) / 8;
    gx_fixture::push_cp(fifo, 0xA0 + array, 0x2000);
    gx_fixture::push_cp(fifo, 0xB0 + array, 48);
    gx_fixture::push_u8(fifo, opcode);
    gx_fixture::push_u32(fifo, 11u << 12);
    gx_fixture::push_triangle(fifo);
    CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
    CHECK(backend.draws.size() == 1);
    CHECK(backend.draws[0].position_matrix[3] == 4.f);
    CHECK(backend.draws[0].position_matrix[7] == 5.f);
    CHECK(backend.draws[0].position_matrix[11] == 6.f);
    ww_renderer_destroy(renderer);
  }
  std::printf("renderer indexed XF supply ok: all four arrays\n");
  return 0;
}

int display_list_coverage() {
  Fixture fixture;
  RecordingBackend backend;
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  auto list = gx_fixture::build_hud_quad();
  list.resize((list.size() + 31u) & ~31u, 0);
  std::memcpy(fixture.memory.mem1.data() + 0x2000, list.data(), list.size());
  std::vector<std::uint8_t> fifo = {0, 0x48, 0x40};
  gx_fixture::push_u32(fifo, 0x80002000);
  gx_fixture::push_u32(fifo, list.size());
  CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
  CHECK(backend.draws.size() == 1);
  CHECK(backend.draws[0].geometry.indices ==
        std::vector<std::uint16_t>({0, 1, 2, 2, 3, 0}));
  ww_renderer_destroy(renderer);
  std::printf("renderer display list with padding/invalidate ok\n");
  return 0;
}

int vertex_attribute_coverage() {
  const std::vector<std::uint8_t> normals = {0xC0, 0, 0x40, 0, 0x40, 0, 0x40, 0, 0xC0};
  const std::vector<std::uint8_t> colors = {255, 128, 64, 32, 0, 255, 0, 255,
                                           64, 0, 255, 128};
  std::vector<std::uint8_t> texcoords;
  for (std::int16_t value : {-8, 12, 4, -4, 16, 20})
    gx_fixture::push_u16(texcoords, static_cast<std::uint16_t>(value));
  for (std::uint8_t mode : {1, 2, 3}) {
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    std::memcpy(fixture.memory.mem1.data() + 0x2100, normals.data(), normals.size());
    std::memcpy(fixture.memory.mem1.data() + 0x2200, colors.data(), colors.size());
    std::memcpy(fixture.memory.mem1.data() + 0x2300, texcoords.data(), texcoords.size());
    std::vector<std::uint8_t> fifo;
    gx_fixture::push_common_state(fifo);
    gx_fixture::push_cp(fifo, 0x50, (1u << 9) | (mode << 11) | (mode << 13));
    gx_fixture::push_cp(fifo, 0x60, mode);
    gx_fixture::push_cp(fifo, 0x70, 1u | (4u << 1) | (1u << 10) | (5u << 14) |
                                  (1u << 21) | (3u << 22) | (2u << 25));
    gx_fixture::push_cp(fifo, 0xA1, 0x2100);
    gx_fixture::push_cp(fifo, 0xB1, 3);
    gx_fixture::push_cp(fifo, 0xA2, 0x2200);
    gx_fixture::push_cp(fifo, 0xB2, 4);
    gx_fixture::push_cp(fifo, 0xA4, 0x2300);
    gx_fixture::push_cp(fifo, 0xB4, 4);
    gx_fixture::push_u8(fifo, 0x90);
    gx_fixture::push_u16(fifo, 3);
    auto attribute = [&](const std::vector<std::uint8_t>& bytes, int stride,
                         std::uint16_t index) {
      if (mode == 1)
        fifo.insert(fifo.end(), bytes.begin() + index * stride,
                    bytes.begin() + (index + 1) * stride);
      else if (mode == 2)
        gx_fixture::push_u8(fifo, index);
      else
        gx_fixture::push_u16(fifo, index);
    };
    for (std::uint16_t index : {2, 0, 1}) {
      for (float value : {1.f, 2.f, 3.f})
        gx_fixture::push_f32(fifo, value);
      attribute(normals, 3, index);
      attribute(colors, 4, index);
      attribute(texcoords, 4, index);
    }
    CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
    CHECK(backend.draws.size() == 1);
    const auto& draw = backend.draws[0];
    CHECK(vertex_float(draw, 0, gx_fixture::gxc::kVertexNormalOffset) == 1.f);
    CHECK(vertex_float(draw, 0, gx_fixture::gxc::kVertexNormalOffset + 8) == -1.f);
    CHECK(vertex_float(draw, 0, gx_fixture::gxc::kVertexColor0Offset) == 64.f / 255.f);
    CHECK(vertex_float(draw, 0, gx_fixture::gxc::kVertexColor0Offset + 12) == 128.f / 255.f);
    CHECK(vertex_float(draw, 0, gx_fixture::gxc::kVertexUvOffset) == 4.f);
    CHECK(vertex_float(draw, 1, gx_fixture::gxc::kVertexUvOffset) == -2.f);
    ww_renderer_destroy(renderer);
  }
  std::printf("renderer direct/index8/index16 S8 normal, RGBA8 and S16 texcoords ok\n");
  return 0;
}

int per_vertex_matrix_coverage() {
  Fixture fixture;
  RecordingBackend backend;
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  std::vector<std::uint8_t> fifo;
  gx_fixture::push_common_state(fifo);
  gx_fixture::push_cp(fifo, 0x50, 1u | (1u << 9));
  gx_fixture::push_cp(fifo, 0x60, 0);
  gx_fixture::push_xf_f32(fifo, 12,
                        {2.f, 0.f, 0.f, 10.f, 0.f, 3.f, 0.f, 20.f,
                         0.f, 0.f, 4.f, 30.f});
  gx_fixture::push_u8(fifo, 0x90);
  gx_fixture::push_u16(fifo, 3);
  for (std::uint8_t row : {0, 3, 0}) {
    gx_fixture::push_u8(fifo, row);
    for (float value : {1.f, 2.f, 3.f})
      gx_fixture::push_f32(fifo, value);
  }
  CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
  CHECK(backend.draws.size() == 1);
  const DrawRecord& draw = backend.draws[0];
  CHECK(vertex_float(draw, 0, 0) == 1.f);
  CHECK(vertex_float(draw, 1, 0) == 12.f);
  CHECK(vertex_float(draw, 1, 4) == 26.f);
  CHECK(vertex_float(draw, 1, 8) == 42.f);
  CHECK(vertex_float(draw, 2, 0) == 1.f);
  CHECK(draw.position_matrix[0] == 1.f && draw.position_matrix[5] == 1.f &&
        draw.position_matrix[10] == 1.f && draw.position_matrix[15] == 1.f);
  ww_renderer_destroy(renderer);
  std::printf("renderer PNMTXIDX selects per-vertex rows ok\n");
  return 0;
}

int tev_register_supply() {
  Fixture fixture;
  RecordingBackend backend;
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  std::vector<std::uint8_t> fifo;
  gx_fixture::push_common_state(fifo);
  gx_fixture::push_cp(fifo, 0x60, 0);
  gx_fixture::push_bp(fifo, 0xE2, (std::uint32_t(-12) & 0x7FF) | (78u << 12));
  gx_fixture::push_bp(fifo, 0xE3, 56u | (34u << 12));
  gx_fixture::push_bp(fifo, 0xE2, 90u | (67u << 12) | (1u << 23));
  gx_fixture::push_bp(fifo, 0xE3, 45u | (123u << 12) | (1u << 23));
  gx_fixture::push_bp(fifo, 0, 1u << 10);
  gx_fixture::push_bp(fifo, 0xC0, 2u | (15u << 4) | (15u << 8) |
                                (15u << 12) | (2u << 22));
  gx_fixture::push_bp(fifo, 0xC1, (1u << 4) | (7u << 7) | (7u << 10) |
                                (7u << 13) | (2u << 22));
  gx_fixture::push_bp(fifo, 0xC2, 4u | (14u << 4) | (4u << 8) |
                                (15u << 12) | (1u << 19));
  gx_fixture::push_bp(fifo, 0xC3, (2u << 4) | (6u << 7) | (2u << 10) |
                                (7u << 13) | (1u << 19));
  gx_fixture::push_bp(fifo, 0xF6, 4u | (13u << 14) | (29u << 19));
  gx_fixture::push_bp(fifo, 0xF7, 14u);
  gx_fixture::push_bp(fifo, 0xF3, 64u | (192u << 8) | (4u << 16) | (1u << 19));
  gx_fixture::push_draw(fifo, 0x90, 3, {0, 0, 0, 1, 0, 0, 0, 1, 0});
  CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
  CHECK(backend.draws.size() == 1);
  const auto& draw = backend.draws[0];
  CHECK(draw.tev_colors[1][0] == -12.f && draw.tev_colors[1][1] == 34.f &&
        draw.tev_colors[1][2] == 56.f && draw.tev_colors[1][3] == 78.f);
  CHECK(draw.konst_colors[1][0] == 90.f && draw.konst_colors[1][1] == 123.f &&
        draw.konst_colors[1][2] == 45.f && draw.konst_colors[1][3] == 67.f);
  CHECK(draw.cg_fragment.find("c1.rgb = clamp(") != std::string::npos);
  CHECK(draw.cg_fragment.find("konst_colors[1].rgb") != std::string::npos);
  CHECK(draw.alpha_ref[0] == 64.f && draw.alpha_ref[1] == 192.f);
  CHECK(draw.cg_fragment.find("prev.a > alpha_ref.x") != std::string::npos);
  WwRendererStats stats{};
  ww_renderer_stats(renderer, &stats);
  CHECK(stats.programs_refused == 0 && stats.draws == 1);
  ww_renderer_destroy(renderer);
  std::printf("renderer signed TEV and aliased KONST register supply ok\n");
  return 0;
}

int ztexture_is_submitted() {
  Fixture fixture;
  gx_ztexture_fixture::fill_memory(fixture.memory);
  fixture.cpu.ram = fixture.memory.mem1.data();
  fixture.cpu.ram_size = fixture.memory.mem1.size();
  RecordingBackend backend;
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  const auto fifo = gx_ztexture_fixture::clear_quad(2, 2, 0xFFFEFF);
  CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
  CHECK(backend.draws.size() == 1);
  CHECK(backend.draws[0].cg_fragment.find("out float fragment_depth : DEPTH") != std::string::npos);
  CHECK(backend.draws[0].vertex_count == 4 && backend.draws[0].index_count == 6);
  CHECK(backend.draws[0].depth_test == 1 && backend.draws[0].depth_write == 1);
  CHECK(backend.draws[0].depth_func == gx_vita::kGlAlways);
  CHECK(backend.draws[0].ztexture_bias[0] == 65534 && backend.draws[0].ztexture_bias[1] == 255);
  CHECK(backend.draws[0].depth_near == 0);
  CHECK(backend.draws[0].depth_far == 16777215.f / 16777216.f);
  WwRendererStats stats{};
  ww_renderer_stats(renderer, &stats);
  CHECK(stats.plans == 1 && stats.programs_refused == 0 && stats.draws == 1);
  ww_renderer_destroy(renderer);
  std::printf("renderer ZTexture depth output submitted\n");
  return 0;
}

int texture_matrices_are_submitted() {
  for (bool indexed : {false, true}) {
    for (bool projected : {false, true}) {
      Fixture fixture;
      RecordingBackend backend;
      WwRenderer* renderer = nullptr;
      WwRendererConfig config = fixture.config();
      CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
      const auto fifo = gx_ztexture_fixture::matrix_quad(indexed, projected);
      CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
      CHECK(backend.draws.size() == 1);
      const auto& uv = backend.draws[0].geometry.texcoords;
      CHECK(uv.size() == 4 * 8 * 3);
      CHECK(uv[0] == 4 && uv[1] == 5 && uv[2] == (projected ? 2.f : 1.f));
      CHECK(uv[24] == (indexed ? 12.f : 6.f));
      CHECK(uv[25] == (indexed ? 9.f : 5.f));
      CHECK(uv[26] == (projected ? (indexed ? 4.f : 3.f) : 1.f));
      CHECK(backend.draws[0].cg_vertex.find("float3 uv0 : TEXCOORD0") != std::string::npos);
      CHECK((backend.draws[0].cg_fragment.find("max(uv0.z") != std::string::npos) == projected);
      ww_renderer_destroy(renderer);
    }
  }
  std::printf("renderer static/per-vertex ST/STQ texture matrices submitted\n");
  return 0;
}

int folded_post_matrix_is_submitted() {
  Fixture fixture;
  RecordingBackend backend;
  WwRenderer* renderer = nullptr;
  WwRendererConfig config = fixture.config();
  CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
  const auto fifo = gx_ztexture_fixture::post_matrix_quad();
  CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
  CHECK(backend.draws.size() == 1);
  const auto& uv = backend.draws[0].geometry.texcoords;
  CHECK(uv[0] == 14 && uv[1] == 25 && uv[2] == 1);
  ww_renderer_destroy(renderer);
  std::printf("renderer folded static post texture matrix submitted\n");
  return 0;
}

int lighting_and_toon_coordinates_are_submitted() {
  for (std::uint8_t light : {0, 7}) {
    Fixture fixture;
    RecordingBackend backend;
    WwRenderer* renderer = nullptr;
    WwRendererConfig config = fixture.config();
    CHECK(ww_renderer_create(&renderer, &config, &backend) == nullptr);
    const auto fifo = gx_ztexture_fixture::lighting_quad(light);
    CHECK(ww_renderer_write(renderer, fifo.data(), fifo.size()) == nullptr);
    CHECK(backend.draws.size() == 1);
    const auto& draw = backend.draws[0];
    CHECK(vertex_float(draw, 0, gx_fixture::gxc::kVertexColor0Offset) == 63.f / 255.f);
    CHECK(vertex_float(draw, 0, gx_fixture::gxc::kVertexColor0Offset + 4) == 128.f / 255.f);
    CHECK(vertex_float(draw, 0, gx_fixture::gxc::kVertexColor0Offset + 8) == 192.f / 255.f);
    CHECK(vertex_float(draw, 0, gx_fixture::gxc::kVertexColor0Offset + 12) == 1.f);
    CHECK(draw.geometry.texcoords[0] == 63.f / 255.f);
    CHECK(draw.geometry.texcoords[1] == 128.f / 255.f);
    ww_renderer_destroy(renderer);
  }
  std::printf("renderer XF lighting and lit RG toon coordinates submitted, lights 0/7\n");
  return 0;
}

} // namespace

int main() {
  CHECK(primitive_coverage() == 0);
  CHECK(indexed_vertex_coverage() == 0);
  CHECK(indexed_xf_coverage() == 0);
  CHECK(vertex_attribute_coverage() == 0);
  CHECK(display_list_coverage() == 0);
  CHECK(per_vertex_matrix_coverage() == 0);
  CHECK(tev_register_supply() == 0);
  CHECK(ztexture_is_submitted() == 0);
  CHECK(texture_matrices_are_submitted() == 0);
  CHECK(folded_post_matrix_is_submitted() == 0);
  CHECK(lighting_and_toon_coordinates_are_submitted() == 0);
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
