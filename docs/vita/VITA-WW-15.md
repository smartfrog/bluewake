# VITA-WW-15 — captured GX bytes into the renderer chain (status)

2026-10-12 · [ticket](https://trello.com/c/7oimCfge) · **delivered on synthetic streams and on the
real (empty) evidence; awaiting the first real GX capture**

This document is the running status of VITA-WW-15. It says what is proven, what is explicitly
**PENDING**, and the exact evidence. It does not claim a rendered scene, a device result, or that
the game has issued its first GX command: no real captured GX bytes exist yet.

## What this ticket does

Bridge the WW05 boot host's GX capture to the WW02 renderer chain, so the day `GX_FRONTIER
complete=1` arrives with real bytes the path to vitaGL already exists: captured bytes → pinned
`RetailGxFrontend` → `GxCoreSink` DrawPlans → `gx_vita` translation → bounded Cg generation, with
every refusal kept.

## Capture surfaces consumed (mapped read-only from `ww05`)

- **Report `GX_FRONTIER` record** — the only persisted capture today
  (`vita/host/main.c`, `vita/host/gx_checkpoint.c`): `complete`, `opcode`, `bytes`, `pc`,
  `completion_pc`, `sp`, `trailing`, `edge_stop`, `hex=` (the first complete GX command, uppercase,
  ≤1025 bytes). The framing table (0x00/0x48→1, 0x08→6, 0x61/0x20/0x28/0x30/0x38→5, 0x10→5+count·4,
  0x40→9) is mirrored in `capture.py`.
- **Gather-pipe batches** — `BwGatherPipeBytes(const u8*, u32)` delivers ≤256-byte batches in guest
  byte order; the bridge accepts the documented log shape `[gather-pipe] bytes=<n> hex=<HEX>` and
  raw binary FIFO dumps (the callback's exact bytes). The host does not emit batch logs yet, so the
  log shape is documented, not observed.
- **CP FIFO writes** arrive inside the stream (`08 reg u32`); there is no separate log.
- **Empty** (`complete=0`, `hex=`) and **partial** (`complete=0`, bytes present: the host stopped
  mid-command) captures are honest states, not errors.

The capture is byte-identical to the VITA-WW-02 fixture format (raw big-endian FIFO), so both sides
meet on one stream with no conversion.

## Delivered

- `scripts/vita/gx/capture.py` — parses report records, gather logs and binary dumps; refuses
  malformed evidence with a `ValueError` (missing record, ambiguous double record, odd/lowercase
  hex, byte-count mismatch, complete without bytes, incomplete with a full frame, opcode mismatch,
  unsupported first opcode, unframed complete command).
- `vita/renderer/gx_bridge.{h,cpp}` — one stream through the pinned frontend, the `gx_vita`
  translation and bounded Cg generation. Counts: decoded trace events (bp/xf/draw/texture/display
  list/other), plans (ok/skipped), programs (ok/refused/distinct), copies (ok/refused), and the
  nonzero gap counters (`missing_vcd`, `vertex_decode_failures`, `unsupported_texgen`,
  `tlut_texture`, `efb_copy_ignored`, `indirect_ignored`, `fog_ignored`, `lighting_ignored`,
  `unresolved_tex_matrix`). A frontend rejection returns its reason, opcode and offset; a skipped
  plan keeps its `skip_reason`; a refused Cg program keeps its `unsupported_reason`.
- `vita/feasibility/gx/gx_bridge_main.cpp` — CLI: `--fixture <scenario>` (02 fixture streams) or
  `--fifo <file> [--mem1 <file>]`. Exit 0 report, 1 frontend rejection, 2 usage/unreadable input.
- `scripts/vita/gx/bridge.py` — one capture file in, combined report out, with verdicts:
  `ran`, `no-bytes-yet`, `partial`, `rejected`. Exit 0 report (including no-bytes-yet and partial),
  1 refusal, 2 usage or missing tooling.
- Tests: `vita/feasibility/gx/test_gx_capture.py`, `vita/feasibility/gx/test_gx_bridge.py`,
  `vita/renderer/gx_bridge_tests.cpp`.

## Proven (what was run, and the result)

Pinned RecompCore `ddd031b2a7f45fcbda37a49daac9b308b8d95da1` (read-only, pristine), built into
`build-f15/ww15/gx-host` with `scripts/vita/gx/build.py --output .../build-f15/ww15`.

- `test_gx_replay.py`, `test_gx_vita.py`, `test_gx_cg.py`, `test_png_capture.py`: green (no 02
  regression).
- `test_gx_capture.py`: 23 tests green, including every real `ww05.report` under `build-f5`.
- `test_gx_bridge.py`: 16 tests green.
- `gx_bridge_tests`: all C++ checks green — empty stream no-op; `textured_triangle` 48 decoded
  events / 1 plan / 1 program; `indirect_triangle` program refused with
  `indirect TEV lookups not supported`; `copy_triangle` 1 copy translated; two shader keys →
  `programs_distinct=2`; a malformed stream rejected as
  `draw vertex layout unavailable (opcode=0x80 offset=0)`.
- **Real evidence, read-only**: all 48 real `ww05.report` files under `build-f5/ww05` run through
  `bridge.py` as `verdict=no-bytes-yet` (exit 0) — every one has `complete=0`, `hex=` empty. No
  gather-pipe log exists in run evidence. `complete=1` appears only in the host's synthetic probe
  logs (`hex=61FEFFFFFF`, a BP 0xFE write), which the tests use as a real-shaped sample.

## PENDING (not proven, honestly open)

- **The first real GX capture.** When a run produces `complete=1` bytes or gather batches, run
  `python3 scripts/vita/gx/bridge.py <report-or-log>`; the report shows commands decoded, plans
  translated, programs generated and gaps flagged. The parser and chain are already validated on
  synthetic streams and on the real empty reports.
- **Guest memory for real captures.** The report carries the first command's bytes, not MEM1;
  addresses inside a real command (display lists, vertex arrays, textures) resolve against a zeroed
  24 MiB MEM1 unless `--mem1 <dump>` is supplied. A command that only sets state (CP/BP/XF) needs
  no memory; a draw would, and the report would say so through the gap counters.
- **An isolated Vita3K demo** on real bytes (established launcher, offscreen) — not requested yet,
  and nothing to render until real bytes exist.
- Nothing was written to the `ww05` worktree; the capture formats were read there only.

## How to run

```sh
python3 scripts/vita/gx/build.py --output <build>          # pinned frontend + bridge
WW02_BUILD=<build> python3 vita/feasibility/gx/test_gx_capture.py
WW02_BUILD=<build> python3 vita/feasibility/gx/test_gx_bridge.py
WW02_BUILD=<build> python3 scripts/vita/gx/bridge.py <ww05.report|gather.log|dump.bin>
WW02_BUILD=<build> <build>/gx-host/gx_bridge --fixture cel_triangle
```
