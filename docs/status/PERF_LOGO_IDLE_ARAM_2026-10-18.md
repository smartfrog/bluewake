# Logo performance: coalescing the deferred-audio ARAM deadline in the idle leap

2026-10-18 · branch `task/perf-logo-60`, base `862b2ce1a7c6fec02480f83c28e6f5754f81cda0`.
Private POC package, one normal-boot guest run. **No menu or gameplay claim, no 60 fps claim.**

## Result

| measurement (same 297-frame logo window) | before | this POC |
| --- | ---: | ---: |
| fast-frame median | 50.75 ms (19.7 fps) | **39.85 ms (25.1 fps)** |
| fast-frame count | 239 / 296 | 289 / 296 |
| present → next-retrace median | 49.82 ms | **35.02 ms** |
| retrace → present median | 3.16 ms | 4.83 ms |
| idle leaps bounded by ARAM | 14 | **0** |

The fast-frame window is the first 297 presented frames, the same window the 19.7 fps baseline
used. **25.1 fps is 27% above 19.7 fps**, so the pre-declared fast-frame criterion passes. It is
still far from 60 fps.

The run continued past the old 297-frame fault to **1,489 frames**. That longer run is slower:
first 500 frames 49.75 ms fast (20.1 fps); all 1,489 frames 74.82 ms fast (13.4 fps), whole-window
mean 11.95 fps, 10 s-window median 13.05 fps, best one-second bucket 27 frames. The later title-logo
phase is a different workload; the pass above is only the 297-frame logo window.

## Mechanism

`vita/host/boot_host.c` `deadline()` now treats an in-flight ARAM transfer as part of the deferred
no-op audio path when all of these are true:

- the cycle domain is in a recognised idle turn (`host->cycles.idle_turn`);
- `host->dsp.audio_deferred` is set (`ww05.audio` request);
- the deferred task has started (`host->dsp.deferred_task_started`).

In that case it uses `ww_dsp_interface_deadline_excluding_aram()` (`vita/host/dsp_interface.c`),
which returns only the DSP-init deadline. The nearest remaining deadline is then the VI retrace (or
a genuine decrementer event), so the idle grant can span the full 8.1 M-cycle VI period instead of
being clamped at about 27.5 k cycles by the ARAM transfer.

The transfer is not abandoned: `advance()` receives the full turn's cycles, subtracts them from
`aram_cycles_remaining`, and completes the transfer (with the existing `[aram-complete]` trace)
inside that turn. The ARAM completion interrupt is therefore delivered at the next turn boundary
after the VI event, not at its exact ARAM cycle. That is the one timing change; the retail idle-poll
identity checks (`pc=0x80307EF4`, scheduler idle, MSR EE, no DVD completion, `r13` global, stable
zero poll word) are unchanged. The precondition that is widened is the *span*: an active ARAM
transfer on the deferred-audio path no longer disqualifies a full-VI idle grant.

The ARAM/DSP code and the idle-turn grant are the only changes. The L1 poll-skip object, turn cap
4096, program cache, VI clock, idle-deadline advance, destination-alpha and edge admissions,
rejection capsule, RAM guard, pad input and the selected REL contracts (ship, title, ikada, actor
batch) are unchanged.

## Tests (red first)

`vita/host/idle_poll_probe.c` now encodes both cases. The existing non-deferred case still expects
the ARAM clamp (`budget == ARAM_CYCLES`). The new case sets `audio_deferred` and
`deferred_task_started`, leaves an ARAM transfer in flight, and expects the turn to reach the VI
deadline (`8,100,000 - 1,212 - 600` cycles here), complete the transfer, and raise the second VI
retrace.

Red: with a stub that returned the ordinary deadline, the probe failed at
`Assertion 'ww_dsp_interface_deadline_excluding_aram(&host->dsp) == UINT64_MAX' failed` (exit 134).
Green: the probe passes and logs a VI leap, an ARAM completion inside the turn, and retrace 2.

- `idle-poll` pass; native host suite **83/84** in one ctest run, with the remaining
  `rel-lifecycle` test passing under `CC=gcc` (21 s rebuild). Its failure without `CC` is a
  test-harness setup error, not this change.
- The 21 s `rel-lifecycle` rebuild and the full ctest run were both green.

## Build

Package `build-f5/runtime-next/perf-logo-idle-aram-package-20261018`:

| artifact | bytes | SHA-256 |
| --- | ---: | --- |
| `ww05.vpk` | 112,205,336 | `a00756324e1f51b0f29bfdb5876d49474c7a1dd2cb7f9c6f9d45b29ee1c5e191` |
| `eboot.bin` | 111,908,019 | `ecb9f870684272587e1899745ac30ce42976cd4e2f96811cdefe396c20d7b924` |
| `ww05_host` | 420,417,036 | `80d934b169c8f79d6e72ade7ec497579b8ec743b5abbccc5ea0040a086642753` |

