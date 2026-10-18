# Logo performance: decrementer coalescing in the idle leap (negative POC)

2026-10-18 · branch `task/perf-logo-60`, base `862b2ce1a7c6fec02480f83c28e6f5754f81cda0`.
Private POC package, one normal-boot guest run. **This POC fails its own criterion.** No menu,
gameplay or 60 fps claim.

## Result

| measurement (first 297-frame logo window) | 25.1 fps state | this POC |
| --- | ---: | ---: |
| fast-frame median | 39.85 ms (25.1 fps) | **55.28 ms (18.09 fps)** |
| present → next-retrace median | 35.02 ms | **51.72 ms** |
| retrace → present median | 4.83 ms | 3.37 ms |
| idle leaps | 6 (4 VI, 2 DEC, 0 ARAM) | 11 (11 VI, 0 DEC, 0 ARAM) |
| whole-window mean | — | 12.73 fps over 118.97 s |
| 10 s-window median | — | 14.10 fps |
| best one-second bucket | 27 frames | 19 frames |

The POC removed the decrementer clamps and made every leap reach a full VI period (7.56-8.09 M
cycles), but the frame got **slower**, not faster. The idle wait rose from 35.02 ms to 51.72 ms and
the fast frame from 39.85 ms to 55.28 ms. The run was 1,516 frames before its fault; first 500
frames 56.97 ms fast (17.55 fps), all frames 61.10 ms fast (16.37 fps).

**Do not integrate this change.** The last good state is `30a192e` (ARAM-only coalescing, 25.1 fps).

## Mechanism

Two changes were added on top of the previous ARAM fix:

1. **Wider poll PC block.** The idle poll is recognised at `0x80307EF4`, `0x80307EF8` or
   `0x80307EFC` (the `lwz`/`cmplwi`/`bc` loop) instead of only `0x80307EF4`. All three still pass
   the same poll-identity checks.
2. **Future decrementer expiry coalesced.** On the deferred-audio idle path, `deadline()` ignores a
   future decrementer expiry, so the turn can reach the VI period. A decrementer already pending is
   still delivered immediately with a one-cycle deadline. `advance()` makes the expiry pending
   inside the turn; the interrupt is delivered at the next turn boundary after the VI event.

The previous deferred-audio ARAM coalescing and the L1 pin are retained.

Why it regressed: the full-VI leap now replaces the decrementer-bounded turns, but the guest does
more work in that one turn. The measured post-present wait rose by 16.7 ms even though the leap
covers almost exactly 8.1 M cycles. The decrementer expiry was acting as a useful scheduling/yield
point; removing it did not remove guest work, it moved more of it into one long turn. This is a
measured result, not a claim about the exact guest code.

## The 0.91% question

The new failure counters in `[idle-lever]` answer why the poll is not recognised on most turns:

```
armed=2025 turns=301111
fail_pc=129353 fail_exc=0 fail_state=15304 fail_msr=0 fail_unsafe=0
fail_dvd=152923 fail_r13=0 fail_poll=1507
```

- **`fail_dvd=152,923` (51% of all turns)** — a queued DVD completion or active DVD callback blocks
  the idle leap. This is now the largest single gate.
- **`fail_pc=129,353`** — the guest is not in the idle loop on that turn (it is executing logo,
  loader or renderer code). This is not an identity failure.
- `fail_state=15,304`, `fail_poll=1,507`; `r13`, `MSR` and unsafe-PC failures are zero.

So the 0.91% was not a broken `pc`/`r13` check: roughly 1.8 poll hits per frame are enough for one
leap, but DVD completions block half the turns and the decrementer clamped the rest. The POC fixed
the decrementer clamp and made the frame worse.

## Tests (red first)

- `idle_poll_probe` now expects `0x80307EF8` and `0x80307EFC` to be recognised, `0x80307F00` not.
- New case: deferred audio, idle poll, `decrementer=100`, `clock_remainder=0`. The turn must reach
  the VI period, not the 1,212-cycle decrementer expiry, and finish with `decrementer_pending` and a
  third VI retrace.
