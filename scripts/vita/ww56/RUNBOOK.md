# WW56 runbook: one bounded normal-boot trial of the current private candidate

Status: **prepared, not executed.** This runbook, the supervisor/run script
(`run.py`), the in-scope gate (`exec_gate.py`) and the tests are delivered for
independent review. The real trial starts only after the coordinator schedules
that review and supplies the WW54 candidate VPK path and SHA-256.

The trial answers one question: with the coordinator's 4 GiB guest envelope,
what is the first frontier the current private candidate reaches, and what is
the first observed failure. The previous real run (WW50) reached seven
authentic frame presentations and the first requested REL, then the guest unit
hit its 2.5 GiB local OOM at 8.05 s. WW50 also missed the first-present memory
snapshot because presentations and the kill happened between one-second log
observations. WW56 fixes that by making the snapshots event-driven and gives
the guest a 4 GiB static budget with a hard 4.5 GiB host headroom gate.

## Not claimed

- No title, menu, Outset, actor, pixel, gameplay, audio or FPS success is
  claimed by a WW56 run. Reaching presentations is not proof of a rendered
  game.
- A compiled candidate is not proof of normal boot; a boot is not proof of
  controllable 30 FPS gameplay. That remains the goal, not a result here.
- The trial always records `INCOMPLETE`; it never upgrades an early stop to a
  completion.
- No FPS number is computed. `[frame-present]` is a post-submit stamp and
  WW55's `[progress]` wall-microseconds integration lands separately; FPS
  computation stays parameterized and end-stamp-agnostic until that is
  reviewed.
- No optimization, source edit, build, actor injection, PNG or MEM1 capture
  happens during the trial.

## Memory envelope (coordinator decision, evidence-based)

| Phase | Static budget | Required MemAvailable before release |
| --- | ---: | ---: |
| probe (`/usr/bin/true`) | 2.5 GiB (2684354560) | checked and recorded, never blocking |
| install | 2.5 GiB (2684354560) | 3.0 GiB (3221225472) = budget + 512 MiB |
| guest | 4.0 GiB (4294967296) | 4.5 GiB (4831838208) |

`MemorySwapMax=0` and `OOMPolicy=stop` for every phase. Each phase verifies
`MemoryMax`, `EffectiveMemoryMax`, `MemorySwapMax` and `OOMPolicy` from
`systemctl --user show`, plus the kernel `memory.max`/`memory.swap.max` files,
**before** the in-scope gate releases the guest command. One `MemAvailable`
reading is taken per phase at that moment; if it is below the requirement the
phase is blocked with a message naming the deficit, the guest command is never
released, and no other application is touched.

## Preconditions checklist

1. Independent review of this ticket is done and the coordinator has given the
   go-ahead.
2. The candidate VPK exists and the coordinator supplied its path, SHA-256 and
   the guest build source commit. Nothing about the candidate is hardcoded.
3. The reviewed run worktree is clean at the reviewed commit. All commands
   below run from that worktree root.
4. The verified pinned checkouts are reachable as `<worktree>/ref/vita-ww-01/
   {RecompCore,DolRecomp}` (WW50 used authorized ignored links to the verified
   clean checkouts). The script fails fast with a clear message when they are
   missing.
5. The verified private disc image and its SHA-256 are supplied. The expected
   value is recorded in `build-f5/ww05/inputs.json` as `disc_sha256`; confirm
   it with the coordinator rather than trusting this file blindly.
6. The display socket exists and is owned by the current user:
   `/run/user/1000/wayland-0`.
7. The runtime stack defaults hold: `build-vita-tools/local-stack/vita3k/
   Vita3K` at `cea2b91f...`, `local-stack/vita3k-env` libraries, `local-stack/
   firmware` (vs0/sa0), and the private shader compiler
   `~/cloudpunk-vita/tools/vita3k/data/Vita3K/Vita3K/ur0/data/libshacccg.suprx`.
   Override any of them explicitly if the reviewed environment differs.
8. The run id is fresh. The runroot `build-f5/ww56-<id>` and receipts
   `/tmp/opencode/ww56-<id>` must not exist; the script refuses to reuse them
   and never deletes an old run.
9. `RLIMIT_FSIZE` is unlimited in the supervisor's environment; the script
   fails fast otherwise (inherited limits can break runtime memfd allocation).

## Tests (no emulator, no game build)

```bash
python3 -B -m unittest discover -s scripts/vita/ww56 -p 'test_*.py' -v
```

