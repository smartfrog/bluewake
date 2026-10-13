# VITA-WW-18: bounded emboss texgens

Continuation after indirect `0c425d0` and real-prefix prerequisite report
`9b367b1`. The read-only reference pin remains RecompCore `ddd031b`.

## Source-backed implementation

Dolphin `Source/Core/VideoCommon/VertexShaderGen.cpp:128–132,924–936` and pinned
`GXRuntime/graphics/gxcore/src/gxcore_shader.cpp:1095–1127` define:

```text
direction = normalize(light.position - view_position)
view_tangent  = normal_matrix * raw_or_cached_tangent
view_binormal = normal_matrix * raw_or_cached_binormal
destination = source_texgen + (dot(direction, view_tangent),
                               dot(direction, view_binormal), 0)
```

The tangent/binormal **must not be normalized**: matrix scale controls emboss
strength. The source's Q is copied without modification. Compute the result in
the existing CPU texgen pass, before Cg interpolation, reusing lighting's view
position/normal-bank resolver and normalization functions rather than duplicating
PNMTXIDX behavior. Both current and per-vertex normal matrices are supported;
omitted tangent/binormal attributes use the pinned cross-draw cached values.

Safety assumptions: source texgens must precede their destination; forward/self
references and out-of-range light/source selectors refuse explicitly. A light
coincident with a vertex gives zero displacement, not a NaN. Non-finite basis
results or light directions also refuse translation. Chained preceding emboss
texgens are supported.

## Important pinned-plan limit

`gxcore.cpp:1613–1629` only fills light constants when `key.lit_valid` is true.
An emboss-only draw with lighting disabled does not carry its actual lights in
the plan, even if the raw XF light registers were written. Testing uncovered
this: using the zero constants displaced the fixture toward the world origin.
The renderer now refuses that form with
`emboss light constants absent from pinned plan`; it does not invent a light or
claim that emboss-alone support is complete. The pin owner needs to supply
lights for emboss consumers before this remaining form can be enabled. No pin
was changed here.

## Evidence

- The byte-split renderer test was red on missing emboss submission before
  implementation. Four current/indexed × single/chained raw-XF/NBT fixtures now
  pass. They verify a 3/4/0 light direction and matrix-scaled displacement
  ±1.2, 2.4 (doubled for a chain), selected light 7 and Q=1.
- Pure translation cases check independent raw/cached tangent and binormal
  supply, scale retention, source Q=2, coincident-light zero offset, invalid
  selectors, non-finite cache and the missing-light-plan refusal.
- Four Linux host suites and ASan/UBSan pass with no diagnostics, including the
  existing lighting tests after shared frame-resolution reuse.
- Synthetic indexed/chained bridge: **804 bytes, 117 events, one usable
  plan/program, zero skips/refusals/gaps, `verdict=ran`**.
- Watched Vita3K run `6d33dbcff1bc4a8fb350043831b235f6`: **171 backend
  submissions, zero compile/link/GL failures**. Cases 168–171 cover the four
  current/indexed × single/chained emboss fixtures; all previous indirect/fog
  cases remain included. Install stopped after staging (0.527 s); run stopped
  on its completed report (70.65 s). CPU/log evidence remains private under
  `build-f6/ww18`. Generated indirect/fog/ZTexture numerical suites also remain
  green: 131,072 / 41,822 / 24,576 cases respectively.

```sh
build-f6/ww18/gx-host/gx_bridge_tests --write-emboss-fixture build-f6/ww18/emboss-fixture
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/bridge.py build-f6/ww18/emboss-fixture/clear.fifo --mem1 build-f6/ww18/emboss-fixture/clear.mem1
```

Actual real draw replay still needs same-run memory for the newly captured
prefix; see `gx_real_replay.md` and `scripts/vita/gx/EXTRACTION.md`. Physical Vita
and visible emboss pixel parity remain unverified. This is a bounded supported
form, not a bypass for the pinned-plan limitation.
