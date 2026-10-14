# Private logging A/B preparation

`WW05_QUIET_LOGGING` is a CMake option, **OFF by default**. Enable it with
`-DWW05_QUIET_LOGGING=ON` only for an explicitly approved private trial.
It does not change game code, dispatch policy, VI timing or rendering.

Quiet mode suppresses `[progress]`, `[edge-progress]`, sampled `[dispatch]`
success lines and byte-verified exception-vector dispatch/return success lines
(external, FP and decrementer vectors). Failed/exceptional sampled returns stay.
It keeps `[frame-present]`, `[frame-timing]`, exception registers/vector bytes,
STOP, panic/halt, REL frontier/link/materialization and prolog milestones, and
all other safety diagnostics. Remaining writes still fail loudly. Only the
suppressed diagnostics' formatting/writes and progress-only wall-clock reads
are omitted; edge counters, diagnostic thresholds, cycle credit, ownership
checks, actual vector execution and the `ww05.dispatch` L/E/R file stay.

WW56 uses CPU **or** output growth for activity. Frame stamps provide periodic
low-volume output during the logo; busy pre-frame execution remains activity.
No heartbeat is needed or added. Silence plus low CPU still stops after 30 s.

## Game-free checks

With the existing pinned dependencies/toolchain available:

```sh
CC=gcc CXX=g++ python3 -B -m unittest discover -s vita/host -p test_logging.py -v
```

The existing synthetic probes build serially into this worktree's
`build-f5/services-tests{,-quiet}`. Tests compare execution reports, cover all
three vectors (including byte mutations and spent quanta), preserve ownership
and cycle contracts, and check retained frame/safety/REL/prolog output. These
are Linux host contracts, not a Vita or Vita3K performance result.

## Future bounded comparison (not executed or authorized by this document)

1. After independent review and separate build/run authorization, prepare A
   (OFF) and B (ON) from the **same clean delivery commit**, same pinned runtime,
   compiler, translated-object optimization, renderer, VI/pad/prolog options
   and private inputs. Reuse only verified identical translated objects;
   inspect the build plan and stop if it would rebuild a game chunk. Record
   chunk/source/dependency/command hashes before and after. Relink the host only
   in separate outputs, `-j1`; never overwrite the historical candidate.
2. Set each host's embedded `WW05_SOURCE` to its actual host-source SHA. Keep
   original generation/source identities in the reuse receipt separately;
   never relabel an old VPK or copy an old identity header as a new build's
   provenance. Record both CMake values and each new payload/package hash.
   Launcher source and guest source are distinct WW56 identity inputs.
3. Use fresh WW56 run ids/profiles, identical runtime/config/disc receipts,
   deferred audio and continuation policy in both arms, a 180 s guest ceiling,
   and `--rel-settle-seconds 0 --present-grace-seconds 0` for the logo window.
   Keep all failure, activity, unit-ownership and cleanup gates. No screenshots,
   memory-byte capture or RAM polling. WW56's existing one-shot process
   snapshots are a protocol feature, not permission to run them here.
4. Alternate at least A/B/A/B, preferably enough paired repetitions to estimate
   uncertainty. Predeclare frames 1→296 and subwindows 11→101 and 141→261.
   Reject comparisons with different frontiers/failures or missing frame bounds;
   report them as incomplete instead of substituting the last surviving frame.
   Reuse `scripts/vita/perf/frame_events.py`; report elapsed wall time and
   `(last_index - first_index) / elapsed_seconds` (295 intervals for 1→296),
   raw present counts, frame-gap distribution, silent tail and CPU watch data.
5. Confirm targeted success markers disappeared while frame/safety markers
   remain. Quiet logs cannot reconstruct WW71's progress-based cycle curve,
   100 ms cycle buckets or sub-frame ≥100 ms frozen-stall census. Do not pretend
   frame gaps measure those stalls. Compare phase **present cadence**, not
   invented phase cycle rates; further stall attribution needs separate work.

WW71 observed 2,730 lines/s and estimated a small cost, not a measured cost.
Its historical 4.4% two-run spread is **not a statistical bound**. A small or
inconsistent delta is inconclusive unless the paired uncertainty excludes a
predeclared meaningful logging cost; a reproducible positive delta attributes
only this combined logging gate. No FPS improvement or logging exoneration is
claimed by this preparation.
