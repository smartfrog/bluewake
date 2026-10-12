# VITA-WW-17 — renderer attach: captured GX commands to a shown frame (status)

2026-10-12 · **delivered on the host, synthetic end-to-end; the device path compiles for ARM and
waits for the march's first draws**

This document is the running status of VITA-WW-17. It says what is proven, what is explicitly
**PENDING**, and the exact evidence. It does not claim a rendered scene on any device: no real
captured GX draw exists yet, so nothing here has been shown by the Vita's GPU.

## What this ticket does

Attach a renderer to the boot host so the captured command stream stops being a log and becomes
draws: the host hands captured GX FIFO bytes and the guest's present events to one interface, the
renderer runs them through the pinned frontend, the `gx_vita` translation and bounded Cg generation,
submits them through vitaGL, and shows a frame when the game's own display copy completed one. It is
not a general GX/Aurora rewrite and not a performance ticket.

## The interface (`vita/renderer/ww_renderer.h`)

The host-facing contract is a small C header; the renderer core is C++ behind it
(`ww_renderer.cpp`, `ww_renderer_backend.h`). The host hands over three things and gets two back:

    create(&renderer, {DolGuestMemory* memory, CPUState* cpu}, backend|NULL)
        // memory + cpu: the host's own MEM1 view, the same pair the pinned GX
        // trace resolves against; NULL backend selects the platform backend
        // (vitaGL on the Vita), tests pass their own.
    write(renderer, bytes, size)     // one batch of captured GX FIFO bytes, stream order
    present(renderer, &presented)    // the guest's present event (VI / display flip)
    stats(renderer, &stats)          // counters, gaps and bounded refusal reasons
    destroy(renderer)

- **Threading**: every call is on the host thread, in stream order. The renderer starts no threads
  and takes no locks.
- **Lifecycle**: create after `ww_host_init` (memory and CPU exist), destroy before
  `ww_host_shutdown` frees them; the backend outlives the renderer.
- **Errors**: a static or renderer-owned string, NULL when the call succeeded, matching the host's
  `ww_gx_stream_write` convention. A failure is sticky: every later call returns it, so the host
  stops instead of running on with a broken picture.
- **Batches may split a command**: the pinned frontend buffers the partial command; the renderer
  reports nothing for it until the rest arrives.
