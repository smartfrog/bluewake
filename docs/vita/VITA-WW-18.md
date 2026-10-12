# VITA-WW-18 — GX draw coverage

**Delivered standard draw-command coverage and common state fixes; full game
rendering readiness remains open.** In particular, Wind Waker's ZTexture clear
quad still needs a depth-output implementation, not a parser bypass.

Branch: `vita-ww-18-drawcov`, baseline `25d20fe`. Linux host checks; no hardware
play claim. Shared `ref/` pins and WW02 fixtures are read-only. Outputs live in
`/tmp/opencode/ww18/build-f6/ww18`. No host or WW05 harness changes.

## Captured inventory

Newest report inspected: WW05 run `6ee5339a723342c79b639e27fad8a8a1`.
It reports 1,759 renderer bytes, 521 events, **zero draws**. Its adjacent
`ww05.log` journals only the first 64 commands: 423 bytes, 27 BP (`61`),
15 CP (`08`), 22 XF (`10`). This prefix is not the whole boot stream.

- BP: `0F,23,24,46,4F,50,51,58,67,69,8C–93,AC–B3`.
- CP: `20,30,40,80–87` (matrix indices and VAT setup, not vertex array payloads).
- XF: `1000,1006,1012,1018,1019,1040–1046,1050–1055` (setup and texgen precursors).
- No primitive, display-list, indexed-XF or actual texture image address is
  journaled in this prefix. BP image1/image2 setup is not proof of a texture draw.

`scripts/vita/gx/inventory.py` counts command boundaries from the journal;
it does not search payload bytes for apparent opcodes. `bridge.py` now accepts
that bounded journal, validates sequence/count/opcode metadata and explicitly
prints `coverage=logged-command-prefix-only`.

Evidence: six Python tests failed before journal support and pass afterward.
Replay of the real prefix: `bridge ok=1 fifo_bytes=423 events=121 plans=0`,
`verdict=ran`. This means frontend execution, not GPU rendering or draw coverage.

## Reference audit

Pinned RecompCore `ddd031b` contains the Dolphin-derived definitions in
`GXRuntime/graphics/gxcore/include/gxruntime/gxcore/shader.hpp`, vertex/layout
decode in `graphics/gxcore/src/gxcore.cpp`, command parsing in
`graphics/frontend/src/retail_gx_frontend.cpp`, and primitive expansion in
`graphics/frontend/src/render_sink.cpp`.

Draw-phase supply is state-dependent: CP `50/60` VCD, `70–77/80–87/90–97` VAT,
`A0–AF/B0–BF` array bases/strides, XF matrices/projection/texgens, then primitive
commands with low three bits selecting VAT. BP carries TEX, TEV, indirect and
PE/Z configuration; these are register groups, not separate FIFO opcodes.

The full Dolphin table is in pinned `Source/Core/VideoCommon/OpcodeDecoding.h`.
It also names nonstandard quads `88–8F` and metrics `44`, which the pinned retail
frontend does not support. They are not observed in the captures, and the SDK's
ordinary primitive enums use the seven tested families. Neither is silently
rewritten here. GX SDK draw examples are in pinned
`GXRuntime/graphics/aurora/lib/dolphin/gx/GXDraw.cpp`. The expected draw-phase
set is inferred from these definitions and GX/J3D requirements, **not claimed
to have appeared in the zero-draw boot capture**.

## Coverage matrix

