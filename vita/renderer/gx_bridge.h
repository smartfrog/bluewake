// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-15 GX bridge: captured FIFO bytes through the pinned frontend into
// gx_vita's translation and bounded Cg generation, with every refusal kept.
//
// The bridge consumes the byte stream the WW05 boot host captures from the
// gather pipe (the report's GX_FRONTIER hex= field, gather batches, or a raw
// FIFO dump): the same big-endian stream the VITA-WW-02 fixtures write, so
// both sides meet on one format. Nothing here guesses at undecoded bytes: a
// frontend rejection is returned with its opcode and offset, a plan the
// consumer skips keeps its reason, and a refused Cg program keeps its
// unsupported_reason.

#pragma once

#include "gxruntime/gxcore/gxcore.hpp"
#include "gxruntime/guest_memory.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gx_bridge {

// Decoded command sightings, plans, programs and copies: the honest totals
// the report prints. events counts every trace event the pinned frontend
// emitted for the stream (one per decoded FIFO command observation).
struct Counts {
  std::uint64_t events = 0;
  std::uint64_t events_bp = 0;
  std::uint64_t events_xf = 0;
  std::uint64_t events_draw = 0;
  std::uint64_t events_texture = 0;
  std::uint64_t events_display_list = 0;
  std::uint64_t events_other = 0;
  std::uint64_t plans = 0;
  std::uint64_t plans_ok = 0;
  std::uint64_t plans_skipped = 0;
  std::uint64_t programs = 0;
  std::uint64_t programs_refused = 0;
  std::uint64_t programs_distinct = 0; // unique shader keys among the programs
  std::uint64_t copies = 0; // texture destinations; GXCopyDisp is a frame boundary
  std::uint64_t copies_ok = 0;
  std::uint64_t copies_refused = 0;
};

struct GapEntry {
  std::string name;
  std::uint64_t value = 0;
};

struct Result {
  bool ok = false; // false: the pinned frontend rejected the stream
  std::string error; // rejection detail (reason, opcode, offset)
  std::uint64_t fifo_bytes = 0;
  Counts counts;
  std::vector<std::string> plan_skips;       // plan.ok == false reasons
  std::vector<std::string> program_refusals; // Cg refusals, in order
  std::vector<std::string> copy_refusals;    // copy translation refusals
  std::vector<GapEntry> gaps;                // nonzero gap counters
};

// Run one captured FIFO stream. The resolver supplies the guest memory the
// stream's addresses resolve against (vertices, textures, palettes); a null
// `out` is a programming error and returns false. An empty stream is a valid,
// empty run.
bool run(std::span<const std::uint8_t> fifo,
         const DolGuestAddressResolver& resolver, Result* out);

// The nonzero gap counters as (name, value) pairs, in a fixed order. The
// bridge report and the live renderer (ww_renderer) name the same gaps, so
// the list lives in one place.
std::vector<GapEntry> nonzero_gaps(
    const gxruntime::gxcore::GapCounters& counters);

} // namespace gx_bridge
