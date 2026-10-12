// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gxruntime/guest_memory.h"
#include "gxruntime/gx_recomp.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <istream>
#include <string>
#include <vector>

namespace gx_bridge::snapshot {

inline constexpr std::uint32_t kMem1Bytes = 24u << 20;

struct Range {
  std::uint32_t address = 0;
  std::uint32_t size = 0;
};

inline bool read_ranges(std::istream& stream, std::vector<Range>& ranges, std::string& error) {
  ranges.clear();
  std::string line;
  if (!std::getline(stream, line) || line != "GX_MEM1_RANGES_V1") {
    error = "invalid snapshot coverage version";
    return false;
  }
  std::uint32_t previous_end = 0;
  std::uint32_t entries = 0;
  while (std::getline(stream, line)) {
    Range range;
    if (line.size() != 17 || line[8] != ' ') {
      error = "invalid snapshot coverage entry";
      return false;
    }
    const auto address = std::from_chars(line.data(), line.data() + 8, range.address, 16);
    const auto size = std::from_chars(line.data() + 9, line.data() + 17, range.size, 16);
    if (address.ec != std::errc{} || address.ptr != line.data() + 8 ||
        size.ec != std::errc{} || size.ptr != line.data() + 17 ||
        range.size == 0 || range.address >= kMem1Bytes || range.size > kMem1Bytes - range.address ||
        range.address < previous_end || ++entries > 65536) {
      error = "invalid snapshot coverage bounds/order";
      return false;
    }
    previous_end = range.address + range.size;
    if (!ranges.empty() && ranges.back().address + ranges.back().size == range.address)
      ranges.back().size += range.size;
    else
      ranges.push_back(range);
  }
  if (stream.bad() || ranges.empty()) {
    error = "unreadable or empty snapshot coverage";
    return false;
  }
  return true;
}

struct Resolver {
  explicit Resolver(const DolGuestAddressResolver& source) : source(source) {}

  DolGuestAddressResolver source;
  std::vector<Range> ranges;
  bool missing = false;
  std::uint32_t missing_address = 0;
  std::uint32_t missing_size = 0;

  void remember_missing(std::uint32_t address, std::uint32_t size) {
    if (missing)
      return;
    missing = true;
    missing_address = address;
    missing_size = size;
  }

  std::string missing_description() const {
    char text[112];
    std::snprintf(text, sizeof text, "snapshot memory missing address=0x%08X size=%u",
                  missing_address, missing_size);
    return text;
  }

  static bool resolve(void* user, u32 address, u32 size, DolGuestAddressSpace space,
                      DolGuestResourceKind resource, DolGuestResolvedRange* out) {
    auto& self = *static_cast<Resolver*>(user);
    const auto physical = dol_gx_recomp_guest_to_physical(address);
    auto found = std::upper_bound(self.ranges.begin(), self.ranges.end(), physical,
                                  [](u32 value, const Range& range) { return value < range.address; });
    if (found == self.ranges.begin() || out == nullptr || size == 0) {
      self.remember_missing(physical, size);
      return false;
    }
    --found;
    const auto end = found->address + found->size;
    if (physical >= end || size > end - physical) {
      self.remember_missing(physical, size);
      return false;
    }
    if (!dol_guest_address_resolver_resolve(&self.source, address, size, space, resource, out)) {
      self.remember_missing(physical, size);
      return false;
    }
    out->available = std::min(out->available, end - physical);
    return true;
  }
};

} // namespace gx_bridge::snapshot
