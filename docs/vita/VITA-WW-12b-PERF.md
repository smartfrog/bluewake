# VITA-WW-12b — performance baseline of the real game boot under Vita3K

2026-10-12/13 · phase 2 of the FPS-harness work. **The baseline is measured at
two revisions; the variant rows are pending their builds.** No FPS, no rendered
frame and no physical-Vita result is claimed anywhere in this document.

## Delivery status

- **Measured:** the current-flags module (translated module at `-O1`, host
  harness at `-O2`) at commit `9fe81ed` (three watched runs, trace logging) and
  at the merged trunk revision `3cb3b5d` (three watched runs, Info logging; the
  merged module materializes the requested REL and reaches the scheduler idle).
- **Paused:** the module `-O2` build (at 61/72 remaining edges of its last pass;
  compiled objects preserved for incremental resume). It was stopped on a
  memory-pressure request and resumes at the coordinator's signal. `-O3` is
  gated on `-O2` showing a measurable gain (coordinator arbitration): if it does
  not, the bottleneck is not the module flags.
- **Trunk merge:** `3cb3b5d` brings the trunk's Info logging fix (`b368ad1`,
  avoids syscall trace amplification) and lane 18's renderer. The O2 measurement
  runs at that revision under Info logging; the `9fe81ed` numbers stand as the
  measured trace-level baseline, and the merged-revision numbers below are the
  comparable ones for the variant matrix.

## What this measures

The recompiled module's guest throughput, under the isolated Vita3K launcher, at
the deepest boot each revision reaches: `9fe81ed` stops at the REL materialization
edge (78,789,721 guest cycles) and `3cb3b5d` materializes the REL and reaches the
scheduler idle (78,796,192 cycles). The metric is guest cycles executed per wall
second, plus the same per process-tree CPU second, measured over the guest phase
(no rendered frame exists at either revision).

## Method

- Sources: commits `9fe81ed` and `3cb3b5d` (this branch; each build receipt
  records its own SHA). Build roots: `build-f6/ww12b` and `build-f6/ww12b/o2`
  (never `build-f5`, which another lane owns).
- Build: `WW05_BUILD_ROOT=<root> python3 -B scripts/vita/ww05/game_build.py
  --disc <private ISO> --jobs 4 --opt-level N`. The receipt records the source,
  the artifacts and the module level actually used.
- Run (watched, one run per invocation):
  `WW05_BUILD_ROOT=<root> python3 build-f6/ww12b/tools/watch_run.py <label>
   --wayland-socket /run/user/1000/wayland-0 --timeout <s> --continue-gx 64
   --disc '<private ISO>'`
- `9fe81ed` runs (600 s deadline): cycles from `STATE ... cycles=` of the
  validated `ww05.report`; guest wall from `process.json` `duration_seconds`.
- `3cb3b5d` runs (180 s deadline, no report is written): the watcher samples the
  live `[edge-progress] ... cycles=` trace every 0.25 s, detects the
  `[scheduler-idle]` frontier, and the frontier crossing time is interpolated
  from the sampled curve; wall boundaries come from the process log's first
  timestamp and the watcher's epoch stamps.
- cpu-seconds: the guest process tree's CPU accumulated inside the guest window
  (per-PID peaks), reported beside wall to separate scheduling effects.
- Three runs per variant; each run reported, median as the summary.

## Logging (measurement-validity note)

The config template at `9fe81ed` emits `log-level: 0`, and the pinned Vita3K
source (`vita3k/config/include/config/config.h:176`) defines 0 as
`SPDLOG_LEVEL_TRACE`; the merged template (`3cb3b5d`) emits `log-level: 2`
(Info). Every run's emitted config is saved
(`phases/guest/evidence/config-emitted.yml`), and all runs of one revision share
a byte-identical emitted config (trace baseline: sha256 `321eb130…`; merged
baseline: sha256 `38163242…`; both are the canonical template plus the accepted
runtime `user-id: 00`).

