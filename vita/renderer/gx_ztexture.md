# VITA-WW-18: late ZTexture depth output

Continuation baseline: `f734af1`. Scope: the existing experimental Vita renderer;
no host, shared harness or reference-pin changes.

## Sourced decision

The read-only RecompCore pin is `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`.

- `Source/Core/VideoCommon/BPMemory.h`: BP F4 is the 24-bit bias. F5 type
  0/1/2 is U8/U16/U24 and operation 0/1/2 is DISABLED/ADD/REPLACE.
- `PixelShaderManager.cpp:346–366`: U8 takes A, U16 takes R + 256*A,
  U24 takes 65536*R + 256*G + B. These are sampled bytes, not TEV's color result.
- `PixelShaderGen.cpp:1074–1090` and `1386–1388`: the last texture sample before
  TEV swapping supplies Z; bias and optional raster Z are added, then masked
  to 24 bits. Explicit depth output is a late-Z operation.
- `GXRuntime/graphics/gxcore/src/gxcore.cpp:1227–1245` gates the captured key to
  valid, late, depth-testing/depth-writing draws. It identifies Wind Waker's
  JFWDisplay REPLACE/U24 clear quad. The existing public fixture is
  `tests/gxcore_ztexture_pixels_test.cpp`.
- `graphics/gxcore/src/gxcore_shader.cpp:949–951`: GX clip Z is [-w,0]. Its
  WebGPU backend flips Z and reverses comparisons; that convention must not be
  copied in isolation into Vita.
- Local vitaGL `source/tests.c:394–428` maps GL LESS directly to GXM LESS;
  `source/misc.c:351–364` supplies ordinary GL depth range. Its
  `source/utils/glsl_utils.c:1325–1355` maps fragment depth to Cg DEPTH and
  fragment position to WPOS.

**Assumption adopted:** retain the renderer's ordinary, non-reversed LESS
comparison. Therefore Cg writes `z24 / 16777216`, and vertex clip Z is remapped
as `2*z + w` before GL's [-w,+w] viewport transform. XF farZ/zRange determine
the GL near/far range, so ADD uses the already viewport-mapped WPOS.z. This
keeps clear depth 0xFFFFFF at the far plane and subsequent nearer LESS draws
eligible. It does not mix Dolphin's reversed normalization with an unreversed
comparison.

Cg float ALU cannot exactly hold every sum above 2^24. The shader adds exact
high-16/low-8 parts, carries low-byte overflow, wraps high bits, then normalizes.
BP F4 is uploaded in the same two-part representation. Alpha discard happens
before late depth output. DISABLED keys do not gain a DEPTH output.

## Evidence

Three focused tests failed before implementation: Cg refused all supported
types/operations, the bridge generated no program for the synthetic clear quad,
and the renderer submitted no ZTexture draw. They pass afterward.

- Four host suites pass, also under AddressSanitizer/UndefinedBehaviorSanitizer.
- `ztexture_math.py` executes the actual generated scalar depth statements as
  C++ against a separate integer oracle: 24,576 cases, including full-scale
  carry/wrap. All three formats and ADD/REPLACE match.
- Tests cover the pre-swap last sample, a later disabled TEV stage, alpha-test
  ordering, exact bias packing, XF depth range and the clear quad's ALWAYS test
  plus enabled depth write.
- No real draw capture exists in the three newest available WW05 reports:
  each still reports zero plans/draws. The bridge evidence is explicitly synthetic.
- Watched Vita3K run `406920ff071f4241ba17940b66774bb9`: 52 real backend
  submissions, zero Cg compile/link/submission failures. Cases 47–52 replay all
  six ZTexture format/operation variants through the pinned frontend before
  submitting the generated DEPTH shaders. Install stopped when staged (0.524 s),
  run stopped on its completed report (9.605 s), with CPU/log records preserved.
- Earlier run `4b594cdc8f9f434a976e398889176397` failed in the private probe's
  large stack-allocated frontend before ZTexture testing, producing memory
  faults and reaching the watched ceiling. The probe now allocates it on the
  heap like the production renderer, and the watcher aborts on memory faults.
  That failed run is not counted as rendering evidence.
- The new zero-vertex constant-packing test exposed a size-zero `memcpy` with
  null pointers. Translation now avoids it; sanitizer rerun with halt-on-error
  passes without diagnostics.

Reproduce on the Linux host:

```sh
cmake --build build-f6/ww18/gx-host
build-f6/ww18/gx-host/gx_bridge_tests --write-ztexture-fixture build-f6/ww18/ztexture-fixture
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/bridge.py build-f6/ww18/ztexture-fixture/clear.fifo --mem1 build-f6/ww18/ztexture-fixture/clear.mem1
python3 scripts/vita/gx/ztexture_math.py build-f6/ww18
```

Bridge result: 426 FIFO bytes, 83 events, one quad, one usable plan, one generated
program, zero skips/refusals, `verdict=ran`. This is not a claim of a real game
frame or physical Vita pixel parity. Early-Z/fog-only invalid forms remain the
pinned core's reported gaps; indirect and destination-alpha support is unchanged.
