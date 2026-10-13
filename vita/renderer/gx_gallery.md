# VITA-WW-18: automatic night gallery

## Objective and scope

Build an image-by-image record of recent capture runs as richer FIFO/MEM1
evidence arrives. Implementation lives on `vita-ww-18-drawcov` in
`/tmp/opencode/ww18`; images, state, logs, fixtures and firmware stay ignored
under `build-f6/ww18`. No push, network or capture-lane source change.

`scripts/vita/gx/gallery.py` only **reads** lane 16's run directories. It reuses
`extract.py`, validated snapshot loading, `image.py`, the existing PNG encoder
and the watched Vita3K runner. It never writes a journal/manifest/request into
build-f5 or borrows resources from another run.

## Start or resume

```sh
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/gallery.py \
  --source /home/fred/projects/zeldaWWHDRecomp/build-f5 \
  --build build-f6/ww18 --output build-f6/ww18/images \
  --watch --interval 30 --recent-hours 24 --notify
```

For a one-pass import, omit `--watch --notify`. `--recent-hours 0` includes all
captured runs. A file lock prevents concurrent gallery writers.

`--notify` exits after **one new image or new error/defer notification**, allowing
the supervising session to report immediately and restart the same command.
With no new evidence it keeps scanning. This option requires that supervisor;
it is not a self-restarting daemon. Without it, `--watch` keeps running and
prints updates directly. Every emulator run still watches CPU/logs and aborts
silent low-CPU hangs early; the gallery's idle polling is not an emulator run.

The watcher PID, start, source and scanning/waiting/stopped state are in
`images/WATCH.json`. `images/UPDATE.json` contains the most recent notification.
Stopping this owned watcher must never stop lane 16's processes.

## Discovery and correctness

- Only isolated `vita3k-run-<32 hex>` **data** directories with a journal,
  report and `ww05.mem1.json` are selected. Synthetic contract-test directories
  and evidence copies are excluded. Existing numbered
  `ww05.mem1-*.snapshot.json` files are a supported fallback when the standard
  manifest is absent, so the first delivered real run can seed the gallery.
- The standard manifest must use the documented `gx-mem1-snapshot-v1` format.
  Unknown formats are reported, not guessed.
- A manifest's FIFO digest is matched against whole-command prefixes. Producer
  draw numbers remain timing provenance, never an assumed digest extent. No
  digest is rebound to a different prefix. Numbered candidates prefer the
  richest matching logged-draw extent, then the latest snapshot.
- Snapshot version, binary SHA, physical regions and bounds are validated
  before replay. Partial deliveries defer visibly and retry when complete.
  Repeated identical errors do not flood notifications.
- Source hashes are compared again after staging, before any GPU launch. A run
  changed by its writer mid-scan cannot be published under the old identity.
- Run/journal/report/manifest/renderer-commit identity prevents repeat replay
  after restart. Changed input or renderer revision creates a new immutable
  gallery version; earlier PNGs and failed attempts are not overwritten.
- Stable rendering refusals remain recorded rather than retrying forever.
  Later input/code changes permit another attempt. Nothing makes a rejected
  backend or missing resource into an image success.

## Morning index

Open **`build-f6/ww18/images/INDEX.md`**. Inline PNGs show frames in arrival
order without opening every link. Each row links to an entry containing
source report/snapshot/FIFO provenance, then to the exact PNG and its source/SHA
manifest. It records:

- run identifier and capture-code SHA;
- run report date, explicitly **UTC**;
- snapshot-bound journal draws;
- actual replay submissions (`rendered`);
- lane 16's renderer total, kept separate from logged/replayed coverage;
- dimensions, file byte size and complete PNG SHA-256;
- verdict and a prominent `NONBLACK` signal.

Artifacts are beneath `images/<run>/<revision-attempt>/frame/`. Guest fixture
resources are separate under `gallery-fixtures`, not placed in the image gallery.
`GALLERY.json` is the durable ledger; `INDEX.md` updates by atomic replacement.
An existing index without this gallery's ledger is treated as user work and
refused instead of overwritten.

## Immediate scene signal

