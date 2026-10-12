# VITA-WW-18: GX fog and real replay readiness

Continuation baseline `2310041`; extraction unit `047d292`. The experimental
Vita renderer now generates fog rather than refusing it. No host/harness or
reference-pin changes; Linux host and Vita3K evidence only.

## Sourced behavior

Read-only RecompCore pin: `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`.

- `Source/Core/VideoCommon/BPMemory.h:1420–1568`: BP EE is A, EF is the 24-bit
  B magnitude, F0 supplies the B shift, F1 is C/projection/function, F2 is RGB.
  A/C are sign + 8 exponent + 11 mantissa bits, re-expanded to IEEE float.
  E8 is enable/center (+342), and E9–ED contain ten 12-bit range K entries.
- `GXRuntime/graphics/aurora/lib/dolphin/gx/GXPixel.cpp:7–110`: SDK-compatible
  register packing, including F0's five-bit shift. The pinned core already
  decodes these registers into DrawPlan pixel constants (`gxcore.cpp:1910–1968`).
- `Source/Core/VideoCommon/PixelShaderGen.cpp:1670–1736`: perspective
  `ze = A*2^24 / (B - (z24 >> shift))`, orthographic `ze = A*z24/2^24`,
  range correction, clamp `ze-C`, fog curve, then integer RGB blend.
- `gxcore_shader.cpp:219–269` defines the pinned WGSL arithmetic, including
  nearest-even fog weight rounding. Dolphin's software path uses 2^24-1 and
  truncated weights instead; this unit follows the pinned GPU convention,
  not a claimed hardware certification of that difference.

For `t = clamp(ze-C,0,1)`:

| F1 function | Curve |
| --- | --- |
| 0 OFF | no fog statements |
| 2 LIN | t |
| 4 EXP | 1 - 2^(-8t) |
| 5 EXP2 | 1 - 2^(-8t²) |
| 6 reverse EXP | 2^(-8(1-t)) |
| 7 reverse EXP2 | 2^(-8(1-t)²) |

There is no GX fog function named ADD. ADD/REPLACE are ZTexture operations.
Dolphin `PixelShaderGen.cpp:1071–1102` and software `Tev.cpp:570–660` use the
resulting ZTexture depth for fog. The pinned WGSL currently fogs before computing
ZTexture; this Vita implementation instead follows Dolphin when the captured
key exposes a late depth output. Alpha discard remains before both operations.
Early/fog-only ZTexture forms missing from the pinned key remain reported gaps.

The blend weight (sometimes called fog alpha) is `roundEven(curve*256)` in
0–256. Each RGB channel becomes
`floor((tevRGB*(256-weight) + fogRGB*weight)/256)`. **TEV alpha is unchanged**:
GX has no independent fog-alpha color/table. The ten-entry K table corrects
distance by `sqrt(offset²+K²)/K`; it does not replace output alpha.

## Vita-specific decisions

- Keep ordinary non-reversed GL depth from the prior ZTexture unit:
  `z24 = floor(clamp(WPOS.z*2^24,0,0xFFFFFF))`, or the generated ZTexture depth.
- B and its power-of-two shift divisor are uploaded as exact floats; dividing
  and flooring implements the integer shift without Cg integer bit operations.
- Preserve Q/TEV behavior; fog only changes final RGB after the alpha test.
- Upload fog coefficients/color/table every draw, independently of shader cache.
- Correct the pin's range width `4*XF.wd` to Dolphin's `2*XF.wd`, adjusting its
  normalized center to retain the same native center. Sources:
  `PixelShaderManager.cpp:94–107` and `gxcore.cpp:1945–1949`.
- Convert WPOS.x to the native GX viewport's left/width before the K lookup.
  The current backend stretches clip space over 960 pixels; it must not treat
  those 960 screen pixels as 640 native pixels. This assumes the backend's
  existing full-frame mapping, not a new viewport/scissor implementation.
- Statically address ten K entries and interpolate the adjacent interval;
  index 9 uses entry 9 directly, never a spurious eleventh entry. This avoids
  dynamic uniform indexing and the pin's center-endpoint padding read.

Degenerate-input assumptions are explicit: A=0 yields ze=0 at a singular B
denominator; nonzero A uses the signed saturated limit. The pin's special
A=0/C=±infinity case becomes finite C=±1 with identical clamping. Other
non-finite coefficients, zero/non-finite K, invalid viewports and reserved fog
modes refuse translation/generation with a reason instead of producing NaN.

## Verification

Three focused tests were red before implementation: fog Cg refusal, bridge
program refusal and no renderer submission. After implementation:

- Four host suites pass normally and under ASan/UBSan with halt-on-error and no
  diagnostics. Raw FIFO tests cover five curves, both projections and range
  on/off; byte-split renderer tests verify coefficient/color/table packing,
  corrected center/width and native viewport mapping.
- `python3 scripts/vita/gx/fog_math.py build-f6/ww18` runs actual emitted scalar
  Cg statements as float32 C++ against an independent integer/double oracle:
  **41,822 cases across 40 variants**, including all 257 weights and 256 half
  steps, signed RGB blending, preserved alpha, table endpoints/interpolation,
  singular denominators and raster/ZTexture depth selection. 164 randomized
  cases within 0.002 weight units of a half-step are excluded from the
  double-vs-float comparison; exact half-step fixtures remain tested.
- Prior ZTexture generated arithmetic still passes all 24,576 cases.
- Synthetic fog bridge: 510 bytes, 107 events, one usable plan/program,
  no skips/refusals/gaps, **`verdict=ran`**.
- Watched Vita3K run `4ee3fa6028cb47688650b65fe06c4718`: **119 backend
  submissions, zero Cg compile/link/GL failures**, including 60 fog variants
  (five curves × two projections × range on/off × ZTexture disabled/ADD/REPLACE).
  Install stopped after staging at 0.526 s; run stopped on completed report at
  33.659 s. CPU/log evidence is private under `build-f6/ww18`.

Reproduce the bridge:

```sh
build-f6/ww18/gx-host/gx_bridge_tests --write-fog-fixture build-f6/ww18/fog-fixture
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/bridge.py build-f6/ww18/fog-fixture/clear.fifo --mem1 build-f6/ww18/fog-fixture/clear.mem1
```

## Real stream status

The extraction/replay tool is ready: [EXTRACTION.md](../../scripts/vita/gx/EXTRACTION.md).
Newest checked real run `555a9cabdde3450e8fcaef8f764612ef` reports one draw,
1,171 decoded commands and 8,256 renderer FIFO bytes. Its journal still contains
only 64 commands/423 bytes and **zero draws**. It stops at draw-done requiring
captured FIFO consumption. The current selector reports
`verdict=no-captured-draw-sequence` for a journal with at least two draws.
There is no full draw FIFO plus matching resource snapshot to replay yet.
No missing bytes/resources were fabricated, and the host lane remains untouched.

Physical Vita behavior, actual marine-fog pixels and pixel parity remain
unverified. Indirect, emboss, missing post-transform/early-Z forms and exact
EFB/viewport behavior are not closed by passing these synthetic submissions.
