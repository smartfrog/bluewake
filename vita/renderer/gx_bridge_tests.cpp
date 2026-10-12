// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-15 bridge tests: captured FIFO bytes through the pinned frontend,
// the gx_vita translation and bounded Cg generation.
//
// The synthetic streams are VITA-WW-02 fixtures, so every decoded fact is
// checked against bytes the fixture itself wrote. A stream the pinned frontend
// rejects must come back as a refusal carrying its opcode and offset, never as
// a silent empty pass.
//
// Checks are explicit (no assert): a release build must fail loudly too.

#include "gx_bridge.h"
#include "gx_ztexture_fixture.h"
#include "gx_fog_fixture.h"

#include "../feasibility/gx/gx_fixture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
      return 1;                                                              \
    }                                                                        \
  } while (0)

namespace {

bool run_stream(std::vector<std::uint8_t> fifo, gx_bridge::Result& out) {
  gx_fixture::FixtureContext ctx;
  gx_fixture::fill_texture_zones(ctx);
  DolGuestAddressResolver resolver;
  dol_guest_address_resolver_init_callback(&resolver, gx_fixture::resolve_mem1,
                                           &ctx);
  return gx_bridge::run(fifo, resolver, &out);
}

const gx_bridge::GapEntry* find_gap(const gx_bridge::Result& result,
                                    const char* name) {
  for (const auto& gap : result.gaps) {
    if (gap.name == name)
      return &gap;
  }
  return nullptr;
}

} // namespace