`event=gallery-nonblack` means a produced, visible image has
**`distinct_rgba > 1`**, with nonzero display alpha and an image-captured verdict.
This is the user's progress signal, not a hardware/gameplay certification.
Coverage gaps remain attached when present. A failed/transparent readback cannot
raise it. Raw readback and the declared display-alpha conversion remain in the
per-image manifest.

## Verification and execution evidence

- Test-first: the new gallery tests failed on the missing module, then passed.
- **101 Python tests pass**, including 20 gallery contract scenarios. The
  repository audit and whitespace checks pass. This unit changes no C++ runtime
  or shader behavior; the existing GPU/PNG path supplies the images.
- Tests cover read-only source bytes, standard and numbered discovery, exact
  bound prefixes, duplicate suppression/restart, richer-version preservation,
  separate render counts, atomic index content, partial delivery/retry,
  nonuniform signal, blocked/failed image handling, age filtering and source/
  output isolation. They mock rendering; they are not GPU pixel evidence.
- Watched real-run smoke test at `build-f6/ww18/gallery-smoke` discovers
  `33aa66c91b9d49a1b78cbabdeb1f86c3` automatically, replays its validated
  snapshot 2, and records **2 journal draws / 2 replay submissions / 9 lane 16
  draws**, 960×544. The uniform-black PNG retains SHA
  `c871acff3fc84ccec64ed29b05f2b1549e05b5067e2e5adf193b60d5490fdae5`;
  **no NONBLACK signal**. The index and update/PID state were inspected.
- The ongoing monitoring objective is richer real dumps and the first validated
  nonuniform image. Actual watcher launch/PID and later results are runtime
  evidence in `images/WATCH.json`, `UPDATE.json` and the ledger, not promises
  inferred from this document.

## Keeping the night watch within the staging quota

The third gallery import stopped on `Disk quota exceeded` before publication.
The filesystem still had free space, but `/tmp` enforces a per-user quota.
Only completed lane-18 emulator profiles were deduplicated: identical runtime
and `vs0`/`sa0` files, verified by SHA and mode, now share private hardlinks.
All paths and bytes remain; no dumps, images, logs or lane-16 runs were removed.
The private operation record is `build-f6/ww18/quota-recovery.json`.

Image replay now opts into `launch.py --share-readonly-stack`. A content-addressed
cache under `build-f6/ww18/readonly-stack-cache` supplies firmware/runtime
hardlinks; it never links back to the original local stack. The sandbox binds
these trees read-only. Cache bytes and mode are checked on every staging copy;
corruption fails loudly. Cross-filesystem destinations use independent copies.
VPKs always stay independent because staging can replace them. User profiles,
caches, fixtures, reports and captures are still private to each run. Ordinary
launcher calls keep their existing copy behavior unless explicitly opted in.

The interrupted run `24356e49e61d44c8b088bc9634afb278` then completed under the
watched shared-stack path: **2 logged draws / 2 replay submissions / 9 lane-16
draws**, the same black PNG SHA as above, and no scene signal. Its manifest
records dirty source hashes and `readonly_stack_shared: true`; install and run
stopped on completed evidence at 0.525 and 5.142 seconds on Linux/Vita3K, not a
physical Vita. The four staging scenarios failed on the missing helper first,
then passed; the full Python suite now has **105 passing tests**. All indexed
PNG hashes were checked unchanged after the quota recovery.

## Bounded phase deadlines

`launch.py --timeout` and both direct phase-runner entry points now require a
finite, positive deadline no greater than 600 seconds, matching WW05's phase
bound. NaN and either infinity are refused before building, staging, opening
phase evidence or starting a process; zero, negative and over-ceiling values
are also refused. The watched runner uses the same validator rather than relying
on comparisons that are always false for NaN. Defaults and the gallery's
120-second healthy-run ceiling remain unchanged.

The timeout regression tests failed first for the CLI and both runners, then
passed with no build/process/evidence side effects. The full GX Python suite has
**111 passing tests** after the three night-review corrections. These deadline
tests mock execution and do not start an emulator or alter a running watcher.