- Red: the widened PC assertion failed first (`idle_poll_probe.c:63`), then the old deadline would
  fail the coalescing case. Green: probe passes.
- Native host suite **84/84** with `CC=gcc`; `scripts/audit_repo.sh` and whitespace checks pass.

## Package and run

Package `build-f5/runtime-next/perf-logo-dec-coalesce-package-20261018`:

| artifact | bytes | SHA-256 |
| --- | ---: | --- |
| `ww05.vpk` | 112,208,795 | `09af5d889236247f923b8b3eb83876e0da78c212308564ca5a72f6042dc0a3b5` |
| `eboot.bin` | 111,910,244 | `efa3ff76c3eaa6942edf95980637f71942ba41ae85a5dddb9b47b95e0bedef89` |
| `ww05_host` | 420,417,100 | `644ad976cbcfb04df2d32419198509e5eb927bb3a67295ca0c8a9199dd9e9d33` |

RecompCore `cab5301`, L1 object `30499865…`, `loop_80307EF4` `0x306` bytes, selected REL contracts
(ship, title, ikada, actor batch) ON.

Run `perf-logo-dec-coalesce-run-20261018-1`, one normal boot, start 16:57:39. Scope
`MemoryMax=5905580032`, `MemorySwapMax=0`, `OOMPolicy=stop`; admission **6,095,798,272 B**, above the
5.91 GB floor. 1,516 presents, 2,040 retraces over 118.97 s. It opened
`/res/Object/TlogoE.arc`, then faulted:

```
[MemoryWrite]: Executing: movt r0, #0x9577
Unhandled SIGSEGV at pc 0x0000aaab20fabcfc
PC: 0x956c332c, SP: 0x80170c00, LR: 0x956c2b2f
```

SIGTRAP/core-dump (exit 133); no `ww05.report`, `ww05.partial` 0 bytes. This is a new title-stage
signature, not the earlier `PC is 0x0` or `std::terminate` faults.

## Next lever

1. **Revert the decrementer coalescing** (and decide separately whether the EF8/EFC widening helps;
   this run cannot separate them). The ARAM-only `30a192e` remains the best measured logo state.
2. **Coalesce or defer a queued DVD completion on the idle path**, because `fail_dvd` now blocks 51%
   of turns. The completion must be delivered in order and before any dependent guest read; a
   queued completion delivered at the next turn boundary is the candidate, but it must be tested
   against DVD ordering.
3. Investigate why one full-VI leap costs more wall time than several decrementer-bounded turns.
   If the full-VI turn is inherently slower, the 25.1 fps frame is limited by guest work, not by the
   number of idle turns.

## Limits

- One measurement run; it ended in a native SIGSEGV and wrote no report. The frame series is the
  only source for the numbers.
- Vita3K timing is not physical Vita timing; nothing here predicts the handheld.
- The public runtime pin is unchanged and the commit is unpublished. This is a private POC, not an
  integration. Independent review is still required; this POC should not be integrated.
- No menu, gameplay, audio, hardware, 30 fps or 60 fps claim.

## Evidence

- Package and build record: `build-f5/runtime-next/perf-logo-dec-coalesce-package-20261018/`.
- Build tree: `build-f5/runtime-next/perf-logo-dec-coalesce-20261018/`.
- Run: `build-f5/runtime-next/perf-logo-dec-coalesce-run-20261018-1/`.
  Guest log `b9eb0109…` (14,798,525 bytes); harness document
  `evidence/fps-logo/baseline.json` `99561449…`; console fault at `live-console.log:279`.
- Prior good state: [PERF_LOGO_IDLE_ARAM_2026-10-18.md](PERF_LOGO_IDLE_ARAM_2026-10-18.md)
  (commit `30a192e`, 25.1 fps).
