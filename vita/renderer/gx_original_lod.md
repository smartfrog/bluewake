# VITA-WW-18: original LOD contract limit, not enabled

## Current decision

The complete original-LOD path is **not delivered**. Under the read-only
RecompCore pin and current plan-only translator, required sampler information
is provably unavailable. This is a hard limit of that contract, **not a claim
that Vita hardware cannot implement GX mipmapping**. The refusal now names the
real prerequisites instead of incorrectly blaming all of Vita Cg:

```text
original indirect LOD requires complete sampler state and captured mip levels
```

Even if the contract were extended, the supplied refs do not establish complete
console behavior for bias-clamp or exact derivative/LOD-fraction quantization.
Those modes require additional sourced behavior or hardware measurements, not
defaults guessed from a different GPU. The investigation below is the accepted
documented-limit route; it does not claim a new `verdict=ran` LOD implementation.

### Full sampler register inventory

For texture unit `t`, bank offset is 0 for slots 0–3 and `0x20` for slots 4–7;
the register within a bank is selected by `t % 4`.

| BP register | Fields | Source |
| --- | --- | --- |
| `80–83/A0–A3` MODE0 | wrap S bits 0–1, T 2–3; mag filter 4; mip mode 5–6; min filter 7; edge/diagonal 8; signed S2.5 bias 9–16; anisotropy 19–20; bias-clamp 21 | `BPMemory.h:946–957` |
| `84–87/A4–A7` MODE1 | min LOD bits 0–7, max 8–15, both unsigned 4.4 | `BPMemory.h:982–986` |
| `88–8B/A8–AB` IMAGE0 | width minus 1 bits 0–9, height minus 1 10–19, format 20–23 | `BPMemory.h:1000–1005` |
| `8C–8F/AC–AF` IMAGE1 | even TMEM offset, cache width/height, manual/preloaded mode bit 21 | `BPMemory.h:1022–1029` |
| `90–93/B0–B3` IMAGE2 | odd TMEM offset/cache fields | `BPMemory.h:1049–1056` |
| `94–97/B4–B7` IMAGE3 | texture memory base, in 32-byte units | `GDTexture.cpp:27–32` |
| `98–9B/B8–BB` TLUT | palette TMEM offset and format | `GDTexture.cpp:57–59` |
| `10–1F` INDIRECT | per-TEV original/unmodified-coordinate LOD selection bit 19 | `BPMemory.h:704–723` |

`GDTexture.cpp:4–20` confirms the banks and GX API filter conversion:
`NEAR/LINEAR/NEAR_MIP_NEAR/LIN_MIP_NEAR/NEAR_MIP_LIN/LIN_MIP_LIN` map to hardware
values **0/4/1/5/2/6**, not the API enum value itself. Wrap 3 behaves as clamp
according to the reference's hardware observation (`BPMemory.h:888–893`), not
repeat. That existing Vita mapping discrepancy is not fixed by this report.
That file is `GXRuntime/graphics/aurora/lib/dolphin/gd/GDTexture.cpp` in the pin;
the Dolphin files here are under `Source/Core/VideoCommon` or the named
`Source/Core/VideoBackends/Software` directory.

There is **no separately captured `LOD_FRACTION` register in the pinned BP
schema**. The sampled fraction is computed at rasterization: `lod & 15`, after
LOD bounds (`PixelShaderGen.cpp:670–679`, `TextureSampler.cpp:81`). Mip-linear
mixes the two sampled byte values with weights `(16-frac, frac)` and shifts
right 4; it does not use the existing shader's half-up byte rounding.
Min/mag choice precedes bounds. Dolphin's sampler normalization forces bounds
and bias to zero for mip mode NONE, and caps min to max for inverted bounds
(`RenderState.cpp:290–303`). These are explicitly reference-model choices.

### Executable information-loss proof

`gx_bridge_tests` replays otherwise identical complete synthetic FIFO streams,
changing one MODE0 field at a time: diagonal bit `000100`, +1 LOD bias `004000`,
and bias-clamp `200000`. For all three it observes **identical PlanSampler,
pixel constants and shader key**. This is an actual frontend → sink → DrawPlan
test, not a deduction from header declarations alone. The gradient model
requires edge/diagonal LOD 0 versus 16 for the same footprint, and +1 bias
changes it by 16. A function receiving only that plan cannot distinguish them.

Raw state exists in the frontend, but consulting its final BP registers during
the plan callback is unsafe: `GxCoreSink::submit_packet` keeps a pending draw and
can flush it after later state changes (`gxcore.cpp:2133–2148`). A viable future
renderer-side adapter must pair effective masked BP values and resource versions
with each consumed draw, including display-list state and deferred/fused draws.
That is an extension of the input contract, not a recovery from the current plan.

