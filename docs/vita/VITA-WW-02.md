# VITA-WW-02 — GX front-end to a GXM/vitaGL path (status)

2026-10-11 · [ticket](https://trello.com/c/Au9LWb5M) · **in progress: host fixtures and
translation delivered, device path pending**

This document is the running status of VITA-WW-02. It says what is proven, what is
explicitly **PENDING**, and what the remaining risks are. It does not claim a rendered
scene, a device result or performance.

## What this ticket does

Reuse the existing GX frontend, the decoded draw plans and the existing codecs to
demonstrate a representative GXM/vitaGL path that the real stream from VITA-WW-05 can
plug into later. It is not a general GX/Aurora rewrite and not a full renderer.

## Delivered (host, pinned sources, all tests green)

- `vita/feasibility/gx/` — documented synthetic GX streams (opcode encodings verified
  against the pinned frontend) replayed through `RetailGxFrontend` and `GxCoreSink`:
  - `textured_triangle`: direct position/tex0, 4×4 RGBA8 texture;
  - `ci8_triangle`: 8×8 CI8 through a 16-entry RGB5A3 palette;
  - `state_triangle`: cull, depth (LEqual, update), SrcAlpha/InvSrcAlpha blending,
    RGBA6 target (alpha update effective);
  - `cel_triangle`: two texgens with a Color0 toon source, channel 0 lit by light 0,
    a two-stage TEV (ramp × raster, then diffuse × prev), **two bound images**
    (diffuse in slot 0, ramp in slot 1);
  - `lines_pair`, `subtract_triangle`, `dstalpha_triangle`: primitive topology, GX
    SUBTRACT blending, BP 0x42 constant destination alpha;
  - `dstalpha_stamp_triangle`: the guest's capture-stamp pattern, BP 0x42 with
    colour updates off.
- `vita/renderer/gx_vita.{h,cpp}` — one `DrawPlan` becomes the command set a vitaGL
  submission would issue:
  - pipeline state in GL tokens (depth, blend with separate alpha factors and
    subtract equation, cull, write masks, explicit front face);
  - decoded geometry (fixed gxcore layout) and indices;
  - **per-texmap** textures when more than one texmap is sampled (samplers indexed by
    texmap), single flat texture otherwise;
  - textures decoded to RGBA8 by the pinned codecs (CI needs its palette, otherwise
    the upload stays invalid);
  - a deterministic Cg program key (FNV-1a over the `ShaderKey`, layout pinned).
- Tests: `test_gx_replay.py` (decoded facts), `test_gx_vita.py` +
  `gx_vita_tests` (translation, golden texels derived from the documented GX formats,
  pinned ShaderKey layout/hash). Checks are explicit, so release builds fail loudly.
- Build: `scripts/vita/gx/build.py` verifies the pinned RecompCore (`ddd031b`,
  pristine) and builds into the ignored `build-f2/ww02`.

## Vita3K demo result (run `a29e7e27`-family, isolated profile)

The Vita fixture (`vita/feasibility/gx/gx_vita_main.cpp`, VPK `ww02_gx_demo`,
title `BWWG00002`) was built for ARM against the pinned frontend/gxcore and
submitted through the Cloudpunk stack's no-splash vitaGL. The launcher
(`scripts/vita/gx/launch.py`) follows the established isolated pattern: fresh
private profile, staged firmware `vs0/sa0` and `libshacccg.suprx` (local
system files, never committed), bwrap containment, no network, Wayland socket
for the GUI (a short window appears while the demo runs).

**What the emulator proved** (report `ux0:data/ww02/report.txt`, collected in
the run's `evidence/`):

- vitaGL initialises: `OpenGL ES 2.0 VitaGL`, RAM pool 64 MiB, VRAM pool
  112 MiB. (`vglInitExtended`'s return is not a success flag; the fixture
  checks `glGetString` and the pools, Cloudpunk's pattern.)
- `libshacccg.suprx` loads and compiles the generated Cg: three programs
  (vertex + fragment per scenario), including vitaGL's internal clear shader.
- Three draws are submitted with no GL error; the EFB copy
  (`glCopyTexSubImage2D`) runs with no GL error; the readback copies 544 rows
  into the guest buffer with dirty marking; the PNG capture is written.

**The blocker (emulator-side, documented, not a demo defect):** the rendered
pixels never reach guest memory on this Vita3K build. A red full-screen clear
reads back `0,0,0,0` from the back buffer, the front buffer and a complete
FBO (`glCheckFramebufferStatus` = `0x8CD5`), with no GL error and no
`sceGxmTransfer` in the log (the log shows only the benign stubs:
`sceGxmMapFragment/VertexUsseMemory`, `sceGxmSetVisibilityBuffer`,
`sceGxmVshInitialize`). The emitted `capture.png` is therefore black: it is
kept as the blocker's evidence, not as a rendered image. Visual validation
waits for physical hardware (or a Vita3K fix); the demo distinguishes
`demo done 0` (all green) from `demo done 2` (path ran, pixel checks blocked),
so a future run on hardware fails loudly if pixels are still wrong.

## PENDING (explicit, not silently dropped)

| Area | State | Note |
| --- | --- | --- |
| Water / indirect stages | **decoded, refused** | The indirect stage is decoded (`ind_stages`, `ind_matrix_index`) and the bounded Cg generator refuses an active lookup with a reason (`GapCounters::indirect_ignored` stays the counter). No indirect rendering yet. |
| HUD (2D draws) | **delivered (fixture)** | `hud_quad`: four screen-space vertices, two triangles, alpha blend, no depth. |
| EFB copy → texture | **delivered (host)** | `translate_copy`: destination, format, source rectangle, write masks and the following clear; a display copy is refused with a reason. |
| EFB readback to guest memory | **delivered (host logic)** | `copy_readback_to_guest`: RGBA8 rows into the guest stride with per-row dirty marking and bounds refusal. The device read (glReadPixels) comes with the Vita3K demo. |
| Cg program generation | **bounded, delivered** | `vita/renderer/gx_cg.{h,cpp}`: untextured passthrough, textured passthrough, regular TEV combines (Prev/Tex/Raster/Zero/One/Half args, add/subtract, clamp, 1x/2x/4x scale, Regular and Colour texgens), and the BP 0x42 constant destination alpha in the mono-pass when the colour pass cannot observe the fragment alpha (`WW_RENDERER_DST_ALPHA_ADMIT`, off by default; `gx_dst_alpha.h`). Anything else is refused with a reason. Compiled by the Vita's Cg runtime in the demo. |
| Vita3K / device run | **run delivered, pixels blocked** | The demo path runs on Vita3K (init, Cg compile, draws, copy, readback, capture); rendered pixels are not readable from guest memory on this emulator build. See the Vita3K section. |
| Physical console | **PENDING** | No hardware measurement; emulator timings never extrapolate to hardware. The pixel checks must be re-run on hardware. |

## Known limits

- The translation covers the states the fixtures exercise. Anything else is a gap that
  must be counted and reported, never ignored.
- `front_face` is GL_CCW (the GL default); the GX winding mapping is **to be
  established** against hardware.
- vitaGL has no blend colour, so BP 0x42 constant destination alpha is a shader
  contract (`use_dst_alpha`/`dst_alpha` carried to the Cg generator), not GL state.
  `vita/renderer/gx_dst_alpha.h` is the single admission predicate. With
  `WW_RENDERER_DST_ALPHA_ADMIT` (off by default) the Cg generator emits the
  constant alpha in the same pass when the colour blend cannot observe the
  fragment alpha: blending off, colour updates off, or colour factors outside
  SRCALPHA/INVSRCALPHA. gxcore already forces the alpha factors to ONE/ZERO, so
  that class is exact with no extra draw and no second program variant. The
  remaining conflict (the colour blend reads the computed alpha) keeps its own
  refusal reason, `destination alpha conflicts with the colour blend source alpha
  (two-pass split required)`; the two-pass split is the future option for that
  class. Subtract-mode draws stay refused with
  `destination alpha with blend subtract stays refused until the ONE/ONE rule is
  verified here` until that rule is checked on this path. The admission is not
  yet measured on Vita hardware, so the flag stays off.
- Multi-texmap uploads are limited to the 8 GX texture slots.

## Rules kept

- Pins read-only (`ref/vita-ww-01/RecompCore`, pristine `ddd031b`); no pin or
  dependency change.
- Fixtures are synthetic; no game data, disc content or private capture is committed.
- This scope is `vita/renderer/**`, `vita/feasibility/gx/**`, `scripts/vita/gx/**`
  and this document; the VITA-WW-13 disc work in the same worktree is another
  writer's and is never touched.
