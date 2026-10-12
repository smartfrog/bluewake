# VITA-WW-18 — GX draw coverage

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

## Coverage matrix

| Command/group | Before | After / evidence |
| --- | --- | --- |
| Journal hex records | Bridge ignored them | Six parser tests; real 423-byte prefix ran |
| `08`, `10`, `61` setup | Parsed | Real prefix: 15 CP / 22 XF / 27 BP |
| `80`, `90`, `98`, `A0`, `A8`, `B0`, `B8` + VAT | Parsed/expanded, incompletely exercised here | 56 recording-backend cases check exact indices/winding/topology and byte-at-a-time streaming |
| CP vertex arrays, indexed XF `20/28/30/38` | Pinned resolver/layout support | Index8/index16 position values; all four XF arrays load translated matrices |
| `40` display list, `48` invalidate, `00` padding | Pinned support | Aligned display-list quad reaches the recording backend with exact indices |
| PNMTXIDX | Decoded but backend ignored it | Translation applies each vertex's selected XF rows; mixed-matrix renderer test and out-of-range refusal test |
| TEV `C0–DF`, colors `E0–E7`, KONST `F6–FD` | Bounded subset, common args refused | Pending |
| Blend `41`, Z `40`, PE `43`, alpha `F3` | Basic GL state; alpha test omitted | Pending |
| Indirect BP `06–1F/25/27`, fog `E8–F2`, ZTexture `F4/F5` | Cg missing/refused | Not proven |

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

Rendering changes remain in the experimental Vita path; do not enable a
production default on the strength of host tests. Windows CI and Vita hardware
are not available in this session.
