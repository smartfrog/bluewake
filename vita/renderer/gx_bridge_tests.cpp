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
#include "gx_indirect_fixture.h"
#include "gx_emboss_fixture.h"
#include "gx_snapshot_memory.h"

#include "../feasibility/gx/gx_fixture.hpp"

#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
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

int snapshot_hole_is_not_zero_filled_memory() {
  const auto path = std::filesystem::path("/tmp/opencode") /
      ("ww18-coverage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::ofstream file(path);
  file << "GX_MEM1_RANGES_V1\n00000000 00000010\n";
  file.close();
  CHECK(file.good());
  const char* previous = std::getenv("GX_BRIDGE_MEM1_RANGES");
  const std::string saved = previous != nullptr ? previous : "";
  CHECK(setenv("GX_BRIDGE_MEM1_RANGES", path.c_str(), 1) == 0);
  gx_bridge::Result result;
  const bool accepted = run_stream(gx_fixture::build_textured_triangle(), result);
  if (previous != nullptr)
    CHECK(setenv("GX_BRIDGE_MEM1_RANGES", saved.c_str(), 1) == 0);
  else
    CHECK(unsetenv("GX_BRIDGE_MEM1_RANGES") == 0);
  CHECK(std::filesystem::remove(path));
  CHECK(!accepted);
  CHECK(result.error.find("snapshot memory missing") != std::string::npos);
  CHECK(result.error.find("00001000") != std::string::npos);
  return 0;
}

int snapshot_resolver_clips_available_and_handles_aliases() {
  gx_fixture::FixtureContext memory;
  gx_fixture::fill_texture_zones(memory);
  DolGuestAddressResolver source;
  dol_guest_address_resolver_init_callback(&source, gx_fixture::resolve_mem1, &memory);
  gx_bridge::snapshot::Resolver guard(source);
  guard.ranges = {{0x1000, 64}};
  for (u32 address : {0x1000u, 0x80001000u, 0xC0001000u}) {
    DolGuestResolvedRange resolved;
    CHECK(gx_bridge::snapshot::Resolver::resolve(&guard, address, 1,
          DOL_GUEST_ADDRESS_AUTO, DOL_GUEST_RESOURCE_TEXTURE, &resolved));
    CHECK(resolved.available == 64);
    CHECK(resolved.data == memory.mem1.data() + 0x1000);
  }
  return 0;
}

int snapshot_resolver_refuses_crossing_coverage() {
  gx_fixture::FixtureContext memory;
  gx_fixture::fill_texture_zones(memory);
  DolGuestAddressResolver source;
  dol_guest_address_resolver_init_callback(&source, gx_fixture::resolve_mem1, &memory);
  gx_bridge::snapshot::Resolver guard(source);
  guard.ranges = {{0x1000, 64}};
  DolGuestResolvedRange resolved;
  CHECK(!gx_bridge::snapshot::Resolver::resolve(&guard, 0x1000, 65,
        DOL_GUEST_ADDRESS_AUTO, DOL_GUEST_RESOURCE_TEXTURE, &resolved));
  CHECK(guard.missing && guard.missing_size == 65);
  return 0;
}

int snapshot_coverage_reader_merges_adjacent_ranges() {
  std::istringstream file("GX_MEM1_RANGES_V1\n00001000 00000020\n00001020 00000020\n");
  std::vector<gx_bridge::snapshot::Range> ranges;
  std::string error;
  CHECK(gx_bridge::snapshot::read_ranges(file, ranges, error));
  CHECK(ranges.size() == 1 && ranges[0].address == 0x1000 && ranges[0].size == 64);
  return 0;
}

int snapshot_coverage_reader_rejects_bad_bounds() {
  for (const char* body : {"00001000 00000000\n", "017FFFFF 00000002\n",
                           "00001000 00000040\n00001020 00000040\n", "not a range\n"}) {
    std::istringstream file(std::string("GX_MEM1_RANGES_V1\n") + body);
    std::vector<gx_bridge::snapshot::Range> ranges;
    std::string error;
    CHECK(!gx_bridge::snapshot::read_ranges(file, ranges, error));
    CHECK(!error.empty());
  }
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc == 3 && (std::string(argv[1]) == "--write-ztexture-fixture" ||
                    std::string(argv[1]) == "--write-matrix-fixture" ||
                    std::string(argv[1]) == "--write-light-fixture" ||
                    std::string(argv[1]) == "--write-fog-fixture" ||
                    std::string(argv[1]) == "--write-indirect-fixture" ||
                    std::string(argv[1]) == "--write-emboss-fixture")) {
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
    else if (std::string(argv[1]) == "--write-indirect-fixture") {
      gx_fixture::fill_texture_zones(memory);
      fifo = gx_indirect_fixture::quad(2, 1, 3, 7);
    }
    else if (std::string(argv[1]) == "--write-emboss-fixture")
      fifo = gx_emboss_fixture::quad(true, true, 7);
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
  CHECK(snapshot_hole_is_not_zero_filled_memory() == 0);
  CHECK(snapshot_resolver_clips_available_and_handles_aliases() == 0);
  CHECK(snapshot_resolver_refuses_crossing_coverage() == 0);
  CHECK(snapshot_coverage_reader_merges_adjacent_ranges() == 0);
  CHECK(snapshot_coverage_reader_rejects_bad_bounds() == 0);
  for (bool indexed : {false, true}) {
    for (bool chained : {false, true}) {
      gx_bridge::Result result;
      CHECK(run_stream(gx_emboss_fixture::quad(indexed, chained, 7), result));
      CHECK(result.counts.plans_ok == 1 && result.counts.programs == 1);
      CHECK(result.counts.programs_refused == 0 && result.gaps.empty());
    }
  }
  for (std::uint8_t matrix : {0, 1, 2}) {
    for (std::uint8_t format : {0, 1, 2, 3}) {
      gx_bridge::Result result;
      CHECK(run_stream(gx_indirect_fixture::quad(matrix, format, 3, 7), result));
      CHECK(result.counts.plans_ok == 1 && result.counts.programs == 1);
      CHECK(result.counts.programs_refused == 0 && result.plan_skips.empty());
      CHECK(result.gaps.empty());
    }
  }
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
    // The original indirect lookup remains a valid single-texmap path.
    gx_bridge::Result result;
    CHECK(run_stream(gx_fixture::build_indirect_triangle(), result));
    CHECK(result.ok);
    CHECK(result.counts.plans_ok == 1u);
    CHECK(result.counts.programs == 1u && result.counts.programs_refused == 0u);
    CHECK(result.program_refusals.empty());
    std::printf("bridge indirect single-texmap generated\n");
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
