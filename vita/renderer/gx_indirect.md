# VITA-WW-18: indirect texturing

Baseline `130e772`; experimental Vita renderer only. Reference pin remains
RecompCore `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`, read-only.

## Source and decisions

- Dolphin `Source/Core/VideoCommon/BPMemory.h:359–434,704–867` defines the three
  2x3 matrices at BP 06–0E, the effective five-bit exponent, 16 per-TEV controls
  at 10–1F, four indirect orders at 27, scales at 25/26, and raster bump selectors
  5/6. The matrix exponent's SDK high bit is intentionally ignored by hardware.
- `PixelShaderGen.cpp:1142–1362` and pinned `gxcore_shader.cpp:296–493` specify
  signed fixed-point UVs, ABG lookup bytes, 8/5/4/3-bit formats, STU bias, alpha
  masks, normal/S/T matrix modes, wrapping and previous-coordinate accumulation.
- `gxcore.cpp:1033–1042,1106–1124,1858–1907` already captures orders, scales,
  signed matrix rows and SU scale into the DrawPlan; no frontend/pin modification
  is needed. The existing used-texmap mask includes indirect-only texture slots.

Source-backed implementation choices:

1. Quantize post-interpolation ST/STQ coordinates to seven fractional bits using
   **per-texgen SU scale**, truncating toward zero. Sample normalization uses
   **the actual sampled texmap's dimensions**, not SU scale or an assumed slot 0.
2. Prefetch each referenced indirect order once, using arithmetic right shifts
   for its S/T scale. Sample ABG before TEV swaps. Byte quantization keeps the
   renderer's existing `floor(sample*255+0.5)` sampling convention; filtered
   half-byte tie parity with the pinned WGSL `round` is not claimed.
3. Format shifts are 0/3/4/5; selected STU bias is -128 for 8-bit, +1 otherwise.
   Bump selectors S/T/U read raw ABG. Alpha shifts 0/5/4/3 followed by mask 248
   produce the persistent bump value. Raster 5 uses it; raster 6 uses
   `bump | (bump >> 5)`, expressed exactly as addition of disjoint bits.
4. Normal matrices use signed integer dot products then arithmetic shift 3.
   S/T modes multiply both base coordinates by the selected S/T component,
   wrap the product to signed 32 bits, then arithmetic shift 8. Upload matrix
   scaling as the exact power of two `2^-shift` for shifts -14 through 17.
5. Use bounded float arithmetic, not presumed native Cg integer bit operations.
   Split S/T products into 16-bit limbs so integer low bits survive. For left
   shifts, reduce modulo the final 24-bit period before multiplication. Add
   wrapped base/translation/previous in two bounded sums, preserving signed
   24-bit overflow even where a naive float sum would lose a bit.
6. Apply wrap/add-prev at every TEV step, including texture-disabled steps and
   wrap-only controls with zero indirect lookups. An out-of-range lookup follows
   Dolphin's deterministic no-sample fallback while retaining the pinned
   `indirect_ignored` diagnostic, not claiming undefined hardware noise parity.

Wrap modes OFF/256/128/64/32/16/zero are implemented; reserved 7 follows
Dolphin's zero-wrap observation. Bump state persists when its selector is off.
Three matrix slots and all four indirect order slots are supported. Texture
resources must decode successfully; missing resources refuse translation rather
than sampling vitaGL's fallback white texture.

**Still refused:** original/unmodified-coordinate LOD. The reference pin marks
it `indirect_ignored` and does not emit it; this backend also does not upload a
mip chain. Reserved matrix ID 3 and malformed fields/scales are refused. Passing
indirect coordinate coverage does not establish mip/LOD or filtered pixel parity.

## Evidence on Linux and Vita3K

Test-first: Cg and bridge tests were red on active indirect keys, then passed.

- Four host suites pass; also ASan/UBSan with halt-on-error, no diagnostics.
  Raw FIFO coverage spans normal/S/T × four formats, four order slots, texture
  slot 7, three TEV iterations, bump/normalized bump and byte-split submission.
- Translation tests verify three matrix slots, extreme signed coefficients,
  shift endpoints, SU/image-size mismatch and explicit resource/bounds refusals.
- `python3 scripts/vita/gx/indirect_math.py build-f6/ww18` executes actual emitted
  scalar Cg statements against an independent signed integer oracle:
  **131,072 cases across 128 variants**, no excluded cases. Coverage includes
  all formats/bias masks, normal/S/T/OFF matrices, all wraps, add-prev, bump
  selectors/off persistence, normalized bump, all 32 exponents, s32 product
  overflow and s24 boundaries with positive/negative coordinates.
- Synthetic bridge: **521 bytes, 123 events, one usable plan/program,
  no gaps/skips/refusals, `verdict=ran`**.
- Watched Vita3K run `5e9561cabced47bd8b81d0e9d4b9f418`: **167 actual backend
  submissions, zero Cg compile/link/GL failures**. The last 48 cases replay the
  three matrix modes × four formats × four indirect orders, with multiple TEV
  steps and texmap 7. Install stopped after staging at 0.525 s; run stopped on
  its completed report at 71.833 s. CPU/log evidence is retained privately under
  `build-f6/ww18`. This is shader/submission evidence, not pixel readback parity.

```sh
build-f6/ww18/gx-host/gx_bridge_tests --write-indirect-fixture build-f6/ww18/indirect-fixture
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/bridge.py build-f6/ww18/indirect-fixture/clear.fifo --mem1 build-f6/ww18/indirect-fixture/clear.mem1
```

Real-stream readiness is unchanged: the current build-f5 selector finds no
journaled draw yet. `scripts/vita/gx/EXTRACTION.md` now records the exact journal
line/whole-command contract, index/hex requirements, the `--continue-gx` command
limit, adjacent report fields and matching MEM1 expectations. Run that selector
first when the capture lane raises its limit; do not substitute renderer totals
for captured bytes. No host/harness edits or network/push were made.

Physical Vita behavior and real environmental-effect pixels remain unverified.
