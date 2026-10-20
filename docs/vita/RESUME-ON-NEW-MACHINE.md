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

On 2026-10-20 the trunk and all 63 tags were pushed to `origin` (`origin/development` follows local `development`); a new machine needs `git clone` + `git checkout development`, and the project memory plus `.opencode/orchestrator.md` come with the clone because they are tracked. Because the memory store is live, it shows as modified as soon as a task or knowledge entry changes — checkpoint the WAL, then commit it before a handover.

Every branch deleted during the cleanups is preserved as a tag `archive/<name>-<date>` (**58 tags**, local only unless pushed). Push them with `git push origin --tags`. A single-file safety net of every ref exists too: `build-vita-private/migration/refs-20261020.bundle` (20 MB, `git bundle verify` reports complete history).

## 2. Set up

1. Clone/copy the repository and check the state:
   ```sh
   git log --oneline -3 development   # expect the handover commit at the top
   git tag -l 'archive/*' | wc -l     # expect 58
   ```
2. Rebuild the tool stack. **The documented stack is ARM64; a x86-64 host cannot run it** (the vendored `build-vita-tools/local-stack/vitasdk/bin/arm-vita-eabi-gcc` and `host-toolchain/bin/aarch64-conda-linux-gnu-gcc` are both `ELF aarch64`). On such a host:
   - install the official **VitaSDK for x86-64** (provides `arm-vita-eabi` plus `vita-elf-create`, `vita-make-fself`, `vita-pack-vpk`);
   - use the host's native **gcc** as the host compiler (`CC=... python3 -m unittest ...`), never the aarch64 conda one;
   - install **Vita3K x86-64**;
   - copy the **prebuilt `libvitaGL.a`** (5.6 MB, ARM static library — host-independent) and `libshacccg.suprx` from a working machine;
   - then fix the four hard-coded absolute paths: `scripts/vita/ww05/game_build.py:17` (local stack), `:404` (`libvitaGL.a`), `:405` (`arm-vita-eabi`), `scripts/vita/ww05/stage_plan.py:29` (`libshacccg.suprx`).
   - A machine that already holds the data can be seeded directly: the disc ISO (verified `sha256 ec2d4ee7…436df`), and the firmware partitions `vs0`/`sa0`/`ur0` (1473/20/1 files).
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
  - Next stop after that admission: **`unsupported MMIO read address=C84F0500 pc=80322E04`**. Identified (2026-10-20): the pc is `GXPeekZ` (`0x80322DE8-0x80322E10`), the address is `GXPeekZ(320, 240)` — a computed **EFB depth peek at screen centre** — called from `dDlst_peekZ_c::peekData` (`0x80085BFC`) queued by `dKyr_sun_move` (`d_kankyo_rain.cpp:663`); the peeked depth gates the **sun's glare** (`mVizChkData[i] >= 0xFFFFFF`). Emitter: `mmio_read`'s final `else` (`vita/host/boot_host.c:660`) → `ww_host_fail`; **no `0xC8` case exists in the Vita host**, and the desktop Aurora handler (`runtime/host/src/main.c:3308`) is not linked into it. Options, lowest risk first: (A) a **real depth readback** (depth `glReadPixels` + GX-Z conversion; vitaGL support unverified) — the only one that returns real pixels; (B) the archived compile-time `WW05_EFB_PEEK` returning constant 0 (measured 308 presents, **fabricated**, diagnostic only); (C) constant `0xFFFFFF` (fabricates a visible sun; reject). Evidence: `build-vita-private/mmio-307-investigation/REPORT.md`.
- **Draw census** is on the trunk (`ae82cff0`) with a builder flag `--draw-census` (default OFF); use it to answer "is a logo-sized textured draw submitted?" once the guest passes 307.
- **CPU identity guard** is on the trunk (`21abc3fa`) as a permanent diagnostic (no false positives over a full run).

## 5. Reading the memory

The authoritative record is `.opencode/memory/memory.db` (tasks + knowledge). **Start with the knowledge entry `handover-next-action-20261020`**: it names the trunk revision, the current wall (present 307 / `0xC84F0500` / `pc 80322E04`), the next four steps and the constraints. Then read `vita-desktop-edge-refusal-gate` (gate policy + MMIO wall), `guest-ppc-address-mapping-and-efb-peek-gate` (offline PPC address mapping, the Aurora-gated EFB handler) and `rendering-title-sky-visible-20261020` (state of the title work). Task `GX-TITLE` tracks the title gate; `WILD-CTRL` the wild-control-transfer family (now worked around).

## 6. Pitfalls learned the hard way

- A guest/process check is **not** a session check: before removing a workspace, verify every session and worktree using it (this cost one worktree on 2026-10-20; the branch survived).
- ARM addresses are **build-specific**: re-derive from the build's own `ww05.map`; the velf `nm` symtab is pre-relocation garbage.
- `0x8e7f7908`-style raw dumps: `objdump --start-address` on a raw binary means **file offsets**, not VMAs.
- Reading guest memory: host `0x400000000 + vaddr`.
- Full-image pixel scans only; state the threshold. A strided sample once erased a 656-pixel cluster.
- Never announce a run's verdict while the run is live (`live-end.txt`, `reason=`, fault count first).
