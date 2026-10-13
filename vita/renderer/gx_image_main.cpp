// SPDX-License-Identifier: GPL-3.0-or-later
#include "gx_frame_capture.h"
#include "gx_snapshot_memory.h"
#include "ww_renderer_backend.h"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

FILE* report_file = nullptr;

void report(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  std::vfprintf(report_file, format, arguments);
  va_end(arguments);
  std::fflush(report_file);
}

bool read_file(const char* path, std::size_t maximum, bool optional,
               std::vector<std::uint8_t>& bytes, std::string& error, bool* present = nullptr) {
  if (present != nullptr)
    *present = false;
  FILE* file = std::fopen(path, "rb");
  if (file == nullptr) {
    if (optional && errno == ENOENT)
      return true;
    error = std::string("cannot open input ") + path;
    return false;
  }
  if (present != nullptr)
    *present = true;
  bool ok = std::fseek(file, 0, SEEK_END) == 0;
  const long size = ok ? std::ftell(file) : -1;
  ok = size >= 0 && std::size_t(size) <= maximum && std::fseek(file, 0, SEEK_SET) == 0;
  if (ok) {
    bytes.resize(size);
    ok = bytes.empty() || std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
  }
  if (std::fclose(file) != 0)
    ok = false;
  if (!ok)
    error = std::string("invalid/unreadable bounded input ") + path;
  return ok;
}

struct Capture {
  WwRenderer* renderer = nullptr;
  gx_bridge::snapshot::Resolver* memory = nullptr;
  std::uint64_t captured_draws = 0;
  unsigned captures = 0;
  const char* point = "after-gx-present";
  std::string error;
};

const char* write_capture(const WwRendererFrame* frame, void* user) {
  auto& capture = *static_cast<Capture*>(user);
  if (capture.memory->missing) {
    capture.error = capture.memory->missing_description();
    return capture.error.c_str();
  }
  const char* error = gx_capture::write_png(*frame, "ux0:data/ww02/capture-rgba.png");
  if (error == nullptr)
    error = gx_capture::write_png(*frame, "ux0:data/ww02/capture.png", true);
  if (error != nullptr)
    return error;
  WwRendererStats stats{};
  ww_renderer_stats(capture.renderer, &stats);
  capture.captured_draws = stats.draws;
  ++capture.captures;
  report("capture written %ux%u point=%s captures=%u draws=%llu presented=%llu\n",
         frame->width, frame->height, capture.point, capture.captures,
         static_cast<unsigned long long>(stats.draws),
         static_cast<unsigned long long>(stats.presented));
  return nullptr;
}

}

int main() {
  report_file = std::fopen("ux0:data/ww02/report.txt", "w");
  if (report_file == nullptr)
    return 1;
  report("ww02 WW18 private captured FIFO image replay\n");
  std::vector<std::uint8_t> fifo, mem1, ranges;
  std::string error;
  bool ranges_present = false;
  if (!read_file("ux0:data/ww02/stream.fifo", 32u << 20, false, fifo, error) ||
      !read_file("ux0:data/ww02/mem1.bin", 24u << 20, true, mem1, error) ||
      !read_file("ux0:data/ww02/mem1.ranges", 2u << 20, true, ranges, error, &ranges_present)) {
    report("FAIL %s\ndemo done 1\n", error.c_str());
    std::fclose(report_file);
    return 1;
  }
  auto cpu = std::make_unique<CPUState>();
  cpu->ram = mem1.data();
  cpu->ram_size = static_cast<u32>(mem1.size());
  DolGuestMemory guest{};
  DolGuestAddressResolver source;
  dol_guest_address_resolver_init(&source, &guest, cpu.get());
  gx_bridge::snapshot::Resolver memory(source);
  DolGuestAddressResolver guarded;
  const DolGuestAddressResolver* resolver = &source;
  if (ranges_present) {
    std::istringstream stream(std::string(ranges.begin(), ranges.end()));
    if (!gx_bridge::snapshot::read_ranges(stream, memory.ranges, error)) {
      report("FAIL %s\ndemo done 1\n", error.c_str());
      std::fclose(report_file);
      return 1;
    }
    dol_guest_address_resolver_init_callback(&guarded, gx_bridge::snapshot::Resolver::resolve, &memory);
    resolver = &guarded;
  }
  WwRenderer* renderer = nullptr;
  auto* backend = ww_renderer_vita_backend();
  const char* failure = ww_renderer_create_with_resolver(&renderer, resolver, backend);
  Capture capture;
  capture.renderer = renderer;
  capture.memory = &memory;
  if (failure == nullptr)
    failure = backend->present();
  if (failure == nullptr)
    failure = ww_renderer_set_frame_capture(renderer, write_capture, &capture);
  if (failure == nullptr)
    failure = ww_renderer_write(renderer, fifo.data(), fifo.size());
  WwRendererStats stats{};
  if (failure == nullptr) {
    for (;;) {
      bool presented = false;
      failure = ww_renderer_present(renderer, &presented);
      if (failure != nullptr || !presented)
        break;
    }
  }
  ww_renderer_stats(renderer, &stats);
  if (failure == nullptr && memory.missing) {
    error = memory.missing_description();
    failure = error.c_str();
  }
  if (failure == nullptr && stats.draws > capture.captured_draws) {
    capture.point = "prefix-preview-swap-not-gx-present";
    failure = backend->present();
    if (failure == nullptr)
      failure = ww_renderer_capture_frame(renderer, write_capture, &capture);
  }
  if (failure != nullptr)
    report("FAIL %s\n", failure);
  report("draws=%llu programs=%llu plans=%llu plans_skipped=%llu programs_refused=%llu copies_refused=%llu presented=%llu captures=%u\n",
         static_cast<unsigned long long>(stats.draws), static_cast<unsigned long long>(stats.programs),
         static_cast<unsigned long long>(stats.plans), static_cast<unsigned long long>(stats.plans_skipped),
         static_cast<unsigned long long>(stats.programs_refused), static_cast<unsigned long long>(stats.copies_refused),
         static_cast<unsigned long long>(stats.presented), capture.captures);
  for (unsigned n = 0; n < stats.gap_count; ++n)
    report("plan skipped gap=%s value=%llu\n", stats.gaps[n].name, static_cast<unsigned long long>(stats.gaps[n].value));
  const bool complete = failure == nullptr && capture.captures > 0 && stats.draws > 0 &&
                        stats.plans_skipped == 0 && stats.programs_refused == 0 &&
                        stats.copies_refused == 0 && stats.gap_count == 0;
  report("demo done %d\n", complete ? 0 : 1);
  ww_renderer_destroy(renderer);
  std::fclose(report_file);
  return complete ? 0 : 1;
}