int main(int argc, char** argv) {
  if (argc == 3 && (std::string(argv[1]) == "--write-ztexture-fixture" ||
                    std::string(argv[1]) == "--write-matrix-fixture" ||
                    std::string(argv[1]) == "--write-light-fixture" ||
                    std::string(argv[1]) == "--write-fog-fixture")) {
    const std::filesystem::path directory(argv[2]);
    std::filesystem::create_directories(directory);
    gx_fixture::FixtureContext memory;
    gx_ztexture_fixture::fill_memory(memory);
    auto fifo = gx_ztexture_fixture::clear_quad();
    if (std::string(argv[1]) == "--write-matrix-fixture")
      fifo = gx_ztexture_fixture::matrix_quad(true, true);
    else if (std::string(argv[1]) == "--write-light-fixture")
      fifo = gx_ztexture_fixture::lighting_quad(7);
    else if (std::string(argv[1]) == "--write-fog-fixture")
      fifo = gx_fog_fixture::quad(4, false, true);
    std::ofstream fifo_file(directory / "clear.fifo", std::ios::binary);
    std::ofstream memory_file(directory / "clear.mem1", std::ios::binary);
    fifo_file.write(reinterpret_cast<const char*>(fifo.data()), fifo.size());
    memory_file.write(reinterpret_cast<const char*>(memory.mem1.data()), memory.mem1.size());
    fifo_file.close();
    memory_file.close();
    CHECK(fifo_file.good() && memory_file.good());
    return 0;
  }
  CHECK(argc == 1);
  for (std::uint8_t mode : {2, 4, 5, 6, 7}) {
    for (bool orthographic : {false, true}) {
      for (bool range : {false, true}) {
        gx_bridge::Result result;
        CHECK(run_stream(gx_fog_fixture::quad(mode, orthographic, range), result));
        CHECK(result.counts.plans_ok == 1 && result.counts.programs == 1);
        CHECK(result.counts.programs_refused == 0 && result.plan_skips.empty());
        CHECK(result.gaps.empty());
      }
    }
  }
  for (std::uint8_t light : {0, 7}) {
    gx_bridge::Result result;
    CHECK(run_stream(gx_ztexture_fixture::lighting_quad(light), result));
    CHECK(result.counts.plans_ok == 1 && result.counts.programs == 1);
    CHECK(result.counts.programs_refused == 0 && result.plan_skips.empty());
  }
  for (bool indexed : {false, true}) {
    for (bool projected : {false, true}) {
      gx_bridge::Result result;
      CHECK(run_stream(gx_ztexture_fixture::matrix_quad(indexed, projected), result));
      CHECK(result.counts.plans_ok == 1 && result.counts.programs == 1);
      CHECK(result.counts.programs_refused == 0 && result.plan_skips.empty());
    }
  }
  {
    gx_fixture::FixtureContext memory;
    gx_ztexture_fixture::fill_memory(memory);
    DolGuestAddressResolver resolver;
    dol_guest_address_resolver_init_callback(&resolver, gx_fixture::resolve_mem1, &memory);
    gx_bridge::Result result;
    CHECK(gx_bridge::run(gx_ztexture_fixture::clear_quad(), resolver, &result));
    CHECK(result.counts.plans_ok == 1);
    CHECK(result.counts.programs == 1 && result.counts.programs_refused == 0);
    CHECK(result.program_refusals.empty());
    std::printf("bridge synthetic Wind Waker ZTexture clear quad ok\n");
  }
  {
    gx_fixture::gxc::GapCounters counters;
    counters.logic_op_ignored = 1;
    const auto gaps = gx_bridge::nonzero_gaps(counters);
    CHECK(gaps.size() == 1);
    CHECK(gaps[0].name == "logic_op_ignored" && gaps[0].value == 1);
  }
  {
    // An empty capture (the real state today) is an honest no-op.
    gx_bridge::Result result;
    CHECK(run_stream({}, result));
    CHECK(result.ok);
    CHECK(result.fifo_bytes == 0u);
    CHECK(result.counts.events == 0u && result.counts.plans == 0u &&
          result.counts.programs == 0u && result.counts.copies == 0u);
    std::printf("bridge empty ok\n");
  }

  {
    // One textured triangle: decoded events, one plan, one Cg program.
    gx_bridge::Result result;
    CHECK(run_stream(gx_fixture::build_textured_triangle(), result));
    CHECK(result.ok);
    CHECK(result.fifo_bytes == gx_fixture::build_textured_triangle().size());
    CHECK(result.counts.events > 0u);
    CHECK(result.counts.events_draw == 1u);
    CHECK(result.counts.events_bp > 0u);
    CHECK(result.counts.events_xf > 0u);
    CHECK(result.counts.plans == 1u);
    CHECK(result.counts.plans_ok == 1u && result.counts.plans_skipped == 0u);
    CHECK(result.counts.programs == 1u && result.counts.programs_refused == 0u);
    CHECK(result.counts.programs_distinct == 1u);
    CHECK(result.counts.copies == 0u);
    CHECK(result.plan_skips.empty());
    CHECK(result.program_refusals.empty());
    const gx_bridge::GapEntry* gap = find_gap(result, "unresolved_tex_matrix");
    CHECK(gap != nullptr && gap->value == 1u);
    std::printf("bridge textured_triangle ok events=%llu\n",
                static_cast<unsigned long long>(result.counts.events));
  }

  {
    // An active indirect lookup translates but the bounded Cg generator
    // refuses it: the refusal keeps its reason instead of vanishing.
    gx_bridge::Result result;
    CHECK(run_stream(gx_fixture::build_indirect_triangle(), result));
    CHECK(result.ok);
    CHECK(result.counts.plans_ok == 1u);
    CHECK(result.counts.programs == 0u && result.counts.programs_refused == 1u);
    CHECK(result.program_refusals.size() == 1u);
    CHECK(result.program_refusals[0] == "indirect TEV lookups not supported");
    std::printf("bridge indirect refused: %s\n",
                result.program_refusals[0].c_str());
  }

  {
    // The EFB copy path: one resolved copy, translated, and its draw's program.
    gx_bridge::Result result;
    CHECK(run_stream(gx_fixture::build_copy_triangle(), result));
    CHECK(result.ok);
    CHECK(result.counts.copies == 1u);
    CHECK(result.counts.copies_ok == 1u && result.counts.copies_refused == 0u);
    CHECK(result.counts.programs == 1u);
    std::printf("bridge copy ok dest_size=%llu\n",
                static_cast<unsigned long long>(result.counts.copies_ok));
  }

  {
    // Two different shader keys in one stream: two plans, two programs, two
    // distinct Cg sources.
    std::vector<std::uint8_t> fifo = gx_fixture::build_textured_triangle();
    const std::vector<std::uint8_t> cel = gx_fixture::build_cel_triangle();
    fifo.insert(fifo.end(), cel.begin(), cel.end());
    gx_bridge::Result result;
    CHECK(run_stream(std::move(fifo), result));
    CHECK(result.ok);
    CHECK(result.counts.plans == 2u);
    CHECK(result.counts.programs == 2u);
    CHECK(result.counts.programs_distinct == 2u);
    std::printf("bridge two keys distinct=2\n");
  }

  {
    // A stream the pinned frontend rejects: the refusal names its opcode and
    // offset; nothing is reported as decoded.
    gx_bridge::Result result;
    CHECK(!run_stream({0x80u, 0x00u, 0x01u}, result));
    CHECK(!result.ok);
    CHECK(result.error.find("opcode=0x80") != std::string::npos);
    CHECK(result.counts.plans == 0u);
    std::printf("bridge rejection: %s\n", result.error.c_str());
  }

  std::printf("gx_bridge: all checks ok\n");
  return 0;
}
