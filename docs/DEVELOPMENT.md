# The `development` branch

`development` is the long-lived trunk for the Vita work and for private packages. It collects
finished, reviewed work so a package does not have to name a different base and a pile of flags per
trial. It is not `main`: `main` is untouched and nothing here is pushed.

## Rules

- Every task or POC branch is created from `development` (for example `task/<id>`), not from an
  ad-hoc base.
- A task merges back into `development` only when it is finished **and** independently
  reviewed/validated. A POC result is not a merge request by itself.
- Packages build from `development`.
- Experimental features stay OFF by default. Their code may be merged behind a flag, but a package
  must opt in explicitly.
- The POC branches are evidence, not bases: `poc/global-ctx` (`76ef2921`), `poc/skip-logo`
  (`face5385`, head `e41ee920`), `poc/perf-combined` (`715f3065`) and any later perf rebuild. Their
  commits are rebased or cherry-picked onto a `task/<id>` branch created from `development`.
- `main` is not touched, and no branch is pushed from this work.

## What is merged

`development` started at `integration/gameplay-perf` `92c2e7c4` and records these merges:

| Merge | Branch and head | What it adds |
| --- | --- | --- |
| `60dcac7a` | `poc/perf-combined` `715f3065` | turn-cap and program-cache options, the program cache, cycle-domain work |
| `511ff6d4` | `poc/global-ctx` `76ef2921` | scoped global-ctx transform, guest MEM1/RAM guard, their tests |
| `a26bf4bd` | `poc/skip-logo` `e41ee920` | host and composite title-logo skip, behind `WW_SKIP_TITLE_LOGO` |
| `53a8b731` | `poc/pa-assets` `87f23cd1` | the source-port track under `source-port/`: the SwitchWaker import, the ILP32 corrections, the E1/E2 Vita skeleton and run harness, the GX/GD stub and profile/static-init harnesses, up to M1 (the first visible Vita frame); no recomp default changes |

Conflict resolutions:

- `poc/global-ctx` vs `development` in `vita/host/CMakeLists.txt`: kept the new `WW05_RAM_GUARD`
  and `WW05_GLOBAL_CTX` options alongside the existing `WW_EDGE_OBSERVATION_ADMIT` option. The RAM
  guard is a safety repair, not a perf lever; the global-ctx transform is off.
- `poc/skip-logo` vs `development` in `scripts/vita/ww05/game_build.py` and
  `vita/host/test_game_build.py`: kept the perf options and the skip option together. Argument order
  is turn cap, program cache, rejection capsule, skip title logo, then the experimental-off options.
- `poc/pa-assets` merged without conflicts. It only adds `source-port/`, two status notes, the
  import script and `.gitignore`/rights text; it does not change the translator or runtime defaults.

## Private builder defaults

`scripts/vita/ww05/game_build.py` now builds the reviewed perf profile with no per-package flags.
The flags are still available; `--no-<name>` disables each one.

| Setting | Default | Where |
| --- | --- | --- |
| `WW_TURN_CAP` | `4096` | `--turn-cap` (zero keeps CMake's default) |
| `WW_PROGRAM_CACHE` | ON | `--program-cache` / `--no-program-cache` |
| VI clock | ON | `--vi-clock` / `--no-vi-clock` |
| destination-alpha admission | ON | `--dst-alpha-admit` / `--no-dst-alpha-admit` |
| edge observation admission | ON | `--edge-observation-admit` / `--no-edge-observation-admit` |
| idle deadline advance | ON | `--idle-deadline-advance` / `--no-idle-deadline-advance` |
| rejection capsule | ON | `--rejection-capsule` / `--no-rejection-capsule` |

Deliberately OFF by default, and still available behind their own switches:

| Setting | Default | Where |
| --- | --- | --- |
| `WW_SKIP_TITLE_LOGO` (composite scene skip) | OFF | `--skip-title-logo` |
| `WW05_GLOBAL_CTX` | OFF | passed off by the builder; CMake option remains |
| `WW05_DISPATCH_ABI_SENTINEL` | OFF | CMake option; not forwarded on |
| `WW05_OBSERVE_NATIVE_CTX` | OFF | CMake option; not forwarded on |
| `WW05_QUIET_LOGGING` | OFF | CMake option; passed off |
| guest pad input | OFF | `--pad-input` |
| guest-cycle controls beyond the VI clock | OFF | not forwarded |

`WW05_RAM_GUARD` is ON for the Vita target. It repairs a corrupted guest CPU/MEM1 state and is inert
when the state is intact; it is part of the global-ctx merge, not one of the perf levers.

## Runtime pin and the L1 poll-skip

The pinned RecompCore in this tree is `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`. The L1 poll-skip
change is RecompCore `cab530113583330ac3640b4de62f740274cc7f5d` (branch `perf/poll-skip-alias`), and
it is **not** in that pin. It was applied to the sealed trial packages as one substituted object, not
through the source pin, so a package built from this base does not carry it yet.

The change is recorded in `patches/recompcore/0169-runtime-resolve-the-poll-word-instead-of-refusing-on-.patch`
(a historical record, not a build input). To make it part of every package:

1. publish `cab5301` on `chrissotraidis/RecompCore` branch `bluewake-next` (maintainer action), then
2. update `scripts/vita/armv7/build.py`, `scripts/vita/ww05/game_build.py`,
   `scripts/builder/profiles/bluewake.sh` and `config/dependencies.lock.json` to that commit, and
3. rebuild the L1 evidence with the new pin.

Until then, any fps claim for a package built here needs that gap stated. Per-package flags do not
fix it.

## Not in this branch

- Source-port implementation work after `87f23cd1`. The ILP32 corrections
  (`source-port/ilp32` `86b562fd`) and the E3 GX/GD stub (`poc/e3-gx-stub` `1103e5dd`) are
  ancestors of `87f23cd1`, so they are part of the merge; the merged branch tip is `87f23cd1`.
- `poc/perf-ramguard`, `poc/dusk-transfer`, `poc/native-ctx-guard` and the B3/ctx POC branches.
- Private packages, builds, saves, disc data and memory floors. They stay outside the repository.
- Any behaviour change that has not been reviewed. Merged experimental code is present, off.