| Command/group | Before | After / evidence |
| --- | --- | --- |
| Journal hex records | Bridge ignored them | Six parser tests; real 423-byte prefix ran |
| `08`, `10`, `61` setup | Parsed | Real prefix: 15 CP / 22 XF / 27 BP |
| `80`, `90`, `98`, `A0`, `A8`, `B0`, `B8` + VAT | Parsed/expanded, incompletely exercised here | 56 recording-backend cases check exact indices/winding/topology and byte-at-a-time streaming |
| CP vertex arrays, indexed XF `20/28/30/38` | Pinned resolver/layout support | Index8/index16 positions; direct/index8/index16 S8 normals, RGBA8 colors and S16 fixed-point texcoords; all four XF arrays load translated matrices |
| `40` display list, `48` invalidate, `00` padding | Pinned support | Aligned display-list quad reaches the recording backend with exact indices |
| PNMTXIDX | Decoded but backend ignored it | Translation applies each vertex's selected XF rows; mixed-matrix renderer test and out-of-range refusal test |
| TEV `C0–DF`, colors `E0–E7`, KONST `F6–FD` | Bounded subset, common args refused; scales 2/4 inverted | All 16 color / 8 alpha arguments, signed color and KONST uniforms, separate destinations/scales/bias/clamp, compare modes and swaps generated; source-contract and raw-BP supply tests |
| TEX images/modes/TLUT under `61` | Pinned codecs; single nonzero texmap bound to slot 0 | Single slot-3 stream now routes through the per-slot upload; RGBA8/CI8 and two-texmap regressions pass |
| Blend `41`, Z `40`, PE `43`, alpha `F3` | Basic GL state; destination-color factor mistranslated; alpha test omitted | Destination factors use source color; eight alpha comparisons / four logic ops generated; raw refs 64/192 reach backend |
| XF regular texgen source rows | Only own-UV path; UV5 could generate undeclared inputs | Cross-source UV0–4 works; UV5–7 explicitly refused, tested |
| Indirect BP `06–1F/25/27` | Active matrix/bump lookups refused | Still refused; existing fixture/renderer refusal tests |
| Fog `E8–F2`, ZTexture `F4/F5`, destination alpha `42` | Shader omitted these despite parsed keys | Explicit Cg refusal tests; raw ZTexture draw reports the reason without frontend failure |
| Nonstandard quads `88–8F`, metrics `44` | Not supported by pinned retail frontend | Unchanged; no captured demand; needs upstream pin work if encountered |

## Checks

Baseline: `gx_vita_tests`, `gx_cg_tests`, `gx_bridge_tests`, `ww_renderer_tests`
all pass. Build: `WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/build.py`.
Capture tests: `python3 -m unittest discover -s scripts/vita/gx -p 'test_*.py'`.
Real replay: `WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/bridge.py <ww05.log>`.

Primitive/supply unit: expanded `ww_renderer_tests` passes. These are
characterization tests of existing pinned support, not a claimed missing-feature
red/green cycle. Fixture corrections were necessary: CP bases are physical;
display-list lengths must be 32-byte aligned. No pinned parser change was needed.

PNMTXIDX unit: renderer test failed on the second vertex's position (`1` instead
of `12`) before implementation; translation bounds test also failed. Both pass
afterward. Translation transforms indexed positions on the CPU and gives Cg an
identity position matrix, preserving projection and avoiding unsigned attribute
binding on vitaGL. Normals/lighting/texture-matrix selection are separate gaps,
not implied by position support. The bridge now actually calls `translate`,
which it previously advertised but did not execute. Real journal still ran:
121 events, zero plans, `verdict=ran`.

TEV unit: argument coverage failed on C0 before implementation; all argument,
12 bias/scale, eight compare-mode and swap/KONST source-contract cases pass now.
A raw two-stage BP stream reaches the recording backend with signed C0 and
KONST values preserved despite their aliased BP addresses. `gx_tev.h` follows
the pinned generator's integer rounding, input wrapping and destination ordering
using float values with explicit floor. Scale2/Scale4 are multiplication, not
the old `0.5/0.25`; Divide2 and separate alpha scale are admitted. These tests
verify generation and uniform supply, **not GPU pixel parity or Cg compilation**.
The later Vita3K probe below additionally verifies compilation/submission for
the exercised keys.

Alpha/blend/TEX unit: tests failed on missing alpha uniforms, wrong destination
blend token and wrong slot-3 binding before changes. All pass afterward, as do
all four renderer/bridge suites. Alpha testing uses the TEV result before output
normalization, with reference values from BP F3. The Vita C++ renderer and demo
VPK cross-build successfully with the local VitaSDK; this alone does not prove
that a generated Cg shader compiles on libshacccg.

