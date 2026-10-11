# VITA-WW-12 — performance measurement harness: frames, windows, baseline (phase 1)

2026-10-11 · [ticket](https://trello.com/c/XTI4rWep) · measurement infrastructure, delivered on branch
`vita-ww-12-perf`. **No optimization is included, and none is claimed.** The game has never presented a
frame on the Vita path yet; what this phase delivers is the harness and the protocol that will measure
the moment it does, plus a parsed record of every real run that exists today.

**What is proven**: the harness reads the real WW05 boot evidence (19 reports) and records it as a
baseline document; the frame-event parser and the windowed FPS metrics are tested end-to-end on
synthetic streams; the honest FPS definition for this stack is fixed and its limits are written down.
**What is not**: no FPS number, no rendered frame, no physical-Vita result, no optimization.

## The frame-rate definition for this stack

The preflight mapped what timing signals exist, and none of them is the emulator's own frame counter:

- **VI retraces (the flipper model).** RecompCore's `GXRuntime/src/vi_clock.c` models the video
  interface: 60 retraces a second over the guest work clock (486 MHz / 60 = 8.1 M cycles a retrace,
  `timebase_ticks_per_retrace` = 675,000 at 40.5 MHz). The desktop host advances it and counts
  retraces (`runtime/host/src/main.c`, `dol_vi_clock_pop_retrace` → `g_host_retrace_count`), and its
  `BLUEWAKE_FRAME_TIMING` instrument already stamps them as `[frame-timing] retrace=N us=T`. The
  Vita boot fixture (`vita/host/`) does not run the VI clock yet.
- **GX display copies (the game's flip).** `dol_gx_recomp_note_display_copy` marks the game's
  `GXCopyDisp` (BP 0x4F–0x51, c=0xF); the desktop backend counts them in
  `DolAuroraFrameTiming.display_copies`. This is the game deciding a picture is finished.
- **The boot frontier.** The WW05 report's `STATE` line carries the guest's edge count and absolute
  cycles at the stop (`edges`, `cycles`). These count boot progress in guest time; they are not
  frames and not wall time. Every report so far says `rendered=0`.
- **What is not counted.** The Vita3K renderer's own present (and any host overlay): an emulator can
  duplicate or interpolate what it shows, so its present count is an emulator artifact, not a game
  frame. The harness never reads it.

**Primary metric (the FPS we will report): presented game frames per wall second.** A presented game
frame is a GX display copy that reached the display. **Secondary metric: VI retraces per wall second**
— it says whether the emulation keeps up with the guest's 60 Hz clock at all, not what the player
sees. A run is only interpretable with the primary and secondary together: 60 retraces with 30
presented frames is a game drawing at 30 on a healthy clock.

## The event stream and the baseline

The harness reads a run log for two markers, searched anywhere in a line so interleaved logger output
cannot hide a stamp (the desktop bench's `scripts/bench_report.py` reads `[frame-timing]` the same
way):

    [frame-timing] retrace=N us=T    one VI retrace, host wall microseconds
    [frame-present] frame=N us=T     one presented game frame

`[frame-timing]` is the existing desktop convention, reused unchanged. `[frame-present]` is the one
new contract; when the Vita host presents its first frame it must emit this line at each presented
display copy. A malformed marker, a repeated index or wall time that goes backwards is refused: a
broken stream is not a measurement.

Metrics (`scripts/vita/perf/fps.py`): windows are non-overlapping and anchored at the first stamp; a
window's FPS is the frames inside it divided by its length; only complete windows are reported so a
short tail cannot drag the series down. The summary carries mean, median, p95 and minimum window FPS
and the p95 frame time, so one slow second cannot hide in an average. A marker series with exactly one
stamp is refused rather than reported as unmeasured: one stamp cannot produce a rate.

The baseline document (`scripts/vita/perf/baseline.py`) is one `baseline.json` plus a `summary.txt`,
written under `--out` (the ignored `build-f12/` tree by default). It records the label, the source
SHA, the frame source, the window, the device and scene when known, the metrics or an explicit
`"measured": false`, the boot series when present, the verdict, and the caveats. The verdict is
against this ticket's numbers: **target 60 fps, floor 30 fps**. A run below the floor still exits 0 —
the measurement succeeded and the number is the number; only unusable evidence fails (exit 1).

## How a measurement is taken (the protocol)

1. Build at one source SHA; record it. One configuration (renderer backend, resolution, cycle
   settings), recorded; the Vita3K profile stays private.
2. Fixed scene and fixed window: boot/title, window 10 s of presented frames after the first frame
   (loading and shader compiles are not part of the measurement). Same scene, window, source and
   configuration for any before/after pair.
3. Run three times; report each run's baseline and the median of the three medians. Keep the log.
4. Measure before any change; measure after the same change the same way; a change is only accepted
   with both numbers and no regression in the WW05 fixture tests.

Command (today, boot evidence only; `--events` joins in the moment a log has frame markers):

```sh
python3 scripts/vita/perf/run.py --out build-f12/perf/<label> --label <label> \
  --events <run.log> --ww05-report <ww05.report> ... [--window 10] [--source <sha>]
```

## What evidence counts for or against 60 FPS

- **For**: a measured median window FPS ≥ 60 from `[frame-present]` stamps, sustained over the fixed
  window, on the recorded device and configuration, at the recorded source SHA, with the log and
  baseline retained. Secondary: retrace rate ≥ 60 (the clock is not the limit).
- **Against**: median window FPS < 60; or < 30 (the floor is missed); or retraces < 60 while frames
  are low (emulation cannot keep up, before any rendering question).
- **Not admissible**: any number without its source, scene, window and configuration; any count from
  the emulator's own present; any claim from a run whose log did not parse.

## What this does NOT prove

- **Vita3K timing is not physical Vita timing.** The guest runs on the host CPU (aarch64 here,
  dynarmic's arm64 backend); the physical handheld's Cortex-A9 at 444 MHz is a different machine. An
  FPS measured in Vita3K does not transfer to hardware, and no physical-Vita result exists.
- **Nothing has rendered yet.** All 19 real WW05 reports are `frontier=FAIL` with `rendered=0`: the
  boot fixture stops at the first complete GX command. No FPS baseline can exist before a frame is
  presented; the harness is built so that the first one is measured, not estimated.
- **This phase measures; it does not optimize.** Compile flags (-O1 today), Vita3K settings and
  runtime seams are the later levers of this ticket, each gated on a before/after measurement.
- **A green suite is not a game that runs.** The tests prove the harness; the game still has to boot
  and render (VITA-WW-05 lane).

## Evidence recorded today

`scripts/vita/perf/run.py` was run read-only over the 19 real reports under
`build-f5/ww05/vita3k-run-*/.../ww05.report` (never written to). Result, stored in the ignored
`build-f12/perf/ww05-boot-series/`: `runs=19 pass=0 fail=19 rendered=0 sources=19
edges=1..10874 cycles=0..271011`. Every embedded build source is kept in the JSON. The series is
recorded as `"measured": false` — the only honest verdict available before the first frame.

## Scope and checks

- Authored: `scripts/vita/perf/**` (harness and runner), `vita/feasibility/perf/**` (tests), this
  document. No pin, product default, desktop file or runtime file is modified; no network, no push,
  no Vita3K process.
- Outputs (ignored): `/home/fred/projects/zeldaWWHDRecomp/build-f12/`.
- Checks: 26 harness tests green at the delivery SHA (`python3 vita/feasibility/perf/test_*.py`), two
  of them reading the real WW05 evidence on this machine; `scripts/audit_repo.sh` run at the delivery
  SHA.
