# VITA-WW-18: original LOD feasibility, not enabled

The snapshot-loader unit is delivered in `c25ed79`. Original LOD was investigated
while awaiting real same-run memory; **the existing explicit refusal remains**.
This report is source/SDK evidence, not a shipped LOD implementation.

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
