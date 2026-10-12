// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-15 bridge CLI: one captured FIFO stream through the pinned frontend,
// the gx_vita translation and bounded Cg generation.
//
//   gx_bridge --fixture <scenario>          one VITA-WW-02 fixture stream
//   gx_bridge --fifo <file> [--mem1 <file>] a captured stream; addresses
//                                           resolve against a zeroed 24 MiB
//                                           MEM1 unless a dump is given
//
// Prints one machine-readable line per fact. Exit: 0 report, 1 the frontend
// rejected the stream, 2 usage or unreadable input.

#include "gx_bridge.h"

#include "gx_fixture.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kMem1Bytes = 24u << 20u;

void print_usage() {
  std::fprintf(stderr,
               "usage: gx_bridge --fixture <scenario>\n"
               "       gx_bridge --fifo <file> [--mem1 <file>]\n");
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    return false;
  out.assign(std::istreambuf_iterator<char>(stream),
             std::istreambuf_iterator<char>());
  return !stream.bad();
}

void print_report(const gx_bridge::Result& result) {
  const gx_bridge::Counts& counts = result.counts;
  std::printf("bridge ok=1 fifo_bytes=%llu events=%llu plans=%llu plans_ok=%llu "
              "plans_skipped=%llu programs=%llu programs_refused=%llu "
              "programs_distinct=%llu copies=%llu copies_ok=%llu copies_refused=%llu\n",
              static_cast<unsigned long long>(result.fifo_bytes),
              static_cast<unsigned long long>(counts.events),
              static_cast<unsigned long long>(counts.plans),
              static_cast<unsigned long long>(counts.plans_ok),
              static_cast<unsigned long long>(counts.plans_skipped),
              static_cast<unsigned long long>(counts.programs),
              static_cast<unsigned long long>(counts.programs_refused),
              static_cast<unsigned long long>(counts.programs_distinct),
              static_cast<unsigned long long>(counts.copies),
              static_cast<unsigned long long>(counts.copies_ok),
              static_cast<unsigned long long>(counts.copies_refused));
  std::printf("events total=%llu bp=%llu xf=%llu draw=%llu texture=%llu "
              "display_list=%llu other=%llu\n",
              static_cast<unsigned long long>(counts.events),
              static_cast<unsigned long long>(counts.events_bp),
              static_cast<unsigned long long>(counts.events_xf),
              static_cast<unsigned long long>(counts.events_draw),
              static_cast<unsigned long long>(counts.events_texture),
              static_cast<unsigned long long>(counts.events_display_list),
              static_cast<unsigned long long>(counts.events_other));
  for (const gx_bridge::GapEntry& gap : result.gaps)
    std::printf("gap %s=%llu\n", gap.name.c_str(),
                static_cast<unsigned long long>(gap.value));
  for (const std::string& reason : result.plan_skips)
    std::printf("skip reason=%s\n", reason.c_str());
  for (const std::string& reason : result.program_refusals)
    std::printf("refused reason=%s\n", reason.c_str());
  for (const std::string& reason : result.copy_refusals)
    std::printf("copy refused reason=%s\n", reason.c_str());
}

int run_stream(std::vector<std::uint8_t> fifo,
               gx_fixture::FixtureContext& memory) {
  DolGuestAddressResolver resolver;
  dol_guest_address_resolver_init_callback(&resolver, gx_fixture::resolve_mem1,
                                           &memory);
  gx_bridge::Result result;
  if (!gx_bridge::run(fifo, resolver, &result)) {
    std::fprintf(stderr, "frontend rejected the stream: %s\n",
                 result.error.c_str());
    return 1;
  }
  print_report(result);
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  std::string fixture;
  std::string fifo_path;
  std::string mem1_path;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--fixture" && i + 1 < argc)
      fixture = argv[++i];
    else if (argument == "--fifo" && i + 1 < argc)
      fifo_path = argv[++i];
    else if (argument == "--mem1" && i + 1 < argc)
      mem1_path = argv[++i];
    else {
      print_usage();
      return 2;
    }
  }
  if (fixture.empty() == fifo_path.empty() || (!fixture.empty() && !mem1_path.empty())) {
    print_usage();
    return 2;
  }
  gx_fixture::FixtureContext memory;
  if (!fixture.empty()) {
    if (!gx_fixture::known_scenario(fixture)) {
      std::fprintf(stderr, "unknown fixture: %s\n", fixture.c_str());
      return 2;
    }
    gx_fixture::fill_texture_zones(memory);
    return run_stream(gx_fixture::build_scenario(fixture), memory);
  }
  std::vector<std::uint8_t> fifo;
  if (!read_file(fifo_path, fifo)) {
    std::fprintf(stderr, "cannot read FIFO file: %s\n", fifo_path.c_str());
    return 2;
  }
  if (mem1_path.empty())
    memory.mem1.assign(kMem1Bytes, 0u);
  else if (!read_file(mem1_path, memory.mem1)) {
    std::fprintf(stderr, "cannot read MEM1 file: %s\n", mem1_path.c_str());
    return 2;
  }
  return run_stream(std::move(fifo), memory);
}
