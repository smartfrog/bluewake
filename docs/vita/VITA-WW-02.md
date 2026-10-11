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
    SUBTRACT blending, BP 0x42 constant destination alpha.
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

## PENDING (explicit, not silently dropped)

| Area | State | Note |
| --- | --- | --- |
| Water / indirect stages | **PENDING** | `ShaderKey` carries indirect stages; no fixture or translation yet. Gaps surface through `GapCounters` (`indirect_ignored`). |
| HUD (2D draws) | **PENDING** | Same direct-draw path as the fixtures; no dedicated fixture yet. |
| EFB copy → texture | **PENDING** | `GxCoreSink` exposes `CopyObserver`/`EfbCopyCommand`; no fixture, no translation into a copy command yet. |
| EFB readback to guest memory | **PENDING** | Required by the acceptance list; not implemented. |
| Cg program generation | **PENDING** | Only the program key is produced; no Cg source generation from `ShaderKey` yet. |
| Vita3K / device run | **PENDING** | No Vita3K run of this path yet; any run must use the isolated private-profile launcher pattern. |
| Physical console | **PENDING** | No hardware measurement; emulator timings never extrapolate to hardware. |

## Known limits

- The translation covers the states the fixtures exercise. Anything else is a gap that
  must be counted and reported, never ignored.
- `front_face` is GL_CCW (the GL default); the GX winding mapping is **to be
  established** against hardware.
- vitaGL has no blend colour, so BP 0x42 constant destination alpha is a shader
  contract (`use_dst_alpha`/`dst_alpha` carried to the Cg generator), not GL state.
- Multi-texmap uploads are limited to the 8 GX texture slots.

## Rules kept

- Pins read-only (`ref/vita-ww-01/RecompCore`, pristine `ddd031b`); no pin or
  dependency change.
- Fixtures are synthetic; no game data, disc content or private capture is committed.
- This scope is `vita/renderer/**`, `vita/feasibility/gx/**`, `scripts/vita/gx/**`
  and this document; the VITA-WW-13 disc work in the same worktree is another
  writer's and is never touched.
