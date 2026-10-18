# Logo performance: queued DVD completion coalescing (single-variable POC)

2026-10-18 · branch `task/perf-logo-60`, base `862b2ce1a7c6fec02480f83c28e6f5754f81cda0`.
Private POC, one normal-boot guest. No menu/gameplay/60 fps claim.

## Result

| window | `30a192e` | this POC | verdict |
| --- | ---: | ---: | --- |
| first 297 frames (comparable logo) | 39.85 ms / 25.1 fps | **39.24 ms / 25.49 fps** | **FAIL** (+1.5%, not material) |
| first 500 frames | 49.75 ms / 20.1 fps | **39.80 ms / 25.13 fps** | +25% |
| whole run (1,515 frames) | — | **57.25 ms / 17.47 fps** | sustained logo |
| 10 s windows | — | 12.0–17.4 fps | sustained |
| whole-window mean | — | 14.08 fps | — |
| best one-second bucket | 27 | 28 | — |

The primary 297-frame window did **not** move materially: +1.5%. The first 500-frame window
improved 25%, and the whole sustained run is 17.47 fps, which matches the visible ~17 fps. The
297-window result is a FAIL on the stated criterion.

## Mechanism (one behavioural change)

The whole `bd6e4f4` delta was reverted to `30a192e` in `2c2368f`: exact `0x80307EF4` poll PC, no
decrementer coalescing, ARAM fix and L1 pin retained. Only this was added:

`ww_host_idle_poll_ready` no longer rejects an idle turn merely because
`dvd_completions.count != 0u`. On the deferred-audio path (`audio_deferred &&
deferred_task_started`) a queued completion may coexist with the leap. The main loop's
`ww_host_dvd_complete` still delivers the FIFO head at the turn boundary, before the next dispatch
or any dependent read; `dvd_callback_active` still blocks the leap. No DVD data or ordering changes.

Measurement-only additions: `fail_*` counters on `[idle-lever]`, and `[idle-jump-taken]` now logs
`wall_us` and `cycles_per_us`.

## Mechanism check

| counter | before (same gate, instrumented `bd6e4f4`) | this run |
| --- | ---: | ---: |
| `fail_dvd` | 152,923 | **0** |
| `fail_pc` | 129,353 | 128,791 |
| `fail_state` | 15,304 | 15,304 |
| `fail_poll` | 1,507 | 3 |
| armed / turns | 2,025 / 301,111 | 3,024 / 147,121 |

`fail_dvd` dropped to zero, but the 297-window fps did not move materially. **The DVD gate was a
frequency limiter, not the causal limiter of the first-297 frame time.** It did matter for the
sustained window (first 500: 20.1 → 25.1 fps), but the whole-run sustained rate is 17.47 fps.

## The leap-wall question

The new `[idle-jump-taken]` samples answer it directly: leaps of **0.7–7.9 M guest cycles cost
18–63 µs** of host wall time (up to ~414 k cycles/µs). The full-VI leap is not the frame wall. The
35–50 ms post-present wait is guest/renderer work after the leap, not the leap itself. The earlier
52 ms wait in `bd6e4f4` was therefore not leap cost; that run changed which guest work ran, not the
cost of the jump.

## Package and run

Package `build-f5/runtime-next/perf-logo-dvd-coalesce-package-20261018`:

| artifact | bytes | SHA-256 |
| --- | ---: | --- |
| `ww05.vpk` | 112,208,541 | `13ad7948342a8a4775a0f8944addd70c72370905ac9c4cfa406a000221d16029` |
| `eboot.bin` | 111,910,003 | `047be43c64b40858b25c6d66b3c138e30c4e95830f0bd0f091e6176c6f9f3200` |
| `ww05_host` | 420,417,964 | `f9b50810aa94c4f9a13c7acb7e5d6486f30683e31f4e81cc0f9eefab9db38556` |

RecompCore `cab5301`, L1 object `30499865…`, selected REL contracts (ship, title, ikada, actor
batch) ON, turn cap 4096, VI/idle/ARAM fixes retained.

Run `perf-logo-dvd-coalesce-run-20261018-1`, start 17:09:46. Scope `MemoryMax=5905580032`,
`MemorySwapMax=0`, `OOMPolicy=stop`; admission **3,679,977,472 B** (a sibling `root-ctx-high`
build held memory below the 5.91 GB pre-flight floor; the required cap/swap/OOM settings were
unchanged). 1,515 presents, 1,530 retraces over 107.53 s.

First fault after `/res/Object/TlogoE.arc`:

```
assertion failed: false
Message:segfaultdynarmic: Segfault happened within JITted code at host_pc = 0000fffee0086180
Segfault wasn't at a fastmem patch location!
terminate called without an active exception
```

SIGABRT/core-dump (134); no `ww05.report`, `ww05.partial` 0 bytes. This is the same
corrupted-control-flow family as the post-logo faults in `bd6e4f4` and `30a192e`; no crash
regression, but post-297 measurements remain unreliable.

## Tests and checks

- Red first: the new probe case `dvd_completions.count = 1` on the deferred-audio path failed the
  strict gate (exit 134); after the gate change it passes, and `dvd_callback_active` still blocks.
- Native host suite **84/84** with `CC=gcc`; `scripts/audit_repo.sh`, attribution, whitespace and
  48 perf-harness tests pass.

## Next lever

The idle gate is no longer the first-297 limiter. The sustained frame is ~57 ms with leaps costing
microseconds, so the remaining wall is guest/renderer work after the leap, not turn frequency. The
next POC should measure that post-leap work (per-frame guest cycles, renderer thread, draw/finish
time), not add another idle-gate relaxation. The 297-frame window is the only reliable comparison
until the title-stage corrupted-control-flow fault is fixed.

## Limits and recommendation

- One run; it ended in the known title-stage JIT fault and wrote no report. The frame series is the
  source.
- The 297-window PASS criterion is **not met**; the 500-window and sustained numbers are reported
  separately and never presented as the logo fps.
- This POC is a valid measurement of the DVD gate, but it is not a speed win for the 297 window.
  Integrate only with an explicit decision that the sustained improvement (and the crash-family
  caveat) is worth it; no integration or push here.

## Evidence

- Package/build record: `build-f5/runtime-next/perf-logo-dvd-coalesce-package-20261018/`.
- Build tree: `build-f5/runtime-next/perf-logo-dvd-coalesce-20261018/`.
- Run: `build-f5/runtime-next/perf-logo-dvd-coalesce-run-20261018-1/`; guest log
  `9df91e98…` (11,985,191 bytes); harness `evidence/fps-logo/baseline.json` `270e37c0…`;
  console fault at `live-console.log:170`.
- Prior 25.1 state: [PERF_LOGO_IDLE_ARAM_2026-10-18.md](PERF_LOGO_IDLE_ARAM_2026-10-18.md).
