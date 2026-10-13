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