Built with `RECOMPCORE_DIR` at L1 `cab530113583330ac3640b4de62f740274cc7f5d`; the verified L1
object `30499865…` is linked and `loop_80307EF4` is still `0x306` bytes. `BUILD-RECORD.json`
records the pin, L1 object, contracts and flags. The public pin remains `ddd031b2`; `cab5301` is
local and unpublished.

## Run

One normal-boot guest, `perf-logo-idle-aram-run-20261018-1`, started 16:38:30 with
`ww05.audio`, `ww05.continue 64` and the same source/package request. The scope used
`MemoryMax=5905580032`, `MemorySwapMax=0`, `OOMPolicy=stop`. The guest peak RSS was about 2.43 GB.

**Admission deviation, disclosed:** a sibling `root-ctx-high` build held `MemAvailable` below the
earlier 5.91 GB pre-flight floor for the whole window. The run admitted at **4,793,417,728 B**;
the required 5.91 GB cgroup cap, swap 0 and OOM stop were unchanged. The previous guest peak was
2.87 GB, so the cgroup cap still had margin. This is not the usual 5.91 GB admission reading.

The run reached 1,489 presents and 1,504 retraces over 124.482 s. It then opened
`/res/Object/TlogoE.arc` and faulted:

```
assertion failed: false
Message:Should raise coproc exception here
terminate called without an active exception
Unhandled 'std::terminate()' call
```

The process exited by SIGABRT (134, core dumped). There is no `ww05.report`; `ww05.partial` is 0
bytes. This is a new title-stage signature, not the earlier `PC is 0x0` fault. It is not a clean
exit and it is not a menu or gameplay result.

The run used the program cache copied from the previous run, but the 13.8 s first-use stall still
appeared at frame 8, so that cache did not cover this shader. The fast-frame median excludes it; the
whole-window mean and the 10 s windows include it.

## Idle-leap evidence

- 14 idle leaps in 294,785 turns: **8 decrementer**, **6 VI**, **0 ARAM**. The 19.7 fps run had
  14 ARAM-clamped leaps. The longest leaps here are 7.57 M and 7.47 M cycles (VI), i.e. nearly a
  whole 8.1 M-cycle period in one turn.
- In the first 297-frame window there are **6 leaps: 4 VI, 2 decrementer, 0 ARAM**, the longest
  5.40 M and 4.95 M cycles. That is the window that moved from 50.75 ms to 39.85 ms.
- The idle precondition armed on **2,690 of 294,785 turns (0.91%)**. It is still the limiting gate:
  the leap works when recognised, but it is recognised few times per frame.
- In the first 297-frame window the post-present wait fell from 49.82 ms to 35.02 ms. The renderer
  phase is 4.83 ms, so the wait is still about twice the 16.7 ms budget for 60 fps.

## Next lever

The ARAM clamp is gone, but the idle poll is recognised on only 0.91% of turns and 8 of the 14
remaining leaps are decrementer-bounded. The next POC should find out why the poll is not recognised
(`pc`, `r13`, scheduler-idle state, or the `MSR EE`/DVD checks) and widen that recognition, then
coalesce a decrementer expiry when no thread is actually runnable. The measured ceiling if the
post-present wait fell to one turn is the 4.83 ms renderer phase; 60 fps is plausible from there but
unproven. The 13.8 s first-use shader stall is a separate lever and still dominates the whole-window
mean.

## Limits

- One measurement run; it ended in the title-stage assertion and wrote no report. The frame series is
  the only source for the numbers.
- Vita3K timing is not physical Vita timing; nothing here predicts the handheld.
- The public runtime pin is unchanged and the commit is unpublished. This is a private POC package,
  not an integration. Independent review is still required before integration.
- No menu, gameplay, audio, hardware, 30 fps or 60 fps claim.

## Evidence

- Package and build record: `build-f5/runtime-next/perf-logo-idle-aram-package-20261018/`
  (`SHA256SUMS`, `BUILD-RECORD.json`).
- Build tree: `build-f5/runtime-next/perf-logo-idle-aram-20261018/`.
- Run: `build-f5/runtime-next/perf-logo-idle-aram-run-20261018-1/`.
  Guest log `c5be5459…` (14,088,222 bytes); harness document
  `evidence/fps-logo/baseline.json` `338421a1…`; console fault at `live-console.log:169-172`.
- Previous L1 pin result: [PERF_LOGO_L1_2026-10-18.md](PERF_LOGO_L1_2026-10-18.md).
