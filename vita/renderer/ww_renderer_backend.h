// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-17 renderer backend contract (C++ side of ww_renderer.h).
//
// The renderer owns the stream chain (pinned frontend -> DrawPlans -> gx_vita
// translation -> bounded Cg); the backend owns the GL context and every GL
// call. The real backend submits through vitaGL (vita_gl_backend.cpp); tests
// pass a recording fake, so the same interface path is exercised without a
// GPU. A backend is created by its owner and outlives the renderer.

#pragma once

#include "gx_cg.h"
#include "gx_vita.h"

struct WwRendererFramePixels {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;
};

// See the typedef in ww_renderer.h: C sees this as an opaque struct.
class WwRendererBackend {
public:
  virtual ~WwRendererBackend() = default;

  // Create the GL context and backend resources. Called once by
  // ww_renderer_create, before any submit. NULL: ready; an error string is
  // fatal and create fails with it.
  virtual const char* init() = 0;

  // Submit one translated draw with its generated Cg program. `draw` and
  // `program` are valid only for the call; a backend that keeps them copies.
  virtual const char* submit_draw(const gx_vita::TranslatedDraw& draw,
                                  const gx_vita::CgProgram& program) = 0;

  // Execute one translated EFB copy-to-texture. Display copies (format 0xF)
  // never arrive here: they are the frame boundary, not a texture copy.
  virtual const char* submit_copy(const gx_vita::TranslatedCopy& copy) = 0;

  // Show the completed frame (vitaGL: vglSwapBuffers).
  virtual const char* present() = 0;

  // Read the most recently presented front buffer, tightly packed RGBA8,
  // bottom row first. Unsupported backends must refuse, never fabricate pixels.
  virtual const char* readback(WwRendererFramePixels&) {
    return "framebuffer readback unsupported by this backend";
  }

  // Release backend resources. The renderer calls it once, at destroy.
  virtual void shutdown() = 0;
};
