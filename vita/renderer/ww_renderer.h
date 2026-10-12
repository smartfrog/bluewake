// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-17 host -> renderer interface.
//
// The WW05 boot host owns the guest CPU, MEM1 and the captured GX byte
// stream; this interface is the one seam where that stream becomes pictures.
// The host hands the renderer
//   - captured GX FIFO bytes, in stream order (batches may split a command:
//     the renderer buffers partial commands itself),
//   - the guest memory view the stream's addresses resolve against, and
//   - the guest's present events (VI / display flip).
// The renderer answers with counters and, at a present event, whether a frame
// actually reached the display.
//
// Threading: every call is on the host thread, in stream order. The renderer
// starts no threads and takes no locks. Lifecycle: ww_renderer_create after
// the host's memory and CPU exist, ww_renderer_destroy before they are freed;
// the backend outlives the renderer.
//
// Errors are returned as a static or renderer-owned string, NULL when the call
// succeeded, matching the host's ww_gx_stream_write convention: the host must
// stop on a non-NULL error, never continue with a broken renderer.

#ifndef WW17_RENDERER_H
#define WW17_RENDERER_H

#include "gxruntime/guest_memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WwRenderer WwRenderer;

// The drawing surface. Opaque here: C++ code implements it (ww_renderer_
// backend.h). NULL in ww_renderer_create selects the platform backend
// (vitaGL on the Vita); a NULL platform backend is a create-time error.
typedef struct WwRendererBackend WwRendererBackend;

typedef struct WwRendererConfig {
    // The host's guest memory and CPU. The CPU's ram is the MEM1 view the
    // stream's vertices, textures and palettes resolve against; the same pair
    // the host gives the pinned GX trace (dol_guest_address_resolver_init).
    DolGuestMemory* memory;
    CPUState* cpu;
} WwRendererConfig;

enum {
    // Bounded reports: the first N gaps and refusal reasons are kept, so a
    // pathological stream cannot grow the renderer without bound. The counters
    // above them count everything.
    WW_RENDERER_MAX_GAPS = 16,
    WW_RENDERER_MAX_REASONS = 16,
};

typedef struct WwRendererGap {
    const char* name;
    uint64_t value;
} WwRendererGap;

typedef struct WwRendererStats {
    uint64_t fifo_bytes;       // captured bytes handed to the renderer
    uint64_t events;           // decoded trace events (one per FIFO command)
    uint64_t plans;            // DrawPlans built, usable or skipped
    uint64_t plans_skipped;    // plans the frontend/gxcore refused (reason kept)
    uint64_t draws;            // translated draws submitted to the backend
    uint64_t programs;         // Cg program pairs generated and submitted
    uint64_t programs_distinct;// unique shader keys among the submitted programs
    uint64_t programs_refused; // bounded Cg refusals (reason kept)
    uint64_t copies;           // EFB copies executed through the backend
    uint64_t copies_refused;   // copy translations refused (reason kept)
    uint64_t display_copies;   // GX display copies seen (the game's flips)
    uint64_t presented;        // swaps issued at present events
    uint32_t gap_count;
    WwRendererGap gaps[WW_RENDERER_MAX_GAPS];
    uint32_t skip_count;                     // kept skip reasons
    const char* skip_reasons[WW_RENDERER_MAX_REASONS];
    uint32_t refusal_count;                  // kept Cg refusal reasons
    const char* refusal_reasons[WW_RENDERER_MAX_REASONS];
    uint32_t copy_refusal_count;             // kept copy refusal reasons
    const char* copy_refusal_reasons[WW_RENDERER_MAX_REASONS];
} WwRendererStats;

// Create a renderer over the host's memory view. `backend` NULL selects the
// platform backend; tests pass their own. On success *out is set and NULL is
// returned; on failure *out is NULL and the error names what failed (including
// a backend whose init failed).
const char* ww_renderer_create(WwRenderer** out,
                               const WwRendererConfig* config,
                               WwRendererBackend* backend);

// Tear down a renderer (NULL is a no-op). The backend is shut down but not
// freed: it belongs to the caller.
void ww_renderer_destroy(WwRenderer* renderer);

// Hand one batch of captured GX FIFO bytes (big-endian, the exact bytes the
// WW05 host captures) to the renderer. An empty batch is a valid no-op. A
// batch may split a command; the renderer keeps the partial command until the
// rest arrives. Draws and copies run in stream order as they decode; a display
// copy ends the frame and the bytes after it wait for the next present.
const char* ww_renderer_write(WwRenderer* renderer, const uint8_t* bytes,
                              size_t size);

// The guest presented a frame (VI retrace / display flip). When a display copy
// completed a frame since the last present, the renderer shows it (the
// backend's present) and reports presented=true, then resumes the held bytes
// as the next frame; otherwise it reports presented=false and shows nothing.
// `presented` may be NULL when the caller does not need the flag.
const char* ww_renderer_present(WwRenderer* renderer, bool* presented);

// Copy the counters out. NULL `out` is a programming error (the call does
// nothing); `renderer` NULL is a programming error too.
void ww_renderer_stats(WwRenderer* renderer, WwRendererStats* out);

// The platform backend: vitaGL on the Vita. Returns NULL where no GPU exists
// (host builds); a renderer created with it fails loudly there.
WwRendererBackend* ww_renderer_vita_backend(void);

#ifdef __cplusplus
}
#endif

#endif