Boundary unit: UV5–7 and unsupported fog/ZTexture/destination-alpha tests failed
before refusal handling; cross-source UV0 support and previously hidden logic-op
gap reporting are now tested. Raw ZTexture commands parse but increment
`programs_refused`, with **zero submitted draws**. No fake success or shader
fallback is introduced.

## Watched Vita3K evidence

- Standard demo run `8600693d72b948cdb9bcd411b7b63a35`: passthrough shaders and
  draw/copy submission ran; black readback blocked three pixel checks. This was
  **not** new TEV compile evidence. The watched install GUI reached its 45-second
  ceiling; the subsequent demo was stopped immediately on its completed report.
- Dedicated synthetic probe run `046a3db1b3a9473d996c4d7b8990eb4e`: **46 actual
  backend submissions, zero failures**. It exercised all color/alpha arguments,
  12 bias/scale combinations, eight compare modes, KONST with alpha testing,
  and the two-texture cel key. The backend compiled/linked generated Cg through
  libshacccg and submitted each draw with `glGetError()==0`.
- Probe install stopped after all VPK files were staged (0.523 seconds); draw
  run stopped on the completed report (8.848 seconds). CPU/log records and the
  report are under `build-f6/ww18/vita3k-run-046a3db1b3a9473d996c4d7b8990eb4e/evidence/`.
  The private probe source and CMake file are in `build-f6/ww18/probe-source/`;
  its watcher is `build-f6/ww18/watch_demo.py`. No game data is in the probe.
- This proves exercised shader compilation and submission, **not pixel parity,
  lighting correctness, game frames, or physical Vita behavior**.

## Remaining work, in draw-frontier order

1. **ZTexture `F4/F5`**: pinned `gxcore.cpp` explicitly identifies Wind Waker's
   JFWDisplay REPLACE/U24 clear quad. Its depth output is still missing in Vita
   Cg. This is likely an early draw-phase requirement and a real readiness blocker.
2. **XF lighting and texture transforms**: normals/NBT decode, but Cg still
   passes raster colors through rather than implementing XF lighting/materials.
   Static/per-vertex texture matrices, position/normal-source texgens, STQ and
   post matrices are not implemented. Raw UV5–7 need a Vita attribute layout.
3. **Indirect/fog/PE**: indirect lookup/wrap/add-prev, fog, exact early-Z versus
   alpha-test ordering, logic operations and dual-source destination alpha remain
   open. The reference's support is not the Vita backend's support.
4. **Submission fidelity**: GX viewport/scissor, mip/LOD behavior, and full EFB
   copy/clear/depth/feedback semantics are not proved by these draw-command tests.
5. **Actual captured draw demand**: the journal stops at 64 commands. New full
   FIFO evidence plus the contemporaneous guest memory snapshot is needed for
   display lists, indexed arrays and texture images. The shared host/harness
   logging limits were not changed in this lane.

## Units on this branch

| Commit | Unit |
| --- | --- |
| `2a8c682` | Real journal inventory/replay |
| `353651f` | Primitive/VAT/indexed-XF/display-list characterization |
| `9b549df` | PNMTXIDX position translation and actual bridge translation |
| `9b1281a` | Common TEV arguments/constants/combiners |
| `e4fc310` | Alpha tests, destination blend factors, nonzero texture routing |

Repository audit and attribution check (`25d20fe..HEAD`) pass. No network,
push, shared-pin edits, host edits, or WW05 harness edits were made.

Final host verification also passes all four suites with AddressSanitizer and
UndefinedBehaviorSanitizer enabled, with no diagnostics in
`build-f6/ww18/sanitize-tests.log`. Configure a separate output with
`-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`, the same
`CMAKE_CXX_FLAGS`, and `-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'`.
The real prefix still reports `verdict=ran`, 121 events and zero draws after
every implementation unit. Both shared pin checkouts remain clean.

Rendering changes remain in the experimental Vita path; do not enable a
production default on the strength of host tests. Windows CI and Vita hardware
are not available in this session.
