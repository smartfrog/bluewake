// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-17 renderer: the host's captured GX stream through the pinned
// frontend, the gx_vita translation and bounded Cg generation, submitted to a
// backend and shown at the guest's present events. See ww_renderer.h for the
// contract and ww_renderer_backend.h for the drawing surface.
//
// The streaming shape is the pinned frontend's own: bytes are appended with
// write_fifo (a partial command stays buffered), flush parses what is
// complete, and set_stop_at_display_copy ends the parse exactly at the game's
// GXCopyDisp so the frame shown is the frame the game finished. Bytes after a
// display copy wait for the next present: they belong to the next frame and
// must not draw into the buffer being shown.

#include "ww_renderer.h"

#include "ww_renderer_backend.h"

#include "gx_bridge.h"
#include "gx_cg.h"
#include "gx_vita.h"
#include "gx_texgen_sink.h"
#include "gx_frame_capture.h"

#include "gxruntime/aurora_recomp/retail_gx_frontend.hpp"
#include "gxruntime/gxcore/gxcore.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace ar = gxruntime::aurora_recomp;
namespace gxc = gxruntime::gxcore;

struct WwRenderer {
  ar::RetailGxFrontend frontend;
  gx_vita::TexGenSink sink;
  WwRendererBackend* backend = nullptr;
  WwRendererStats stats{};
  std::set<std::uint64_t> program_keys;
  // The gap names the stats' pointers refer to: renderer-owned storage, so
  // they outlive the call that collected them.
  std::array<std::string, WW_RENDERER_MAX_GAPS> gap_names;
  std::string error;
  bool frame_ready = false;
  bool failed = false;
  WwRendererFrameCapture capture = nullptr;
  void* capture_user = nullptr;
};

namespace {

void count_event(const DolGxRecompTraceEvent& event, void* user) {
  (void)event;
  ++static_cast<WwRenderer*>(user)->stats.events;
}

void keep_reason(const char** reasons, std::uint32_t* count,
                 const char* reason) {
  if (reason == nullptr || *count >= WW_RENDERER_MAX_REASONS)
    return;
  reasons[*count] = reason;
  ++*count;
}

// A backend or frontend failure is fatal for the whole run: the renderer keeps
// it and every later call returns it, so the host stops instead of running on
// with a broken picture.
const char* fail(WwRenderer* renderer, const std::string& message) {
  renderer->error = message;
  renderer->failed = true;
  return renderer->error.c_str();
}

const char* frontend_failure(WwRenderer* renderer) {
  char detail[192];
  std::snprintf(detail, sizeof detail,
                "frontend rejected the stream: %s (opcode=0x%02X offset=%zu)",
                renderer->frontend.last_error() != nullptr
                    ? renderer->frontend.last_error()
                    : "unknown",
                static_cast<unsigned>(renderer->frontend.last_error_opcode()),
                renderer->frontend.last_error_offset());
  return fail(renderer, detail);
}

void observe_plan(const gxc::DrawPlan& plan, void* user) {
  auto* renderer = static_cast<WwRenderer*>(user);
  if (renderer->failed)
    return;
  ++renderer->stats.plans;
  if (!plan.ok) {
    ++renderer->stats.plans_skipped;
    keep_reason(renderer->stats.skip_reasons, &renderer->stats.skip_count,
                plan.skip_reason);
    return;
  }
  const gx_vita::TranslatedDraw draw = gx_vita::translate(plan);
  if (!draw.ok) {
    ++renderer->stats.plans_skipped;
    keep_reason(renderer->stats.skip_reasons, &renderer->stats.skip_count,
                draw.skip_reason);
    return;
  }
  const gx_vita::CgProgram program = gx_vita::generate_cg(plan.pipeline.shader);
  if (!program.ok) {
    ++renderer->stats.programs_refused;
    keep_reason(renderer->stats.refusal_reasons, &renderer->stats.refusal_count,
                program.unsupported_reason);
    return;
  }
  const char* error = renderer->backend->submit_draw(draw, program);
  if (error != nullptr) {
    fail(renderer, error);
    return;
  }
  ++renderer->stats.draws;
  ++renderer->stats.programs;
  renderer->program_keys.insert(draw.pipeline.program_key);
  renderer->stats.programs_distinct = renderer->program_keys.size();
}

void observe_copy(const gxc::EfbCopyCommand& cmd, void* user) {
  auto* renderer = static_cast<WwRenderer*>(user);
  if (renderer->failed)
    return;
  if (cmd.format == 0xFu) {
    // GXCopyDisp: the game finished a picture. The frontend stopped the parse
    // here, so the frame's draws are all submitted; the present shows them.
    ++renderer->stats.display_copies;
    return;
  }
  const gx_vita::TranslatedCopy copy = gx_vita::translate_copy(cmd);
  if (!copy.ok) {
    ++renderer->stats.copies_refused;
    keep_reason(renderer->stats.copy_refusal_reasons,
                &renderer->stats.copy_refusal_count, copy.skip_reason);
    return;
  }
  const char* error = renderer->backend->submit_copy(copy);
  if (error != nullptr) {
    fail(renderer, error);
    return;
  }
  ++renderer->stats.copies;
}

void collect_gaps(WwRenderer* renderer) {
  renderer->stats.gap_count = 0;
  for (const gx_bridge::GapEntry& gap : gx_bridge::nonzero_gaps(
           renderer->sink.counters())) {
    if (renderer->stats.gap_count == WW_RENDERER_MAX_GAPS)
      break;
    std::string& name = renderer->gap_names[renderer->stats.gap_count];
    name = gap.name;
    WwRendererGap& entry = renderer->stats.gaps[renderer->stats.gap_count++];
    entry.name = name.c_str();
    entry.value = gap.value;
  }
}

// Parse everything buffered up to the next frame boundary, submit the draws
// and copies in stream order, and plan the frame's last pending draw. A
// display copy leaves the bytes after it buffered and marks the frame ready.
const char* flush(WwRenderer* renderer) {
  if (!renderer->frontend.flush(&renderer->sink))
    return frontend_failure(renderer);
  if (renderer->failed)
    return renderer->error.c_str();
  renderer->sink.flush_frame();
  if (renderer->failed)
    return renderer->error.c_str();
  collect_gaps(renderer);
  if (renderer->frontend.display_copy_stopped())
    renderer->frame_ready = true;
  return nullptr;
}

} // namespace

