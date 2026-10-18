# GX title render: black phase counters and the title-scene stop

2026-10-18 · branch `task/gx-title-render`, base
`30a192e1043e63ad0a5f79c7b43d50359485f246`, source
`4552e2b39f2349e13dbe5d38284f1de0dfca834f` (two measurement commits on
`30a192e`). One normal boot under the 5,905,580,032-byte floor, swap 0 and
OOM stop. Private package. **No fix is claimed; the title/menu was not reached.**

## Verdict

**FAIL, exact stage:** the run reached the black phase and kept presenting, but
the title scene was still being initialized when the TlogoE control-flow fault
ended it. The black phase is not a `draws=0` (not-submitted) case: every one of
the 1,489 black-phase frames submitted at least one draw, with zero skipped
plans and zero refused programs.

The title scene starts at the end of the run:

- `d_a_title` REL module 400 is materialized at log line 143652.
- `/res/Object/TlogoE.arc` entry 1288 is opened at line 143666.
- Frames 1490-1494 add two new program keys (4 → 6 distinct) and the first six
  EFB copies (0 → 6).
- The process then exits 133 with `Unhandled SIGSEGV` at host PC
  `0x0000aaab78034664`, `Unhandled access to 0x49C9ACF78`. `ww05.partial` stays
  0 bytes; there is no `ww05.report`.

This is the same corrupted-control-flow/Thumb signature the perf track sees at
TlogoE. The black frames precede it; they are not caused by it. The run never
reaches a title/menu frame, so no renderer fix can be claimed.

## Counters at the end of the black phase

At `[frame-present] frame=1489` (the last black-phase frame, before the title
scene loads):

| counter | value |
| --- | ---: |
| `draws` | **1,637** |
| `plans` / `plans_skipped` | 1,637 / **0** |
| `programs` / `programs_distinct` | 1,637 / **4** |
| `programs_refused` | **0** |
| `copies` / `copies_refused` | **0** / 0 |
| `display_copies` | **1,489** |
| `presented` | **1,489** |
| `GX_STREAM decoded` | **2,763,887** |
| `GX_STREAM pending` | 5 |
| `GX_STREAM read_bytes` | 17,275,904 |
| `GX_STREAM logged / limit` | 4,096 / 4,096 |
| renderer gaps | 1 (`tlut_texture=60`) |

At the final frame 1494: `draws=1,648`, `plans_skipped=0`,
`programs_refused=0`, `programs_distinct=6`, `copies=6`, `display_copies=1,494`.

Draw deltas over frames 1-1,489: **zero-draw frames = 0**, minimum **1**,
maximum **3**, mean **1.10** per frame. Frames 129-1,489 are a steady **one
draw per frame**; the four distinct programs are the logo/fade path. The
`tlut_texture=60` gap is the pinned counter for valid CI-format binds, not a
decode or palette failure signal.

`GX_FRONTIER`/`RENDER` report lines are unavailable because the process died
before publishing `ww05.report`; the per-frame `[renderer-stats]` lines carry
the same counters and are the evidence used above.

## The 14.11 s gap is not a 14 s GL call

The run has a **13.33 s** gap after frame 8 (the evidence run reported 14.11 s).
Inside it the guest log is active, not empty:

- **53,443,831 guest cycles** advanced (81,065,894 → 134,509,725).
- **806** `[progress]` lines, **2,988** `[exception-vector-dispatch]` entries,
  806 edge progress lines, 22 DVD completions and 2 `[gx-gpu-finish]` lines.
- The slowest instrumented renderer call in the whole run is the **894 ms
  program compile + draw at the title-scene transition (frame 1490)**, not at
  frame 8. At frame 8 the new program is a **program-cache hit**: 1.9 ms load +
  7.3 ms restore. Other slow calls are `finish` up to 184.7 ms.
- No `program-compile`, `draw`, `texture-upload`, `finish` or `present` call
  accounts for the 13.3 s.

The gap is therefore guest CPU work (the `ww-fixture` thread accumulates 15,276
sampled 10 ms ticks; the emulator threads 2,635 + 274), dominated by
exception-vector handling. **The "host-side shader/GL stall" hypothesis is not
supported by this log.** The one large compile is a real 894 ms title-scene
hitch, but it happens after frame 1,489, not after frame 8.

## Steady state: the 32-50 ms is after present, not in the renderer

Frame intervals, excluding the long load gaps:

| frames | median | p95 |
| --- | ---: | ---: |
| 1-128 | 58.13 ms | 110.6 ms |
| 129-500 | 58.63 ms | 84.6 ms |
| 501-1,000 | 55.41 ms | 59.2 ms |
| 1,001-1,489 | 54.90 ms | 57.0 ms |

Median `retrace → present` is **4.49 ms** (p95 11.24 ms); median
`present → next retrace` is **50.23 ms** (p95 70.71 ms). The renderer completes
its submission in about 4.5 ms; the 50 ms wall sits between the present and the
next retrace, on the guest CPU thread. That is the same shape the perf track
reported, but the counters here show no renderer work in that window.

## What this does and does not establish

- It establishes that the black phase has **draws every frame**, no skipped
  plans, no refused programs and no EFB copies. It is not a display-list
  submission failure at the counters.
- It does not establish that those draws are visible or correct: no framebuffer
  image was captured in this run. The title scene itself is not reached.
- It does not establish any renderer defect. The next blocker is the
  `d_a_title`/`TlogoE.arc` control-flow fault at frames 1489-1494, which must
  be fixed or bypassed before a title/menu frame can be judged.
- It does not support a 14 s shader/GL stall at frame 8. The only large compile
  measured is the 894 ms title-scene program at frame 1490.

## Evidence

- Package: `build-f5/runtime-next/gx-title-render-package-20261018/`
  (`SHA256SUMS`, `BUILD-RECORD.json`). `ww05.vpk` SHA-256
  `52761a754242c39f39e2c17a5ffcdcb0d013ce42fb7767b36b4e05a3fe8444a9`.
- Run: `build-f5/runtime-next/gx-title-render-run-20261018-2/`.
  Guest log `ww05.log` (14.93 MB), `ww05.partial` 0 bytes, console
  `flock.out` with the SIGSEGV, `cpu-samples.jsonl` with 349 samples.
  Admission 10,486,104,064 B, floor 5,905,580,032 B, exit 133.
- Superseded attempt: `gx-title-render-run-20261018-1` stopped at 307 presents
  at `8015EA5C` (“desktop interception unsupported”); it is not used for this
  result.
- Measurement commits: `71cf53a1` (per-present counters), `4552e2b3`
  (per-call timing). No renderer behavior was changed.