78 tests cover the one-shot trigger scanner, the headroom gate, the static
budget property/kernel verification, the incremental journal tail, the smaps
summary, the early-stop rules, phase-unit ownership (a foreign unit is never
stopped, and an owned `RemainAfterExit` unit is stopped even with an empty
control group), the staged plan schema, the config-gate ordering (the
permitted `user-id: 00` mutation is validated, restored, then re-checked),
the exit-race drain with cleanup-window output and the core-dump-free stop
escalation. The suite is pure Python; any native host test build for the
reviewed SHA stays `-j1` per repo convention.

## Step 1: read-only preflight

```bash
python3 -B scripts/vita/ww56/run.py --check \
  --expected-head <reviewed-sha> \
  --vpk <candidate.vpk> --vpk-sha256 <candidate-sha256> \
  --guest-source <guest-build-source> \
  --disc <verified-disc.iso> --disc-sha256 <disc-sha256> \
  --wayland-socket /run/user/1000/wayland-0 \
  --run-id <id>
```

`--check` launches nothing and writes nothing. It verifies the launcher head
and cleanliness, the pinned checkouts, every input SHA-256, the firmware and
library trees, the socket and the freshness of the two output paths. Run it
twice: once from a plain shell (the `supervisor_cgroup` check will fail and
print the exact `systemd-run` command to use) and once with the step 2 command
plus `--check` (all four checks passed in service mode during preparation).

## Step 2: launch the supervisor outside any capped scope

The supervisor must not share a memory cap with the phase units. Launch it as
its own uncapped transient unit under `app.slice`, in **service mode with an
absolute path**: a transient service starts in the user manager's home, not
the caller's directory, and `--unit` together with `--scope` is broken on this
host's systemd 259.9. WW50's supervisor used the same service form
(`app.slice/ww50-supervisor.service`, all ancestors `max`).

```bash
systemd-run --user --unit=ww56-supervisor.service --slice=app.slice --wait --pipe -- \
  /usr/bin/python3 -B <worktree>/scripts/vita/ww56/run.py \
  --expected-head <reviewed-sha> \
  --vpk <candidate.vpk> --vpk-sha256 <candidate-sha256> \
  --guest-source <guest-build-source> \
  --disc <verified-disc.iso> --disc-sha256 <disc-sha256> \
  --wayland-socket /run/user/1000/wayland-0 \
  --run-id <id> \
  > /tmp/opencode/ww56-<id>-supervisor.log 2>&1
```

`--wait --pipe` keeps the supervisor in the foreground and returns its exit
code when the trial ends; the redirect captures the full supervisor log. The
script refuses to run unless its own cgroup ends with
`/app.slice/ww56-supervisor.service` and every ancestor `memory.max` is `max`.
Re-running with the same unit name needs `systemctl --user reset-failed
ww56-supervisor.service` first (transient units are garbage-collected, but a
failed one may linger). To abort a live trial, stop the supervisor and both
phase units:

```bash
systemctl --user stop ww56-<id>-guest.service ww56-<id>-install.service ww56-supervisor.service
```

Defaults (all overridable; changing one changes the protocol, so note it in
the run record):

| Option | Default | Meaning |
| --- | --- | --- |
| `--guest-ceiling-seconds` | 180 | guest phase deadline; the unit also has a `RuntimeMaxSec` backstop 15 s later |
| `--rel-settle-seconds` | 5 | stop this many seconds after the first requested REL; `0` disables |
| `--present-grace-seconds` | 10 | stop this many seconds after the first presentation when no REL follows; `0` disables |
| `--hang-seconds` | 30 | silence + <5% CPU for this long aborts as a hang |
| `--cleanup-grace-seconds` | 10 | unit stop grace (`TimeoutStopSec`) before systemd escalates the stop; core dumps stay disabled (`LimitCORE=0`) |
| `--audio` | deferred | deferred DSP bootstrap, as in WW50 |
| `--continue-gx` | 64 | continuation journal profile, as in WW50 |
| `--install-budget-bytes` | 2684354560 | installer cap |
| `--guest-budget-bytes` | 4294967296 | guest cap |
| `--install-headroom-bytes` | 3221225472 | installer headroom gate |
| `--guest-headroom-bytes` | 4831838208 | guest headroom gate |

Exit codes: `0` the trial ran and recorded its frontier (always
`INCOMPLETE`), `2` aborted with an error, `3` blocked on a precondition
(headroom or an identity gate).

## Phase flow