const char* ww_renderer_create(WwRenderer** out,
                               const WwRendererConfig* config,
                               WwRendererBackend* backend) {
  if (out == nullptr)
    return "renderer out pointer is null";
  *out = nullptr;
  if (config == nullptr)
    return "config is null";
  if (config->memory == nullptr)
    return "config memory is null";
  if (config->cpu == nullptr)
    return "config cpu is null";
  DolGuestAddressResolver resolver;
  dol_guest_address_resolver_init(&resolver, config->memory, config->cpu);
  return ww_renderer_create_with_resolver(out, &resolver, backend);
}

const char* ww_renderer_create_with_resolver(WwRenderer** out,
                                             const DolGuestAddressResolver* resolver,
                                             WwRendererBackend* backend) {
  if (out == nullptr)
    return "renderer out pointer is null";
  *out = nullptr;
  if (resolver == nullptr)
    return "renderer resolver is null";
  if (backend == nullptr)
    backend = ww_renderer_vita_backend();
  if (backend == nullptr)
    return "no renderer backend on this platform";
  auto* renderer = new WwRenderer();
  renderer->backend = backend;
  renderer->frontend.reset(resolver);
  renderer->frontend.set_packet_drain_enabled(true);
  renderer->frontend.set_stop_at_display_copy(true);
  renderer->frontend.set_event_observer(count_event, renderer);
  renderer->sink.set_guest_resolver(&renderer->frontend.state().resolver);
  renderer->sink.set_plan_observer(observe_plan, renderer);
  renderer->sink.set_copy_observer(observe_copy, renderer);
  const char* error = backend->init();
  if (error != nullptr) {
    // The backend's own message is the most useful one; the caller stops.
    delete renderer;
    return error;
  }
  *out = renderer;
  return nullptr;
}

