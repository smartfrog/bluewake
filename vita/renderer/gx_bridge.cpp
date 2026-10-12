// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-15 GX bridge implementation. See gx_bridge.h for the contract.

#include "gx_bridge.h"

#include "gx_cg.h"
#include "gx_vita.h"

#include "gxruntime/aurora_recomp/retail_gx_frontend.hpp"
#include "gxruntime/gxcore/gxcore.hpp"

#include <cstdio>
#include <set>
#include <utility>

namespace ar = gxruntime::aurora_recomp;
namespace gxc = gxruntime::gxcore;

namespace gx_bridge {
namespace {

void count_event(const DolGxRecompTraceEvent& event, void* user) {
  auto* counts = static_cast<Counts*>(user);
  ++counts->events;
  switch (event.kind) {
  case DOL_GX_RECOMP_EVENT_BP_REG:
    ++counts->events_bp;
    break;
  case DOL_GX_RECOMP_EVENT_XF_LOAD:
  case DOL_GX_RECOMP_EVENT_INDEXED_XF_LOAD:
    ++counts->events_xf;
    break;
  case DOL_GX_RECOMP_EVENT_DRAW:
    ++counts->events_draw;
    break;
  case DOL_GX_RECOMP_EVENT_TEXTURE:
  case DOL_GX_RECOMP_EVENT_TLUT:
    ++counts->events_texture;
    break;
  case DOL_GX_RECOMP_EVENT_DISPLAY_LIST:
    ++counts->events_display_list;
    break;
  default:
    ++counts->events_other;
    break;
  }
}

struct Collector {
  Result* result = nullptr;
  std::set<std::uint64_t> program_keys;
};

void observe_plan(const gxc::DrawPlan& plan, void* user) {
  auto* collector = static_cast<Collector*>(user);
  Result* result = collector->result;
  ++result->counts.plans;
  if (!plan.ok) {
    ++result->counts.plans_skipped;
    result->plan_skips.emplace_back(plan.skip_reason != nullptr
                                        ? plan.skip_reason
                                        : "plan not ok");
    return;
  }
  const gx_vita::TranslatedDraw draw = gx_vita::translate(plan);
  if (!draw.ok) {
    ++result->counts.plans_skipped;
    result->plan_skips.emplace_back(draw.skip_reason != nullptr
                                       ? draw.skip_reason
                                       : "translation not ok");
    return;
  }
  ++result->counts.plans_ok;
  const gx_vita::CgProgram program = gx_vita::generate_cg(plan.pipeline.shader);
  if (!program.ok) {
    ++result->counts.programs_refused;
    result->program_refusals.emplace_back(program.unsupported_reason != nullptr
                                              ? program.unsupported_reason
                                              : "unknown");
    return;
  }
  ++result->counts.programs;
  collector->program_keys.insert(gx_vita::shader_key_hash(plan.pipeline.shader));
}

void observe_copy(const gxc::EfbCopyCommand& cmd, void* user) {
  auto* result = static_cast<Result*>(user);
  ++result->counts.copies;
  const gx_vita::TranslatedCopy translated = gx_vita::translate_copy(cmd);
  if (translated.ok) {
    ++result->counts.copies_ok;
    return;
  }
  ++result->counts.copies_refused;
  result->copy_refusals.emplace_back(translated.skip_reason != nullptr
                                         ? translated.skip_reason
                                         : "copy not ok");
}

// Retain unresolved upstream diagnostics, not activity counters for paths that
// the Vita translation implements.
void collect_gaps(const gxc::GapCounters& counters, Result* result) {
  result->gaps = nonzero_gaps(counters);
}

} // namespace

std::vector<GapEntry> nonzero_gaps(const gxc::GapCounters& counters) {
  const std::pair<const char*, unsigned long long> gaps[] = {
      {"missing_vcd", counters.missing_vcd},
      {"vertex_decode_failures", counters.vertex_decode_failures},
      {"vertex_array_unresolved", counters.vertex_array_unresolved},
      {"vertex_array_out_of_bounds", counters.vertex_array_out_of_bounds},
      {"unsupported_texgen", counters.unsupported_texgen},
      {"tlut_texture", counters.tlut_texture},
      {"efb_copy_ignored", counters.efb_copy_ignored},
      {"indirect_ignored", counters.indirect_ignored},
      {"fog_ignored", counters.fog_ignored},
      {"lighting_ignored", counters.lighting_ignored},
      {"unresolved_tex_matrix", counters.unresolved_tex_matrix},
      {"alpha_compare_ignored", counters.alpha_compare_ignored},
      {"tev_stages_over", counters.tev_stages_over},
      {"logic_op_ignored", counters.logic_op_ignored},
      {"ztexture_ignored", counters.ztexture_ignored},
  };
  std::vector<GapEntry> entries;
  for (const auto& [name, value] : gaps) {
    if (value != 0u)
      entries.push_back(GapEntry{name, value});
  }
  return entries;
}

bool run(std::span<const std::uint8_t> fifo,
         const DolGuestAddressResolver& resolver, Result* out) {
  if (out == nullptr)
    return false;
  *out = Result{};
  out->fifo_bytes = fifo.size();
  if (fifo.empty()) {
    // The pinned frontend refuses a zero-byte replay by design; nothing to
    // decode is a valid, empty run, not a rejection.
    out->ok = true;
    return true;
  }

  ar::RetailGxFrontend frontend;
  frontend.reset(&resolver);
  frontend.set_packet_drain_enabled(true);
  frontend.set_event_observer(count_event, &out->counts);

  gxc::GxCoreSink sink;
  sink.set_guest_resolver(&frontend.state().resolver);
  Collector collector;
  collector.result = out;
  sink.set_plan_observer(observe_plan, &collector);
  sink.set_copy_observer(observe_copy, out);

  if (!frontend.replay_fifo(fifo, &sink)) {
    char detail[160];
    std::snprintf(detail, sizeof detail, "%s (opcode=0x%02X offset=%zu)",
                  frontend.last_error() != nullptr ? frontend.last_error()
                                                   : "unknown",
                  static_cast<unsigned>(frontend.last_error_opcode()),
                  frontend.last_error_offset());
    out->error = detail;
    return false;
  }
  sink.flush_frame();
  out->counts.programs_distinct = collector.program_keys.size();
  collect_gaps(sink.counters(), out);
  out->ok = true;
  return true;
}

} // namespace gx_bridge