### Mip resources and API capacities

GX stores independently authored, tile-padded mip levels in guest memory; it
does not specify regeneration from decoded level 0. A native codec test feeds
two chains with **identical 4×4 base images and different 2×2 mip colors**: the
base decodes match, but the lower decodes are uniformly 128 versus 240. Thus
`glGenerateMipmap` cannot restore missing guest mip bytes. A sparse capture must
cover every requested level (and the TLUT for CI); adjacent padding is not data.

`TextureDecoder_Common.cpp:26–130` defines the block sizes used by the new
bounded `scripts/vita/gx/lod_reference.py` layout/slicing checks. RGBA8 4×4,
2×2 and 1×1 each occupy **64 guest bytes**, offsets 0/64/128. Smaller dimensions
do not eliminate tile padding. Manual TMEM/preloaded textures are a separate
even/odd-resource path (`TextureSampler.cpp:129–174`), not a MEM1 byte chain.
The model handles the standard power-of-two chain only, never extrapolates
past 1×1 and refuses missing payload bytes or NPOT input explicitly.

GX IMAGE0 supports dimensions through 1024, giving up to **11 natural levels**
for a standard power-of-two chain. The local Vita3K GXM implementation accepts
dimensions through 4096 and **at most 13 mip levels** (`SceGxm.cpp:1477,5309`);
the VitaSDK exposes mip-count/min/mag/mip-filter setters. Therefore level-count
capacity does **not** establish a GX-on-Vita impossibility.

The present vitaGL path is not an exact GX sampler substitute:
`glTexParameterf(GL_TEXTURE_LOD_BIAS, ...)` casts to an integer before setting
the GXM control word (`textures.c:458–461`, `utils/gxm_utils.h:99–101`), losing
GX's fractional bias. Its exposed texture parameter switch lacks GX
edge/diagonal and fractional min/max controls. A software/manual sampling path
can potentially avoid these limitations; its exact pixel arithmetic is not
established by the previously accepted gradient overload.

External local sources: vitaGL `cd3791e29ff7f1c0ab349f12c7231f4871ce6a75`,
Vita3K `ca2daeae8e949a9bd7eba56b4c2b639afe5b1a0e`. These are read-only inspected
sources, not physical-device specifications or fresh network documentation.

### Reference limits and checks

- `RenderState.h:202` marks LOD clamp as not implemented;
  `RenderState.cpp:320` asks what it does. `PixelShaderGen.cpp` extracts
  `lod_clamp` but does not use it. The numerical model refuses that bit and
  anisotropy rather than inventing behavior.
- The refs themselves disagree about quantization: the software rasterizer's
  approximate mantissa-based `FixedLog2` (`Rasterizer.cpp:124–132`) yields **8**
  for footprint 1.5; the GPU reference's `floor(log2(rho)*16)` yields **9**.
  A regression test records this difference rather than claiming both match.
- **70 Python tests**, including full register decoding, all filter modes,
  signed bias, zero/negative/nonfinite derivatives, pre-clamp min/mag choice,
  bounds, point/linear fractions, tile extents and missing-resource refusal.
- `python3 scripts/vita/gx/lod_reference.py`: **107,008 gradient cases** and
  **1,048,576 mip-byte blends** against analytical expectations. Output labels
  this `reference-model-only`, never renderer or physical-GX fidelity. These
  use Python's numerical model, not emitted Cg or GPU derivative evaluation.
- Four native suites pass normally and under ASan/UBSan. New tests prove the
  three plan-state collisions, independent mip texels, and the precise bridge
  refusal with one skipped plan, zero programs and `indirect_ignored` retained.
- Strict snapshot/extraction replay is still **`ran-with-coverage-gaps`**,
  expected exit 1; evidence is `build-f6/ww18/lod-limit-bridge.log`.
- Vita cross-build passes. Watched Vita3K run
  `2fa89d773f01491fb745f04a60fd0db0` completed the unchanged **171 existing
  backend submissions with zero compile/link/GL failures**. Install stopped on
  staging at 0.527 s; run stopped on its final report at 71.106 s. This is
  regression/submission evidence, not a new original-LOD or pixel test.
- Existing emitted-arithmetic regressions remain green: indirect **131,072**,
  fog **41,822** and ZTexture **24,576** oracle comparisons, with their existing
  qualifications. Repository audit and whitespace checks pass; no pin changed.