const char* ww_renderer_set_frame_capture(WwRenderer* renderer,
                                          WwRendererFrameCapture capture, void* user) {
  if (renderer == nullptr)
    return "renderer is null";
  if (renderer->failed)
    return renderer->error.c_str();
  renderer->capture = capture;
  renderer->capture_user = user;
  return nullptr;
}

const char* ww_renderer_capture_frame(WwRenderer* renderer,
                                      WwRendererFrameCapture capture, void* user) {
  if (renderer == nullptr)
    return "renderer is null";
  if (capture == nullptr)
    return "frame capture callback is null";
  if (renderer->failed)
    return renderer->error.c_str();
  WwRendererFramePixels pixels;
  const char* error = renderer->backend->readback(pixels);
  if (error != nullptr)
    return fail(renderer, error);
  const WwRendererFrame frame{pixels.width, pixels.height, pixels.rgba.data(), pixels.rgba.size()};
  if (!gx_capture::valid(frame))
    return fail(renderer, "invalid framebuffer readback dimensions/bytes");
  error = capture(&frame, user);
  return error != nullptr ? fail(renderer, error) : nullptr;
}

void ww_renderer_destroy(WwRenderer* renderer) {
  if (renderer == nullptr)
    return;
  renderer->backend->shutdown();
  delete renderer;
}

const char* ww_renderer_write(WwRenderer* renderer, const uint8_t* bytes,
                              size_t size) {
  if (renderer == nullptr)
    return "renderer is null";
  if (renderer->failed)
    return renderer->error.c_str();
  if (size == 0u)
    return nullptr;
  if (bytes == nullptr)
    return "bytes is null with a nonzero size";
  renderer->stats.fifo_bytes += size;
  if (!renderer->frontend.write_fifo({bytes, size}))
    return fail(renderer, "captured GX bytes refused by the pinned frontend");
  if (renderer->frame_ready) {
    // A completed frame waits for its present: these bytes are the next frame
    // and must not draw into the buffer being shown.
    return nullptr;
  }
  return flush(renderer);
}

const char* ww_renderer_present(WwRenderer* renderer, bool* presented) {
  if (renderer == nullptr)
    return "renderer is null";
  if (presented != nullptr)
    *presented = false;
  if (renderer->failed)
    return renderer->error.c_str();
  if (!renderer->frame_ready)
    return nullptr;
  const char* error = renderer->backend->present();
  if (error != nullptr)
    return fail(renderer, error);
  ++renderer->stats.presented;
  renderer->frame_ready = false;
  if (presented != nullptr)
    *presented = true;
  if (renderer->capture != nullptr) {
    error = ww_renderer_capture_frame(renderer, renderer->capture, renderer->capture_user);
    if (error != nullptr)
      return error;
  }
  // The shown frame is complete; the held bytes resume as the next frame,
  // drawing into the fresh back buffer.
  return flush(renderer);
}

void ww_renderer_stats(WwRenderer* renderer, WwRendererStats* out) {
  if (renderer == nullptr || out == nullptr)
    return;
  *out = renderer->stats;
}

const char* ww_renderer_finish(WwRenderer* renderer) {
  if (renderer == nullptr)
    return "renderer is null";
  if (renderer->failed)
    return renderer->error.c_str();
  if (renderer->frame_ready)
    return fail(renderer, "GPU finish cannot bypass an unpresented display copy");
  if (const char* error = flush(renderer))
    return error;
  if (renderer->frontend.pending_fifo_size() != 0u)
    return fail(renderer, "GPU finish requires complete captured commands");
  if (renderer->frame_ready)
    return fail(renderer, "GPU finish cannot bypass an unpresented display copy");
  if (renderer->stats.plans_skipped != 0u || renderer->stats.programs_refused != 0u ||
      renderer->stats.copies_refused != 0u)
    return fail(renderer, "GPU finish cannot confirm refused renderer work");
  if (const char* error = renderer->backend->finish())
    return fail(renderer, error);
  return nullptr;
}

#if !defined(__vita__)
// Host builds have no GPU: a renderer created without an explicit backend
// fails loudly in ww_renderer_create. The Vita definition lives in
// vita_gl_backend.cpp.
WwRendererBackend* ww_renderer_vita_backend(void) { return nullptr; }
#endif
