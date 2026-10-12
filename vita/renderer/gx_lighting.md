# VITA-WW-18: vertex lighting

Continuation after texture matrices `67299ee`. Source pin: RecompCore `ddd031b`.
The behavior comes from `GXRuntime/graphics/gxcore/src/gxcore_shader.cpp:597–735`
(Dolphin LightingShaderGen port), including the existing `channel_lit_path`
predicate. `gxcore.cpp:1560–1635` defines current/per-vertex normal matrices,
cached normals, the 16-word XF light layout and material/ambient constants.

Source-backed decision: calculate vertex lighting in the CPU translation before
texture generation. Uploaded colors therefore feed both TEV raster arguments
and Color0/Color1 toon coordinates. Cg interpolates those already-lit values.
No extra GPU light-uniform arrays or normal attributes are required. This costs
CPU work; no performance improvement or production-default claim is made.

Implemented:

- The pinned capture gate: unconfigured channels remain vertex passthrough;
  configured register-material channels work even with lighting disabled.
- Independent RGB/alpha material and ambient sources; color channels 0/1.
- Eight-light masks, None/Sign/Clamp diffuse and None/Directional/Spec/Spot
  attenuation. Spec uses the pinned normalized distance coefficients when
  diffuse is enabled.
- Current or per-vertex normal-matrix bank selection, normalized normals and
  the plan's cached-normal fallback when the vertex format omits normals.
- Per-light round-to-nearest-even, integer accumulation/clamp and the pinned
  material multiplier `(mat * (acc + (acc >> 7))) >> 8`, then division by 255.

Robustness choice: zero-length direction uses the normal fallback rather than
NaN, matching the pinned None/Directional intent and extending it to degenerate
Spec/Spot inputs. Zero attenuation denominators, non-finite contributions,
invalid channel configuration and out-of-range normal rows refuse translation
with a reason. Unlit material-only paths do not inspect irrelevant matrices.
Missing captured XF lights are now exposed in bridge/renderer diagnostics.

Test-first evidence: register-material output failed before implementation
(`1.0` instead of `100/255`). Host cases then verify 12 attenuation/diffuse
combinations, signed versus clamped diffuse, separate alpha material/ambient,
cached normals, a per-vertex flipped normal matrix, light 7 selection,
uncaptured-channel passthrough, channel 1/Color1 coordinates, coincident-light
fallback, invalid attenuation and normal-bank bounds.

A raw FIFO fixture loads actual XF normal/light/material/channel registers and
uses a Color0 texgen. Recording-backend tests for lights 0 and 7 check lit RGB
`63/255, 128/255, 192/255`, alpha 1 and toon S/T `63/255, 128/255`. The 16-word
light layout is derived from the source, not from the older padded demo fixture.

Synthetic bridge command:

```sh
build-f6/ww18/gx-host/gx_bridge_tests --write-light-fixture build-f6/ww18/light-fixture
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/bridge.py build-f6/ww18/light-fixture/clear.fifo --mem1 build-f6/ww18/light-fixture/clear.mem1
```

Result: 670 bytes, 120 events, one usable plan/program, no skips/refusals,
`verdict=ran`. Four Linux host suites and ASan/UBSan pass, and the 24,576
ZTexture arithmetic cases remain green.

Watched Vita3K run `df1886836fd744e0b5531ca096d34edf`: 59 backend submissions,
zero compile/link/GL failures, including the raw XF light-0 and light-7 fixtures
as cases 58/59. Install stopped after staging (0.532 s); run stopped on its
completed report (12.588 s). This checks uploaded lit colors and toon-coordinate
submission, not visible pixel parity. Evidence is under `build-f6/ww18`.

Real captured draw frames and physical Vita pixel parity remain unverified.
Emboss, normalized/per-vertex post-transform forms not carried by the pinned
plan, indirect effects, fog and exact early-Z/dual-source behavior remain outside
these three units.