The latest inspected real journal (`a4906dc5413141f5b4c1afc249f1382d`)
still has no adjacent `ww05.mem1.bin`/manifest. No real replay was performed
against default RAM or a snapshot from another run. The loader is ready; lane
16's contemporaneous captured resource bytes are still the replay prerequisite.

Remaining delivery requirements: per-draw raw sampler/TMEM provenance, decoded
guest mip uploads, integer-equivalent filtering and LOD fraction routing,
original-coordinate derivative selection, and sources/measurements for the
unsupported clamp/aniso/quantization modes. No production mip renderer or
success verdict was substituted for those requirements.

## Earlier feasibility evidence (unchanged)

The snapshot-loader unit is delivered in `c25ed79`. Original LOD was investigated
while awaiting real same-run memory; **the existing explicit refusal remains**.
The following is earlier source/SDK evidence, not a shipped LOD implementation.

## Reference behavior

Read-only RecompCore pin: `ddd031b`.

- `Source/Core/VideoCommon/BPMemory.h:704–723`: per-TEV BP 10–1F bit 19 selects
  unmodified versus indirect-modified coordinates for LOD computation. Sampling
  still uses the modified coordinates; moving the texture lookup itself back to
  the original coordinates would be wrong.
- `BPMemory.h:946–995`: texture mode 0 includes edge/diagonal LOD, signed S2.5
  bias, mip mode, min/mag filtering, anisotropy and LOD clamp; mode 1 carries
  unsigned 4.4 min/max LOD.
- `PixelShaderGen.cpp:640–679`: use derivatives of 7-fractional-bit texel
  coordinates. Edge mode uses component-wise maxima of absolute X/Y deltas;
  diagonal mode sums them. `rho=max(delta.s,delta.t)/128`; quantize LOD to
  sixteenths using `floor(log2(rho)*16)` plus bias. A zero derivative selects
  min LOD. Select min/mag filtering before min/max clamping; point mip mode
  rounds up when the fraction is at least 8/16.
- Software `TextureSampler.cpp:72–100` confirms point versus linear mip
  selection. The reference GPU's derivative formula itself notes unverified
  hardware details, so compilation is not console-parity evidence.

Nine reference-only numerical spot checks were run and retained in
`build-f6/ww18/lod-reference-checks.log`: edge versus diagonal gives LOD16 0
versus 16 for the same deltas; original versus eight-times-modified footprint
gives 0 versus 48; bias, zero derivatives, clamps and point/linear fractions
also match the source equations. **These are not tests of a renderer
implementation.** They demonstrate why dropping these state fields is unsafe.

## Actual Vita Cg probe

Watched Vita3K run `401f8d2664954586a083e010bd072dac` tested two private shader
variants with distinct program-cache keys and a live final texture result:

1. `tex2D(sampler, modified_uv, ddx(original_uv), ddy(original_uv))` compiled,
   linked and submitted with GL error 0.
2. `tex2Dgrad(sampler, modified_uv, ddx(original_uv), ddy(original_uv))` was
   refused by the fragment compiler. The wrapper returned no extra compiler
   detail; no more precise cause is claimed.

The report has **two attempted variants, one failure** and `demo done 1`. It is
not a green rendering run. Install stopped after staging at 0.526 s; the run
stopped on its completed report at 2.56 s. CPU/log records are retained. Private
candidate source/VPK are `build-f6/ww18/lod-candidate-probe.*`; the standard
171-case probe source was restored after this isolated feasibility check.

## Why the flag is not enabled yet

`gxcore.cpp:1752–1780` and `shader.hpp:436–445` retain min/mag/mip filters,
min/max LOD and anisotropy, but **not edge/diagonal mode, bias or LOD clamp**.
The current Vita texture translation uploads only the decoded base image and
collapses minification to nearest/linear; it does not upload a mip chain or
honor all captured mip/range state. Explicit gradients alone cannot supply
missing levels or recover discarded state.

No production shader or gap counter was changed to disguise this as complete.
A synthetic indirect FIFO with bit 19 set and a validated 128-byte sparse
snapshot was replayed through the strict extractor/bridge:

```text
plans=1 plans_ok=0 plans_skipped=1 programs=0
gap indirect_ignored=1
skip reason=original indirect LOD not supported by Vita Cg
verdict=ran-with-coverage-gaps
```

Evidence: `build-f6/ww18/lod-refusal-proof` and `lod-bridge-refusal.log`.
Next requirements are sufficient captured sampler state, mip decode/upload and
tested LOD/filter selection, followed by original-coordinate gradient routing
using the accepted Cg overload. Same-run real memory, visible mip-selection
pixels and physical Vita behavior remain unverified.