1. **Preflight (supervisor).** Supervisor cgroup and ancestor caps; identities
   before (launcher, pins, VPK, runtime, driver stack, shader compiler,
   firmware, disc).
2. **Staging.** Fresh private profile `build-f5/ww56-<id>`: runtime copy,
   candidate VPK copy, firmware, shader compiler, verified disc link, private
   config; receipts in `/tmp/opencode/ww56-<id>`.
3. **Isolation probe.** One transient unit runs `/usr/bin/true` through the
   same gate, property verification and permit path. It launches no emulator.
4. **Install.** Unit `ww56-<id>-install.service`, budget 2.5 GiB, deadline
   60 s. Stops when the installed payload is complete and verified
   (`installed_payload`), then the emitted config is preserved, the payload is
   hash-verified and the canonical config restored.
5. **Guest.** Unit `ww56-<id>-guest.service`, budget 4 GiB, deadline 180 s,
   fresh `ww05.request` with the supplied guest source and candidate package.
6. **Postflight.** Input and payload gates re-verified, identities after
   compared with identities before, `result.json` written under the receipts.

## Stop conditions and decision tree

Each phase writes its stop to `<phase>-disposition.json` and to `result.json`.
For the guest:

| stop_reason | Meaning | What it preserves / next step |
| --- | --- | --- |
| `unsupported-or-context-error` | First failure marker: `[gx-unsupported]`, `[fatal]`, `[renderer-error]`, config load failure, `STOP reason=` with unsupported/failed/unhandled/rejected | The failing lines are in the disposition and journal. This is the "first observed failure" to fix next. |
| `first-requested-rel-settle` | The first `[rel-frontier]` line arrived, then `--rel-settle-seconds` passed | The REL request and whatever followed in the settle window. Raise the settle window or disable it to pursue deeper materialization in a later trial. |
| `first-present-grace` | First `[frame-present]` arrived and no REL followed within `--present-grace-seconds` | The presentation frontier plus the first-present snapshot. |
| `bounded-phase-planned-time-stop` | The guest ceiling was reached with no earlier frontier | Everything up to the ceiling; still `INCOMPLETE`. |
| `inactive` | Hang rule: no log growth and <5% CPU for `--hang-seconds` | Partial journal and CPU/watch evidence; abort is honest, not a verdict. |
| `managed-native-exit` | The owned process exited by itself | Unit properties at exit (e.g. `Result=oom-kill` with `MemoryPeak`); the kernel verdict, not an unsupported-guest verdict. |

Installer stops at `installed_payload`; anything else aborts the trial before
the guest starts.

## Snapshot protocol (the WW50 fix)

Two one-shot, event-driven triggers, then never again:

1. `[create]: GPU =` -> `snapshot-gpu-context-created/`
2. first `[frame-present]` -> `snapshot-first-actual-frame-present/`

The GPU marker arrives on the guest runtime/process logs; the present stamp
arrives in the guest journal. The watcher reads every log continuously (about
every 50 ms through the phase observer) and feeds each newly read line to one
single-fire scanner, so each trigger fires once from wherever its line
appears. When the owned process exits, the watcher drains the logs once more
before returning, and drains them again after the unit cleanup completes, so
output written during the stop window is still counted. A presentation that
landed just before exit is counted, and its snapshot is recorded as
`SNAPSHOT-MISSED` rather than lost. The disposition records
`presentations_at_stop`, `presentations_total` and an independent
`journal_present_lines` count with `presentations_undrained`; a non-zero
undrained count would be visible evidence loss, and no count is ever invented.

Each trigger does exactly one `/proc/<pid>/smaps`, `status`, `maps`, thread
list and one cgroup `memory.stat`/`memory.current`/`memory.events` read. If the
process is already gone when a trigger fires, the receipts record
`SNAPSHOT-MISSED <trigger>` honestly instead of inventing a replacement.

There is no RAM polling loop, no RSS threshold and no memory-threshold kill.
The user-disabled MemWatcher stays disabled. The one-shot `MemAvailable` gate
is the only other memory read, and it only decides whether to release a phase.

## Evidence map

