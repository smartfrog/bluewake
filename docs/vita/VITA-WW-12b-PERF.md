# VITA-WW-12b — performance baseline of the real game boot under Vita3K

2026-10-12/13 · phase 2 of the FPS-harness work. **The baseline is measured; the
variant rows are pending their builds.** No FPS, no rendered frame and no
physical-Vita result is claimed anywhere in this document.

## Delivery status

- **Measured today:** the current-flags module (translated module at `-O1`,
  host harness at `-O2`) at commit `9fe81ed`, three watched runs, identical
  frontier and emitted config. Numbers below.
- **In flight:** the module `-O2` build (interrupted once to merge the trunk;
  resuming at `3cb3b5d`). `-O3` is gated on `-O2` showing a measurable gain
  (coordinator arbitration): if it does not, the bottleneck is not the module
  flags.
- **Trunk merge:** `3cb3b5d` brings the trunk's Info logging fix (`b368ad1`,
  avoids syscall trace amplification) and lane 18's renderer. The O2 measurement
  and the baseline re-measurement for the comparable/absolute numbers run at
  that revision under Info logging; the numbers below stand as the measured
  trace-level baseline at `9fe81ed`.

## What this measures

The recompiled module's guest throughput at the deepest boot available at
`9fe81ed` (78,789,721 guest cycles, DVD seams and callback delivery included),
under the isolated Vita3K launcher. The metric is guest cycles executed per wall
second of the guest phase, plus the same per process-tree CPU second. The boot
still stops at the REL materialization edge and presents nothing.

## Method

- Source: commit `9fe81ed` (this branch; the build receipt records the same SHA).
  Build root: `build-f6/ww12b` — never `build-f5`, which another lane owns.
- Build: `WW05_BUILD_ROOT=<root> python3 -B scripts/vita/ww05/game_build.py
  --disc <private ISO> --jobs 4 --opt-level N`. The receipt records the source,
  the artifacts and the module level actually used.
- Run (watched, one run per invocation):
  `WW05_BUILD_ROOT=<root> python3 build-f6/ww12b/tools/watch_run.py <label>
   --wayland-socket /run/user/1000/wayland-0 --timeout 600 --continue-gx 64
   --disc '<private ISO>'`
- cycles: `STATE ... cycles=` of the validated `ww05.report`.
- guest wall: `phases/guest/evidence/process.json` `duration_seconds`; the
  watcher independently records epoch timestamps for the `RUN phase=guest` /
  `STOP phase=guest` lines and per-second, per-PID CPU (`watch.jsonl`).
- cpu-seconds: the guest process tree's CPU accumulated inside the watcher's
  guest window (per-PID peaks), reported beside wall to separate scheduling
  effects.
- Three runs per variant; each run reported, median as the summary.

## Logging (measurement-validity note)

The pinned config template at `9fe81ed` emits `log-level: 0`, and the pinned
Vita3K source (`vita3k/config/include/config/config.h:176`) defines 0 as
`SPDLOG_LEVEL_TRACE`. Every run's emitted config is saved
(`phases/guest/evidence/config-emitted.yml`); all three baseline runs share the
byte-identical emitted config (sha256
`321eb130dcf8d38a639ab67b16f6eb9eccf52a80e0c0c3f97fb21ada47266eb9`, canonical
template `c11fa8fe...` plus the accepted runtime `user-id: 00`).

- **Relative comparisons between variants are valid only while every variant
  runs the same config.** The matrix records the log level per run, and the
  emitted-config digest is compared across runs; after the trunk merge all
  re-measured rows run at Info.
- **The absolute cycles/second below is logger-bound and understates the
  module.** This boot writes ~5,400 lines (~409 KB) per 10-second run at trace.
  The trunk fix (`b368ad1`, merged as `3cb3b5d`) raises the level to Info; the
  baseline is re-measured at the merged revision for the comparable absolute
  number.

## Matrix

| variant | module | log level | cycles | guest wall s (median) | cycles/s (median) | frontier | rendered |
|---|---|---|---|---|---|---|---|
| baseline | -O1 (current flags) | 0 (trace) | 78,789,721 | 9.743 | 8,086,690 | FAIL @ 80240744 | 0 |
| module O2 | -O2 | Info (planned) | pending build | | | | |
| module O3 | -O3 | — | gated on O2 | | | | |

Per-run baseline evidence (identical cycles, frontier and emitted config):

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

## The real-time gap

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
- The boot stops before any REL materialization, draw or present; nothing has
  rendered yet.
- Boot throughput is not steady-state game throughput; the measured frontier is
  dominated by boot code, DVD callbacks and initialization.
- Wall numbers include emulator startup, harness polling and the known
  post-report teardown crash; CPU-second numbers are reported beside them.
- Trace logging inflates wall time relative to the module's own work; see the
  logging note. Relative variant comparisons hold only within one log level;
  the absolute number awaits the merged Info-level re-measurement.