- **Relative comparisons between variants are valid only while every variant
  runs the same config.** The matrix records the log level per row, and the
  emitted-config digest is compared across runs; the O2/O3 rows run at Info.
- **The `9fe81ed` absolute number is logger-bound and understates the module**
  (this boot writes ~5,400 lines / ~409 KB per 10-second run at trace). The
  merged rows are measured at Info; they are the comparable absolute numbers.

## Matrix

| variant | revision | module | log level | boot depth | boot rate (process) | boot rate (exec) | idle-spin rate | rendered |
|---|---|---|---|---|---|---|---|---|
| baseline | `9fe81ed` | -O1 | 0 (trace) | 78,789,721 @ REL refusal | 8.09M/s | — | n/a (stopped) | 0 |
| baseline | `3cb3b5d` | -O1 | 2 (info) | 78,796,192 @ scheduler idle | 13.42M/s | 19.47M/s | 73.60M/s wall, 71.95M/cpu-s | 0 |
| module O2 | pending build | -O2 | 2 (info, planned) | | | | | |
| module O3 | gated on O2 | -O3 | — | | | | | |

Per-run baseline evidence at `9fe81ed` (identical cycles, frontier and emitted
config):

| run | guest wall s | cycles/s | process-tree cpu s | cycles/cpu-s | report sha256 |
|---|---|---|---|---|---|
| base-run1 | 9.977 | 7,897,385 | 8.35 | 9.44M | a6710599… |
| base-run2 | 9.581 | 8,223,797 | 7.93 | 9.94M | 29cbf8b3… |
| base-run3 | 9.743 | 8,086,690 | 12.08 | 6.52M | 34a147d2… |