| Path | Contents |
| --- | --- |
| `/tmp/opencode/ww56-<id>/result.json` | Final record: completion, phases, stops, snapshots, `identities_before` / `identities_after` / `identity_before_equals_after`, errors |
| `/tmp/opencode/ww56-<id>/effective-plan.json` | Staged plan: commands, environment, hashes, phases |
| `/tmp/opencode/ww56-<id>/<phase>-{exec,ready,verified-preexec,disposition}.json` | Per-phase command spec, gate pid/cgroup, pre-exec property/kernel/headroom proof, unit ownership (`properties_before_creation`, `invocation_id`), `terminal_drain`, presentation counts (`presentations_at_stop`, `presentations_total`, `journal_present_lines`, `presentations_undrained`), final state |
| `/tmp/opencode/ww56-<id>/<phase>-blocked.json` | Present only when a headroom gate blocked the phase |
| `/tmp/opencode/ww56-<id>/cpu-log-watch.jsonl` | One line per ~1 s per phase: pid, cpu_fraction, new log lines, tail, presentations, snapshots, stop; one `terminal_drain` line per drain (wrapper exit and post-cleanup), each with both presentation counts |
| `/tmp/opencode/ww56-<id>/snapshot-*/` | One-shot memory evidence per trigger; raw addresses stay local, no memory bytes read; a trigger that arrives after exit is recorded as `SNAPSHOT-MISSED`, never invented |
| `/tmp/opencode/ww56-<id>-supervisor.log` | Supervisor console: RELEASE/WATCH/SNAPSHOT/STOP/RESULT lines |
| `build-f5/ww56-<id>/` | Fresh private profile: staged inputs, phases, installed payload, guest journal and reports |
| `build-f5/ww56-<id>/phases/<name>/evidence/process.{log,json}` | Owned wrapper log and disposition (waited, exit, duration) |

## Post-run analysis (separate step, no gating)

The run script preserves the raw journal and watch records; analysis is a
separate, read-only step and is never a run gate:

- Reuse the existing parsers: `scripts/vita/perf/frame_events.py` for
  `[frame-present]`/`[frame-timing]`, `scripts/vita/relset/boot_residency.py`
  for `[rel-frontier]`/`[rel-materialized]`/`[rel-link]`.
- Count exception-vector and fp-resume lines per second against `[progress]`
  lines per second as a logging-cost discriminator:
  `grep -c '\[exception-vector-return\]'`, `grep -c '\[fp-exception-resume\]'`,
  `grep -c '\[progress\]'` on the guest journal, bucketed by the `[progress]`
  wall stamp once WW55's integration lands (end-stamp-agnostic; do not depend
  on a specific new field name).
- Frame-rate windows, if ever computed, must include silent tails, and
  `[frame-present]` is a post-submit stamp: boot cadence stays labelled
  exploratory, and no FPS number is a WW56 result.

## Abort, cleanup and honesty

- Every phase unit is stopped and reaped by the supervisor (`systemctl --user
  stop`, then owned-group TERM/KILL through `run_owned`), and the disposition
  proves the native pid is absent afterwards.
- A phase unit that already exists is refused before spawning (`require_fresh_unit`,
  `LoadState` check), and cleanup stops only the unit invocation this run
  created: the recorded `InvocationID` (or the wrapper's own `Running as unit:
  ...; invocation ID: ...` line) must match, otherwise the unit is left
  untouched and the refusal is recorded as `foreign_unit_untouched`. Units that
  stay loaded/active after their main process exits (`RemainAfterExit=yes`,
  empty control group) are still stopped.
- **Cleanup signals are not a boot crash.** When a phase ends, the supervisor
  stops the owned unit; systemd sends SIGTERM and, if the guest does not exit
  within `--cleanup-grace-seconds`, escalates through SIGABRT to SIGKILL. Our
  units set `LimitCORE=0`, so even a SIGABRT during cleanup produces no core
  file; systemd may still label the unit result `core-dump` because that is the
  signal classification, not a produced dump (verified on this host: the
  coredump handler reports "terminated abnormally without generating a
  coredump"). Attribute a SIGABRT to cleanup only when the manager journal
  confirms its origin (the unit's stop sequence); a signal or result
  classification alone is not proof. A SIGABRT, a killed process or a
  `core-dump` result during cleanup is our teardown, not a guest failure and
  not the trial's frontier; report it as cleanup, never as a boot crash. The
  trial's own stop reasons (the table above) are the only failure vocabulary.
- Output written during the cleanup window is drained after cleanup and counted
  in the disposition (`presentations_total`), with `presentations_at_stop` and
  `journal_present_lines` recorded separately so any evidence loss is visible.
- On any error the receipts already written stay; the supervisor never deletes
  evidence. Re-running requires a fresh run id.
- A WW56 run is a bounded observation, not a product result. Say what ran on
  which device; a build that compiles is not a game that plays.