- **Presentation**: the renderer parses up to the game's display copy (BP 0x52 bit 14, GXCopyDisp)
  with the pinned frontend's `set_stop_at_display_copy`, marks the frame ready, and holds the bytes
  after it. `present` shows the frame (the backend's `vglSwapBuffers`) only when one is ready,
  reports `presented=true`, and then resumes the held bytes as the next frame — which draw into the
  fresh back buffer of vitaGL's triple-buffered swap, never into the buffer being shown. A present
  with no completed frame reports `presented=false` and shows nothing.

## Delivered

- `vita/renderer/ww_renderer.h` — the host contract above (stats: fifo bytes, decoded events,
  plans/plans_skipped, draws, programs/programs_distinct/programs_refused, copies/copies_refused,
  display_copies, presented, nonzero gaps, bounded skip/refusal reasons).
- `vita/renderer/ww_renderer.cpp` — the streaming chain: pinned `RetailGxFrontend` (packet drain,
  stop at display copy) → `GxCoreSink` → `gx_vita::translate` → `gx_cg::generate_cg` → backend;
  copies executed in stream order; display copies counted; a frame held until present.
- `vita/renderer/ww_renderer_backend.h` — the drawing surface: `init`, `submit_draw(draw, cg)`,
  `submit_copy(copy)`, `present`, `shutdown`.
- `vita/renderer/vita_gl_backend.cpp` — the real backend (Vita-only), the VITA-WW-02 demo's
  submission shape plus what a game needs: programs cached by the pinned shader key, one persistent
  vertex/index buffer, one texture object per texmap slot, EFB copy destinations cached by guest
  address. `vglInitExtended` + `vglWaitVblankStart`, context verified through `glGetString` and the
  pools (Cloudpunk's pattern); every GL error is checked and returned, never swallowed.
- `vita/renderer/gx_vita.{h,cpp}` — `TranslatedDraw` now carries the plan's transform
  (`position_matrix` = the XF position rows padded to 4, `projection` = the Dolphin-layout
  projection), the two uniforms the generated Cg reads. The mapping is the pinned WGSL generator's
  (`gxcore_shader.cpp`: `viewpos = (dot(p0,pos4), dot(p1,pos4), dot(p2,pos4), 1)`, then
  `clip = (dot(projection[0],viewpos), …)`), so the Cg's `mul(mul(pos4, position_matrix),
  projection)` is the same arithmetic.
- `vita/renderer/gx_bridge.{h,cpp}` — `nonzero_gaps()` moved to one place so the bridge report and
  the live renderer name the same gaps; no behavior change.
- Tests: `vita/renderer/ww_renderer_tests.cpp` (14 checks through the C interface with a recording
  backend) and `vita/feasibility/gx/test_ww_renderer.py`.
- `docs/vita/VITA-WW-17-host-hook.patch` — the host-side integration, delivered as a patch, not
  committed to this branch.

## Proven (what was run, and the result)

Pinned RecompCore `ddd031b2a7f45fcbda37a49daac9b308b8d95da1` (read-only, pristine), built into
`build-f17/ww17/gx-host` with `scripts/vita/gx/build.py --output /home/fred/projects/zeldaWWHDRecomp/build-f17/ww17`.

- `ww_renderer_tests`: all 14 checks green — create refuses missing input; a backend init failure
  fails the create; an empty write and a present with no frame are no-ops; a textured triangle split
  mid-command becomes one draw (3 vertices, 3 indices, triangles, program key, 4x4 RGBA texture,
  non-empty Cg) with the fixture's own matrices; a display copy presents exactly once; the next
  frame's draws wait for that present and resume after it; the fixture's depth/blend/cull/write
  masks reach the backend; the cel fixture's two texmaps arrive with mask 0x3; an indirect TEV
  program is refused with `indirect TEV lookups not supported` and no draw; an EFB copy runs with
  the fixture's destination; a malformed stream stops with
  `frontend rejected the stream: draw vertex layout unavailable (opcode=0x80 offset=0)`; a backend
  submit failure stops with the backend's reason and is not counted as a draw; the
  `unresolved_tex_matrix` gap is kept; two frames give two draws, two presents and one distinct
  program.
- No 02/15 regression: `test_gx_replay.py` 25, `test_gx_vita.py` 3, `test_gx_cg.py` 3,
  `test_gx_capture.py` 23 (real WW05 evidence, read-only), `test_gx_bridge.py` 16,
  `test_ww_renderer.py` 2 — 72 tests green, plus the C++ binaries `gx_vita_tests`, `gx_cg_tests`,
  `gx_bridge_tests` green.
- The real backend compiles for the target: `ww_renderer.cpp` and `vita_gl_backend.cpp` both
  compile with the Cloudpunk vitasdk's `arm-vita-eabi-g++ -std=c++20 -O2 -Wall -Wextra
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard` against the pinned frontend/gxcore and the pinned
  vitaGL headers. Not linked into a VPK and not run.
- The host patch applies cleanly with `git apply --check` at `8d50556` and on the march's HEAD
  `c7e92d0`, and a patched host build (tests-only configuration, `io_probe`, `gx_stream_probe`,
  `host_probe`) configures, compiles and links; `gx_stream_probe`'s five scenarios and
  `host_probe frontier` still pass, so the renderer hooks change nothing while no renderer is
  attached. The host CMake needs `WW17_RENDERER_DIR` and the pinned runtime with
  `GXRUNTIME_ENABLE_AURORA_RECOMP=ON`, both in the patch.

## PENDING (not proven, honestly open)

| Area | State | Note |
| --- | --- | --- |
| Real captured draws on the Vita | **PENDING** | No real GX draw exists yet; the march must continue past the first command. The renderer is attached by the patch, and its hooks are dormant until bytes flow. |
| A shown frame on Vita3K/hardware | **PENDING** | The submission path compiles for ARM; it has never run with real bytes. Per the ticket's rule, no Vita3K run without real bytes. |
| VI retrace clock and `[frame-timing]`/`[frame-present]` markers | **PENDING (march)** | The present call is at the completed command today (the earliest moment that works); the march moves it to the VI/present site and owns the markers. |
| EFB copy-to-texture fed back to the game | **PARTIAL** | The copy executes into a GL texture keyed by destination address; a later draw that samples that guest range still decodes from guest memory (the readback blocker, VITA-WW-02). |
| Copy-clear after an EFB copy | **PENDING** | The hardware clear after a copy is not applied; the copy itself is. |
| EFB-to-screen mapping | **PENDING** | Draws go to the default framebuffer at 960x544 with a fixed viewport; the game renders 640x528. Aspect/letterboxing is a display decision (measurement ticket). |
| Depth/z convention | **PENDING** | The Cg path has no z flip (the desktop WGSL substrate runs reversed-Z); whether the Vita needs one is a hardware check. |
| Texture upload cache | **PENDING** | Textures are re-specified per draw into per-slot objects (no leak, no cache); the program cache is keyed by the pinned shader key. |

## How to run

```sh
python3 scripts/vita/gx/build.py --output /home/fred/projects/zeldaWWHDRecomp/build-f17/ww17
WW17_BUILD=/home/fred/projects/zeldaWWHDRecomp/build-f17/ww17 \
  python3 vita/feasibility/gx/test_ww_renderer.py
WW02_BUILD=/home/fred/projects/zeldaWWHDRecomp/build-f17/ww17 \
  python3 vita/feasibility/gx/test_gx_bridge.py          # and the other 02/15 wrappers
/home/fred/projects/zeldaWWHDRecomp/build-f17/ww17/gx-host/ww_renderer_tests

git apply docs/vita/VITA-WW-17-host-hook.patch           # in the host's worktree
```

## Rules kept

- Pins read-only (`ref/vita-ww-01/RecompCore`, pristine `ddd031b`); no pin or dependency change.
- One writer in this worktree; `vita/host/**` is not modified here — the host integration is the
  patch file above, for its owner to apply.
- Fixtures are synthetic; no game data, disc content, save or private capture is committed; no
  network, no push, no Vita3K process.
- Outputs (ignored): `/home/fred/projects/zeldaWWHDRecomp/build-f17/`.
