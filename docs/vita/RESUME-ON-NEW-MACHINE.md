# Resuming this project on a new machine

Date: 2026-10-20. State: trunk `development` = the tip of this branch (this handover doc is the last commit). No run in flight.

## 1. What to copy

| What | Path | Size | Notes |
|---|---|---|---|
| Repository | this checkout | `.git` 44 MB + code ~18 MB | branches + 58 `archive/*` tags travel with git |
| Single-file export (optional) | `build-vita-private/migration/refs-20261020.bundle` | 20 MB | every branch and tag; `git clone <file>` restores the full history without a network |
| Project memory | `.opencode/` | 1.7 MB | `memory/memory.db` (tasks + knowledge), `orchestrator.md` (model pools), `skills/` |
| Evidence (optional but useful) | `build-f5/runtime-next/preserved-provenance-*` | ~160 MB | reports, hashes, RELEASE notes |
| Run reports (optional) | `build-f5/runtime-next/*/REPORT.md`, `dispatch_disasm.txt`, dumps | tens of MB | each run dir's `profile/` was pruned; logs live at the run root |
| OpenCode session history (optional) | `~/.local/share/opencode/opencode.db` | 2.6 GB | restores the conversation history |
| **Private, never in git** | `GZLE01.iso`, Vita3K firmware partitions (`vs0`, `sa0`, `ur0`), keys/saves | — | copy out-of-band, same trust level as before |

**Do not copy** `~/.local/share/opencode/auth.json` into the repository or any shared location: it holds credentials. Re-authenticate on the new machine instead.

**Do not copy** build trees (`build-f5/ww05`, `build-f6`, `build-vita-private/*`): rebuildable, and they were the bulk of the old disk usage.

## 1b. Branch inventory at handover (2026-10-20)

Trunk: **`development` = the last commit in this file's history** (`5830a197`, which includes the class-admission merge).

| Branch | Ahead / behind `development` | State | On `origin`? |
|---|---|---|---|
| `task/cycdomain-transfer-probe` @ `613318ff` | 13 / 40 | superseded experiment; its worktree holds uncommitted diffs — kept, never forced | pushed |
| `task/HIGH-BLOCK-GLOBAL-CTX` @ `862b2ce1` | 0 / 40 | merged content (ancestor); its `/tmp` worktree holds uncommitted diffs — kept, never forced | no |
| `ram-guard-title-boundary` @ `547a7d0a` | 0 / 178 | branch checked out in the primary working copy (dirty `AGENTS.md`) | no |

Merged and then deleted on 2026-10-20: `task/draw-class-admission` (now `7f2d45d4` + `5830a197` on the trunk), `task/admit-desktop-edge-8015ea5c`, `task/textured-draw-census`.

The repository went from **124 local branches to 5**: 103 were ancestors of `development` (deleted losslessly), 18 divergent ones were preserved as tags first.

On 2026-10-20 the trunk and all 63 tags were pushed to `origin` (`origin/development` follows local `development`); a new machine only needs `git clone` and `git checkout development`.

Every branch deleted during the cleanups is preserved as a tag `archive/<name>-<date>` (**58 tags**, local only unless pushed). Push them with `git push origin --tags`. A single-file safety net of every ref exists too: `build-vita-private/migration/refs-20261020.bundle` (20 MB, `git bundle verify` reports complete history).

## 2. Set up

1. Clone/copy the repository and check the state:
   ```sh
   git log --oneline -3 development   # expect 5830a197 at the top
   git tag -l 'archive/*' | wc -l     # expect 58
   ```
2. Rebuild the tool stack (`build-vita-tools/local-stack`: native AArch64 GCC, CMake, Ninja, VitaSDK), or reinstall it per `AGENTS.md`.
3. Put the disc at a stable path and symlink it into run profiles as before (`GZLE01.iso`).
4. Vita3K + firmware: a working profile needs `vs0` (1473 files), `sa0` (20), `ur0` (1) under `profile/data/Vita3K/Vita3K/`. Staging seeds only `ux0`; copy the partitions from a known-good profile. A profile without them idles at ~0.1 % CPU and never creates `ww05.log`.

## 3. Running the game (the configuration that works)

- `cpu-opt: false` — **without it the guest dies at ~310 presents** (data-execution fault, only with the optimised Dynarmic path; `ClearCache` epochs = 0 during the fault).
- `disable-surface-sync: false` — with it `true`, frames capture as **all zero** (surface writeback omitted; `glReadPixels` reads a stale buffer).
- `texture-cache: true` (leave default; `false` exits at 307 presents for an unrelated reason).
- Fixture: `ww05.request`, `ww05.continue` (64), `ww05.audio`, plus frame keys (`frame-start=300`, `ending-present=363`; `main.c:107` rejects an ending before the start).
- Run through `/tmp/opencode/run_guest.sh` (patches `pref-path`, memory floor, guest lock, watcher). For longer windows, copy the watcher into the run dir and import the canonical one with `deadline_seconds=...`; never fork the canonical script.
- Frames land in `.../bluewake-ww-fixture/frames/` (`frame-NNN.png`, `-rgba.png`, `.json`) — **not** at the run root.

## 4. Where the work stands

- **Crash**: worked around (`cpu-opt: false`). 368 presents, 24 M edges, no fault.
- **Title background**: renders (night sky, clouds, moon; max RGB 255) from present ~312 on the builds that reach it.
- **Logo**: **never observed**. Current wall: the run stops at **307 presents**.
  - Desktop-edge gates for the draw family are **solved and merged** (`development = 5830a197`): the generated actor-draw class admits four members (`daBg_Draw`, `daSea_Draw`, `daVrbox_Draw`, `daVrbox2_Draw`), invariants held, fail-closed preserved, generator rejects malformed switches.
  - Next stop after that admission: **`unsupported MMIO read address=C84F0500 pc=80322E04`** — investigate MMIO/address provenance, not more draw admissions. Offline address mapping is available: `source-port/game/src/d/actor/d_a_*.cpp` annotate `.text` ranges (`/* 8015E3F0-8015E530 .text daVrbox_Draw__FP11vrbox_class */`); `80322E04` is not one of those starts. Note `runtime/host/src/main.c` already has an EFB-peek handler for `0xC8xxxxxx` reads, gated on `aurora_enabled` (off in this build).
- **Draw census** is on the trunk (`ae82cff0`) with a builder flag `--draw-census` (default OFF); use it to answer "is a logo-sized textured draw submitted?" once the guest passes 307.
- **CPU identity guard** is on the trunk (`21abc3fa`) as a permanent diagnostic (no false positives over a full run).

## 5. Reading the memory

The authoritative record is `.opencode/memory/memory.db` (tasks + knowledge). Start from the knowledge entry `rendering-title-sky-visible-20261020` (state of the title work) and `vita-desktop-edge-refusal-gate` (gate policy + MMIO wall). Task `GX-TITLE` tracks the title gate; `WILD-CTRL` the wild-control-transfer family (now worked around).

## 6. Pitfalls learned the hard way

- A guest/process check is **not** a session check: before removing a workspace, verify every session and worktree using it (this cost one worktree on 2026-10-20; the branch survived).
- ARM addresses are **build-specific**: re-derive from the build's own `ww05.map`; the velf `nm` symtab is pre-relocation garbage.
- `0x8e7f7908`-style raw dumps: `objdump --start-address` on a raw binary means **file offsets**, not VMAs.
- Reading guest memory: host `0x400000000 + vaddr`.
- Full-image pixel scans only; state the threshold. A strided sample once erased a 656-pixel cluster.
- Never announce a run's verdict while the run is live (`live-end.txt`, `reason=`, fault count first).
