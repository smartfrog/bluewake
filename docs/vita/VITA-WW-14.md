# VITA-WW-14 — active REL set measurement: parser, tracker, baseline (preparation)

2026-10-11 · [ticket](https://trello.com/c/a49upzMX) · measurement infrastructure, delivered on branch
`vita-ww-14-relset`. **No generator change is included, and none is claimed.** Ticket 14 will split the
415 REL modules out of the composite for demand-loading; its design needs the active set measured first
("mesures d'ensemble REL actif"). The host lane does not emit REL events yet; this delivery provides the
harness, the marker contract the host lane implements, and the static catalog picture recorded from the
real ww05 build logs.

**What is proven**: the marker contract is anchored in the real seams (composite build logs, the guest
module manager's PCs, the host's materialization and slot-liveness code); the parser and the active-set
tracker are tested end-to-end on synthetic streams; the catalog parser is validated against the real
build-f5 evidence (the translator log's 415 module ids equal the 415 ids in the composite tables); the
static baseline is recorded with an explicit not-measured verdict.
**What is not**: no active set has been measured, no REL event has ever been emitted, no generator
scoping, no physical-Vita result.

## Preflight: where REL load/unload is visible

Three layers report the same lifecycle, and the contract uses each for what it can prove:

- **Build (composite).** DolRecomp reads the authentic module id from the REL header's first big-endian
  word (`src/frontend/container/rel.c`, `rel->module_id = read_be32(h + 0x00)`) and emits each REL under
  `generated/rels/<name>_<id>/`; `scripts/generate_composite.py` merges all of them into
  `rel_modules.inc` / `rel_data.inc` / `module_tables.inc`, and `cmake/composite/module_export.c` already
  carries `BlueWakeRelData {module_id, section_index, linked_start, size, bytes}` and `s_rel_modules` in
  the module descriptor. The real ww05 logs say: 206 DOL chunks + 542 REL chunks = 748 chunks, 415
  modules, 417 code ranges.
- **Guest (module manager).** The load path is intercepted at PC `0x80240744` (archived REL
  materialization, the `do_link` route) and `0x80240A48`; `0x80240C98` is the link-header check; the
  profile-list lifecycle table is aliased at `0x80241178`/`0x802411F8`.
- **Host (seams).** `host_materialize_rel()` (`runtime/host/src/main.c:2516`) materializes the raw REL;
  the success branch at `main.c:9358-9378` then reads the authentic `module_id = mem_read32(module)`
  (`main.c:9362`) and already prints `[rel] archived resource materialized name=... size=...`
  (`main.c:9374`). A materialized slot stays live while the guest module object still points at it
  (`host_rel_slot_is_live`, `main.c:2454`); when the game releases the module the slot becomes
  reclaimable and `host_remove_rel_aliases` (`main.c:2435`) drops its aliases.

## The marker contract (for the host lane)

Two markers, searched anywhere in a line, like the frame markers (`[frame-timing]`/`[frame-present]`):

    [rel-load] module=ID name=NAME bytes=B us=T    a REL became resident
    [rel-unload] module=ID [us=T]                  a REL was released

- `ID` is the authentic REL module id (the header's first big-endian word; the same id DolRecomp and the
  composite use).
- `NAME` is the module name the guest requested, without spaces (for example `d_a_acorn_leaf`).
- `B` is the byte size of the materialized REL image (the raw `.rel` size; the guest allocation is this
  rounded up to 32, at most 31 bytes more).
- `T` is the host's monotonic wall clock in microseconds, the same base as the frame markers when both
  exist. The load always carries it. The unload carries it when the host can stamp the release; it is
  recommended, because it fixes the residency span end.
- Emit a load exactly when a module becomes resident (the `host_materialize_rel` success branch is the
  natural point: id and name are both in hand there) and an unload exactly when it is released. Do not
  emit a load for a cache hit of an already-resident module: the tracker would refuse the stream.
- The harness refuses a malformed marker, a load with zero bytes, a wall time that does not advance
  across timed events, a load of an already-loaded module and an unload of one that is not loaded. A
  broken stream is not a measurement.

## The harness

- `scripts/vita/relset/rel_events.py` — the stream parser. Keeps the two markers, ignores everything
  else, refuses the defects above.
- `scripts/vita/relset/active_set.py` — the tracker. Loads minus unloads gives the resident set; the
  report carries the state after every event (the residency timeline), the high-water mark in bytes and
  in module count (each with the event that set it), every module's residency span and the modules
  ranked by bytes.
- `scripts/vita/relset/composite_evidence.py` — the static catalog parser. Reads the translator's
  `module <id>: <path> -> base 0x...` lines, the composite generator's `DOL:`/`RELs:`/`Composite:`
  summary and the `rel_modules.inc` module ids (tests only). Names and bases are kept; no game bytes.
- `scripts/vita/relset/relset_baseline.py` — the baseline document: one `baseline.json` plus a
  `summary.txt`. Without events it records the static picture and the verdict is exactly
  `not measured: no REL events in the evidence`; a measured run's verdict compares its peak active set
  against the ticket's ~215 MiB combined ceiling (active RELs + GPU + misc) — fitting under it is a
  necessary condition, not a proof of the design.
- `scripts/vita/relset/run.py` — the analysis CLI (active-set size over time, peak, top modules):

```sh
# static catalog picture only, until the host lane emits the markers
python3 scripts/vita/relset/run.py --out build-f14/relset/<label> --label <label> \
  --composite-log <generate-composite.log> --translate-log <translate-rels.log>

# the moment a run log has the markers
python3 scripts/vita/relset/run.py --out build-f14/relset/<label> --label <label> \
  --events <run.log> --scene boot
```

Exit 0 means the evidence was read and the baseline written (a not-measured verdict still exits 0 — the
honest result); exit 1 means the evidence was unusable.

## The static baseline recorded today

`run.py` was run read-only over the real ww05 build logs
(`build-f5/ww05/logs/generate-composite.log`, `build-f5/ww05/logs/translate-rels.log`; never written to)
and the result is stored in the ignored `build-f14/relset/static-catalog/`:

| Quantity | Value |
| --- | ---: |
| REL modules | 415 |
| REL chunks | 542 (748 total − 206 DOL) |
| DOL chunks | 206 |
| Code ranges | 417 |
| Known REL code | 183.1 MiB (ticket and integration notes) |
| Verdict | not measured: no REL events in the evidence |

Cross-checks that passed: the translator log lists 415 unique module ids and the composite tables carry
the same 415 ids; the composite log's totals agree with the module log. The baseline JSON keeps the 415
module ids (numbers only) for later cross-checks.

One evidence number to keep in view for the generator scoping: the 542 REL chunk objects of the ww05
Vita build carry 188.3 MiB of allocatable sections (summed per object with `readelf`, before link-time
deduplication); the linked figure behind the ticket's 183.1 MiB is the one to quote until the generator
scoping fixes its own number. Neither is a measurement of an active set.

A synthetic stream (`build-f14/relset/synthetic-example/`, clearly labelled) exercises the measured
path: peak 0.06 MiB / 4 modules, top module `d_a_agbsw0`. It is not a measurement of the game.

## Validation

- `python3 -m unittest discover -s vita/feasibility/relset` → **44 tests, all green** (11 parser,
  12 tracker, 9 catalog, 8 baseline, 4 end-to-end runner). The three real-evidence catalog tests read
  `build-f5/ww05` when present and skip elsewhere.
- `scripts/audit_repo.sh` → AUDIT PASS; `scripts/check_attribution.py` → no bot/AI attribution.
- No regression: the existing perf suite still passes (26 tests).
- No game change, no runtime change, no pin change, no Vita3K process, no network, no push.

## What evidence counts for or against the demand-loading design

- **For**: a parsed run whose peak active set fits under the ~215 MiB combined ceiling, on the recorded
  scene and build source, with the log retained and the timeline in `baseline.json`.
- **Against**: a peak at or above the ceiling (the verdict says so); or a stream the harness refused; or
  a number without its scene, source and log.
- **Not admissible**: any count from the emulator's own module list, any number whose unloads are
  missing (it overstates residency), and any extrapolation from the static totals — the whole point of
  the measurement is that the static totals say nothing about which modules are live together.

## What this does NOT prove

- **No active set exists yet.** The markers are a contract; no host lane emits them today. The static
  baseline says `not measured` and that is the only honest verdict available.
- **The ceiling is not the design.** Fitting RELs under ~215 MiB is necessary, not sufficient; the
  resident composite, heap and GPU budgets are other tickets.
- **A green suite is not a game that runs.** The tests prove the harness; the boot lane still has to
  reach the REL loads and the host lane has to emit the markers.

## Scope and checks

- Authored: `scripts/vita/relset/**` (harness and runner), `vita/feasibility/relset/**` (tests), this
  document. No pin, product default, desktop file, runtime file or composite file is modified; no
  network, no push, no Vita3K process.
- Outputs (ignored): `/home/fred/projects/zeldaWWHDRecomp/build-f14/`.