All three stop at the identical frontier: `STOP pc=80240744` ("REL
materialization requires a checked raw-image provider and scratch mapping"),
`END frontier=PASS live=0 rendered=0`, process exit 133 (the known post-report
teardown crash, preserved). The guest process is multi-threaded (emulator
workers), so the cpu-second column is coarser than wall; wall is the primary
metric. Host harness (`ww05_host`, services) stays at its established `-O2` in
every variant, so the matrix isolates the translated module's codegen.

## Merged revision (`3cb3b5d`, Info logging): boot to the scheduler idle

The merged trunk materializes the requested REL and the boot no longer stops at
`80240744`. It reaches the **scheduler idle** instead: `[scheduler-idle]` at
**78,796,192 cycles** (pc `80307EF4`) reports `run_bits=00000000
current_thread=00000000`, no runnable threads, `vi_enabled=0 vi_retraces=0`,
`draws=0 presented=0`. With the experimental VI clock off, the game's main
thread waits for the video system; the host keeps executing the idle loop, and
the run ends at the launcher's phase deadline (180 s) with **no report**
(`status 124`, INCOMPLETE). The merged-revision metric therefore comes from the
live guest trace: the watcher samples the `[edge-progress] ... cycles=` line
every 0.25 s and detects the `[scheduler-idle]` frontier, and the frontier
crossing time is interpolated from the sampled curve.

Baseline at the merged revision (module `-O1`, host `-O2`, Info logging,
identical emitted config sha256 `38163242…`, log-level 2; three runs):

| run | boot to idle, from process start | frontier rate (process) | frontier rate (execution) | frontier cpu s | frontier cycles/cpu-s | idle-spin rate | idle cycles/cpu-s |
|---|---|---|---|---|---|---|---|
| base-info-r2 | 5.75 s | 13.70M/s | 19.48M/s | 4.57 | 17.24M | 73.60M/s | 72.53M |
| base-info-r3 | 5.87 s | 13.42M/s | 19.47M/s | 4.61 | 17.09M | 73.79M/s | 71.82M |
| base-info-r1b | 6.03 s | 13.06M/s | 18.89M/s | 4.61 | 17.09M | 62.16M/s* | 71.95M |
| **median** | **5.87 s** | **13.42M/s** | **19.47M/s** | **4.61** | **17.09M** | **73.60M/s** | **71.95M** |

\* r1b's idle wall rate dropped to 62.16M because the machine's load spiked to
13 during it; the **cycles per CPU second stays within ±0.5 % across all four
runs** (71.82–72.53M), so the CPU-normalised rates are the load-robust metric
used for the variant comparison, with the wall rates reported at their recorded
load. Per-run evidence: `build-f6/ww12b/runs/base-info-r{2,3,1b}/`
(`watch.jsonl`, `summary.json`, `result.json`, emitted config).

Against real time: the boot's 0.162 s of hardware work takes 5.87 s at the
process boundary (**~36x slower**) or 4.05 s of guest execution (**~25x
slower**); the idle loop then runs at ~73.6M cycles/s wall (~72M cycles/cpu-s).
For 30 fps the guest needs ~243M cycles/s; the idle spin is still ~3.3x short,
and the boot workload ~12x short. **These are cold-boot bounds, not steady-state
execution cost:** per the WW-20 profile most of the boot's guest-thread CPU is
cold JIT compilation and much of the process CPU is software presentation, so
the hot protocol above is what will produce the number the 30 FPS goal needs.

The delivered `9fe81ed` runs are not directly comparable to these: they ended
with the REL refusal, a report write and the teardown crash inside their process
lifetime, while the merged runs are killed at the deadline; and they logged at
trace. The variant comparison (below, when `-O2` completes) is run entirely at
the merged revision with one protocol.

## Hot measurement (protocol fixed now; no frame exists yet)

The numbers above are **cold boot** numbers, and the WW-20 profile (trunk
commit `8bfd2d5`, `docs/vita/VITA-WW-20-PROFILE.md`) shows what that mixes: 82.25%
of the main guest thread's user samples are **cold Dynarmic JIT compilation**,
only 17.06% generated guest code; the full process spends ~45.7% of sampled CPU
on software presentation; and 34.29% of the measured guest wall is a post-crash
teardown tail. The 30 FPS goal is a **steady-state frame loop with a warm JIT
cache**, so the FPS that counts must be measured **hot**, and any absolute claim
from this document is a cold-boot number only.

The hot protocol (to run when the game presents frames; nothing here estimates
its result):

- **Source:** the per-frame `[frame-present] frame=N us=T` markers already wired
  by lane 16, parsed by the WW-12 harness (`scripts/vita/perf/`); a malformed,
  repeated or backwards stamp is refused. `[frame-timing]` retraces stay the
  secondary clock signal.
- **Window:** late frames only, after the frame loop is established and the JIT
  cache is warm — startup, cold compilation transients and teardown are excluded
  explicitly, and the excluded boundaries are recorded in the baseline document.
- **Metrics:** presented frames per second over the fixed window (non-overlapping
  windows anchored at the first stamp; median of window rates, plus p95 and
  minimum so one slow second cannot hide), and the per-frame interval
  distribution. The emulator's own present count is never the source.
- **Repeats:** three runs per variant and configuration, same revision, same
  emitted config, same scene and window; the median of medians is the headline.
- **Reporting:** every hot number carries its source SHA, scene, window,
  configuration digest and the run log; a hot number without them is inadmissible.

Until a frame series exists, this document's gap figures must be read as cold
boot bounds, not as the steady-state cost of executing the game.

## The real-time gap (delivered trace baseline, historical)

A GameCube CPU at ~486 MHz executes 78,789,721 cycles in **0.162 s**. The
measured guest phase is 9.743 s, so the module under Vita3K runs guest code
**~60x slower than real hardware** (9.743 / 0.162 = 60.1) at trace logging.
At the measured ~8.09M cycles/s, a 60 Hz guest frame (~8.1M cycles) needs
~1.0 s, and 30 presented frames per second would need ~243M guest cycles per
second — about 30x today's throughput. The boot is not a rendered scene; this
is the only measured execution point today and the floor the rendering work
starts from.

## Harness changes this lane added

- `7f71520`: `WW05_BUILD_ROOT` chooses the private build root (default
  unchanged, relative values refused) with a focused test, so this lane never
  writes into another lane's evidence root.
- `9fe81ed`: `--opt-level 1/2/3` for the translated module (host maps it to the
  composite level; the composite accepts level 3; the receipt records it), with
  a static-mode test. Both were verified red-then-green, and the full host
  suites pass (479 + 29 at `9fe81ed`; 565 at the merged revision).

## Compile-cost evidence (why variants are expensive)

Per-chunk scaling pilot with the exact Vita flags (user seconds; ratios to O1):

| chunk (O1 quantile) | O1 user s | O2 user s | O3 user s | O2 x | O3 x |
|---|---|---|---|---|---|
| p02 | 1.3 | 3.12 | 3.25 | 2.4 | 2.5 |
| p25 | 8.1 | 36.48 | 39.09 | 4.5 | 4.8 |
| p50 | 28.3 | 140.47 | 160.91 | 5.0 | 5.7 |
| p75 | 43.2 | 312.10 | 332.63 | 7.2 | 7.7 |
| p90 | 47.9 | 288.58 | 302.19 | 6.0 | 6.3 |
| p99 | 54.9 | 395.00 | 399.71 | 7.2 | 7.3 |
| max | 68.3 | 550.21 | 529.47 | 7.9 | 7.6 |

Historical O1 full build (build-f5 `.ninja_log`): 748 chunk objects, 5.5 h of
compile CPU, 87 min wall at -j4. With the measured scaling curve: O2 ≈ 34.1 h
CPU (~8.5 h wall at -j4), O3 ≈ 36.4 h CPU (~9.1 h wall at -j4). Variants are
built sequentially: the machine has 15 GB RAM and other lanes share it (a
concurrent pair risks OOM for both).

The composite's existing three O1-fallback chunks were pilot-compiled at O3
(337.7 s / 317.9 s / 276.9 s user) and all succeed, so the O3 variant would
compile all 748 chunks at -O3. The O2 variant applies the composite's existing
fallback list (three chunks at O1), exactly as the desktop O2 build does; its
compile database was verified: 745 chunks at `-O2`, 3 at `-O1`.

## CPU backend finding

Vita3K's config template (`scripts/vita/armv7/fp_config.yml`) exposes no
interpreter-vs-dynarmic setting. The only CPU knobs are `cpu-opt` (Dynarmic
optimizations on/off; `vita3k/cpu/src/dynarmic_cpu.cpp:349` maps it to
`all_safe_optimizations` / `no_optimizations`) and `cpu-pool-size`. Dynarmic is
the only backend; `InterpreterFallback` handles unsupported instructions only.
There is no interpreter-vs-dynarmic pair to measure; the closest knob, `cpu-opt`,
is already the harness's approved alternate config (VITA-WW-09) and is not part
of this matrix.

## What this does not prove

- Vita3K timing is not physical Vita timing (host aarch64 vs the handheld's
  Cortex-A9). No physical result exists.
- Nothing has rendered: `draws=0 presented=0` at both revisions; the merged boot
  stops at the scheduler idle waiting for the video system (`vi_enabled=0`), and
  the experimental VI clock was off for every run here.
- Every number here is a cold-boot number; per the WW-20 profile it includes
  cold JIT compilation, software presentation and teardown. It does not measure
  steady-state execution cost and cannot certify or refute the 30 FPS goal; the
  hot protocol above is required for that.
- Boot throughput is not steady-state game throughput; the measured frontier is
  dominated by boot code, DVD callbacks and initialization.
- Wall numbers include emulator startup and harness polling; the `9fe81ed`
  numbers also include the report write and the known post-report teardown crash
  (exit 133), while the merged runs are killed at the phase deadline. The
  CPU-normalised rates are reported beside wall to separate scheduling effects.
- Trace logging inflates wall time relative to the module's own work; see the
  logging note. Relative variant comparisons hold only within one log level.
