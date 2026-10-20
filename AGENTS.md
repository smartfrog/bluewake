# Vita port instructions

This fork is the PS Vita product home (`smartfrog/bluewake`); upstream is read-only, with no upstream PRs.
The goal is visible title/menu, verified controls and normal-speed gameplay in Vita3K; audio is deferred.

## Project state and machines

- Read the local `task-board` and relevant `knowledge`, starting with `project-rules`. SQLite memory under
  `.opencode/memory/` is authoritative, not old Markdown backlogs. All agents read; only memory-manager writes it.
- `development` is the local integration trunk. Code tasks branch as `task/<id>` from its verified HEAD and
  integrate only after independent review/checks. Leave `main` untouched; no push without explicit permission.
  Retrieve `development-branch-integration` for the full flow; do not switch/reset someone else's workspace.
- The host may be any Linux machine (the current one is **x86-64**); never assume a host architecture in
  scripts, paths, binaries or addresses. Rebuild or reinstall host tooling per machine instead of copying it.
- The Vita target is **ARMv7-A/Cortex-A9, NEON, hard-float, ELF32/ILP32** (4-byte pointers). Native host tests
  do not certify Vita structure layouts or ABI.
- Symbolize host PCs with the matching ELF and `/proc/<pid>/maps` load bias, never as raw file offsets or as
  addresses taken from another host or another build.

## Build wiring

- Two tracks coexist: native `source-port/` and the AOT recompilation fallback (`vita/host`, `vita/renderer`).
  `source-port/` and newer harnesses are branch-dependent; verify Git refs before assuming code is absent.
  Retrieve `runtime-sourceport-architecture` for import boundaries/provenance; do not create another working fork.
- `vita/feasibility` contains diagnostics. Read relevant `docs/vita/VITA-WW-*.md`, checking dated claims against code.
- `vita/host/CMakeLists.txt` reuses selected `runtime/host/src` services and builds the translated module
  through `cmake/composite`. The static module borrows the host's CPU/runtime; do not add a second CPU owner.
- Aurora itself is disabled, but `GXRUNTIME_ENABLE_AURORA_RECOMP=ON` supplies the GX frontend/gxcore used
  by the Vita renderer. The rendering backend is vitaGL, not the desktop Dawn backend.
- Dependencies are clean, exact-pinned checkouts in `ref/vita-ww-01/{RecompCore,DolRecomp}`, not `ref/recompcore`.
  Pin checks involve the Vita scripts, `config/dependencies.lock.json`, and `scripts/builder/profiles/bluewake.sh`.
  Do not edit ignored dependencies or replay historical patches to bypass these checks.
- Fix generators/preparation scripts rather than generated chunks. Preserve disc and composite-digest validation.
- Reuse sealed translated objects where valid; do not regenerate canonical exports or repeat full rebuilds speculatively.
  CMake caches absolute paths/flags: pass explicit ON/OFF and verify emitted flags/cache, not just intended argv.
  Reused composites must carry the current context macros; see `build-configuration-evidence` and
  `build-composite-digest-requirement`. Keep large scratch under ignored `build-*`, not quota-limited `/tmp` tmpfs.

## Recompilation build

- Use Python 3.11+ and the GameCube USA `GZLE01` revision 0 disc, not Wii U WWHD.
- `scripts/vita/ww05/game_build.py` uses existing tools without downloading them. Paths come from
  `scripts/vita/vita_workspace.py`: the private stack is `<repo>/build-vita-tools/local-stack` (override
  `BLUEWAKE_VITA_STACK`), and the renderer archive is the official VitaSDK package at
  `<stack>/vitasdk/arm-vita-eabi/lib/libvitaGL.a`. CMake, Ninja and the host compiler come from the distribution
  (`CMAKE`, `NINJA`, `CC`). On a new host, reinstall these components for that host; do not copy a foreign toolchain.
- Output defaults to `<repo>/build-f5/ww05`. To isolate a build, set absolute
  `WW05_BUILD_ROOT="$PWD/build-vita-private/ww05"` for build, stage and launch commands; `--out` must equal it.
- Full builds also require the three private vectors (`private/{external,fp,decrementer}-vector.bin`), and
  `--prepare-only` does not create them. They can be reconstructed exactly from `main.dol`: take the 152-byte
  template at `0x80301D8C` and replace bytes 104-107 with `0x38600000 | exception` for exception numbers 4
  (external), 7 (FP) and 8 (decrementer). The code verifies the resulting sha256; never substitute a constant
  or a different lineage.

Run from the repository root, after checking the prerequisites:

```sh
CC=gcc python3 -B scripts/vita/ww05/game_build.py --disc "/absolute/path/GZLE01.iso" --jobs 2
```

- Add `--prepare-only` to stop after extraction, translation and composite verification. No Vita3K process is launched.
- The module defaults to `--opt-level 1` with `-fno-tree-dominator-opts`; retain `-ffp-contract=off` and
  `-fno-fast-math`. Bound parallelism so the machine stays usable: the default is 2 jobs, targeted builds use 1,
  and any higher value must fit the machine's real capacity. Feature defaults differ by branch: inspect `--help`,
  the actual cache and compile commands before comparing builds.
  Toolchain/reuse details: knowledge `build-reuse-toolchain` and `build-vita-tools/local-stack/USAGE.md`.

## Focused verification

```sh
python3 -B vita/host/test_build_root.py
CC=gcc python3 -B -m unittest discover -s vita/host -p 'test_host.py'
bash scripts/audit_repo.sh
```

- `CC` is the native host compiler, never `arm-vita-eabi-gcc`. The private stack is resolved by
  `vita_workspace.py`; `BLUEWAKE_VITA_STACK` is the only override.
- The Python host harness generates synthetic vectors/contracts before CMake with `WW05_TESTS_ONLY=ON`;
  configuring `vita/host` alone misses these fixtures. Stop Vita3K first. Suites sharing
  `build-f5/services-tests` must not run concurrently, and the suite must not run while a build competes for
  CPU: timing probes fail under load. Narrow discovery with `-p 'test_NAME.py'`.
- Inspect `build-evidence.json`, `footprint.json`, `vita/ww05.map`, and `vita/compile_commands.json` before
  execution; completed builds preserve snapshots under `artifacts-*`. Compilation is not physical Vita acceptance.

## Execution and private data

- `scripts/vita/ww05/stage_plan.py` stages without launching; `launch.py` installs/runs in fresh private profiles.
  **Status on this fork:** the staged runner is not usable on this host yet — its `RUNTIME_SHA256` pin is the
  previous host's build and its config templates predate the verified run configuration. Until it is repaired or
  replaced, a hand launch is allowed only from a brand-new private profile with `pref-path` re-pointed at that
  profile, authenticated owned processes and both logs preserved. Record it as a hand launch, not runner evidence.
  Launch requires a clean source checkpoint, including no untracked files; report blockers rather than deleting user files.
- Read `safety-run-envelope` before trials: scope-specific memory admission/cgroup limits, zero swap and OOM-stop
  are mandatory. No RAM/RSS poller or gate bypass. Serialize install+guest under
  `flock -w 600 /tmp/opencode/ww-guest.lock`; signal only authenticated owned guest processes, never the watcher.
- Use the canonical two-log fault watcher, not stale per-run copies (`runtime-guest-watch-classification`).
  Thirty seconds of low CPU/silence can be Mesa shader warm-up; reconcile the selected watcher's threshold
  with the reviewed 120-second silent-idle fix before running. Preserve both guest and emulator logs.
- Bind requests to the packaged guest's source identity, not a separate host-wrapper revision. Preserve exact argv,
  packages, failed profiles and evidence. Without explicit continuation, the first GX command is an intentional stop,
  not a title/render result; see `runtime-trial-harness-preconditions`.
- Unsupported guest services must fail explicitly: never skip instructions, fabricate completions, or change FP
  semantics to work around an emulator defect. Vita3K results do not establish hardware behavior or frame rate.
- Emulator patches are a last-resort diagnostic only, never acceptance: fix our code and the configuration first,
  prove with an unpatched control run that the behaviour is emulator-only, then patch. The target remains real
  Vita hardware. See `docs/vita/VITA3K_PATCH_POLICY.md`.
- Acceptance belongs in task `GAMEPLAY`: logo cadence, present counters and diagnostic/sentinel builds are not
  controllable-gameplay FPS or a valid matched performance comparison.
- Keep discs, extracted/translated game files, game VPK/SELF/ELF files, firmware, keys and saves private and untracked.
  Use ignored `build-*` outputs and save copies; never upload these artifacts or delete player data.

## Measuring runs and diagnosing translated-code faults

- A run is valid only from a **brand-new private profile**. A reused profile reproduces a false stop with
  identical artefacts; one left by a killed run can carry a partial `.bwsc` program cache. Install afresh each time.
- Launch through the canonical runner. A hand-launched profile keeps its absolute `pref-path` and then reads the
  *sibling* run's files: the guest stalls near 1 % CPU, never opens `app0:/main.dol` or `libshacccg.suprx`, and
  `/proc/<pid>/maps` has no `0x4…` guest block. If launching by hand, re-point `pref-path` at the profile first.
- Guest memory is not identity-mapped: read a guest vaddr `X` at host `0x400000000 + X`. The module's own code and
  data vaddrs are stable across runs, so they need no load bias; guest-space addresses always do.
- Pixel verdicts come from **full-image scans** (960×544 is trivial) with the threshold stated. Never stride-sample:
  a step-2 sample erased a 656-pixel cluster and produced two false "all black" calls in one night.
- **ARM addresses are build-specific.** Re-derive every address against the exact `.velf`/`ww05.map` of the build
  under test; never carry addresses across builds. Forgetting this wasted five gdb attempts.
- Keep the three address spaces apart: **PowerPC guest PCs** (`0x80xxxxxx`, fixed 4-byte instructions, no state
  bit), the **translated ARMv7 module** (`.text` ~`0x81…`–`0x97…`, Thumb-2, ILP32) and the **host process**
  (addresses from the host's own mapping, symbolized with the matching ELF). A PowerPC PC is not an ARM address; disassembling one inside the module is meaningless.
- Thumb interworking: on ARMv7 a `blx` target's bit 0 selects the instruction set (odd = Thumb, even = ARM). A
  function pointer that loses that bit, or a PowerPC address used as a function pointer, makes the CPU decode
  Thumb bytes as ARM and run data as code.
- Classify a fault before interpreting it: a fault PC inside `.rodata`/`.bss` means *data executed as code*, not a
  wild store. Check the bytes at the PC and the register arithmetic before calling it memory corruption.
- An opt-in switch must be **compile-time or fixture-driven, never an environment variable**: in the guest
  `environ` is NULL (`_start` never installs `envp`), so a `getenv` gate is dead and looks like "never fires".
- When a check cannot be run, report it as **unobserved**, not refuted. A fail-closed check must name the invalid
  state and print the values (`pc`, `lr`, target value), never fall through.
- Build trees are reproducible from their `build.sh`; releasing one is fine when its recipe, identity files and
  artefact hashes are preserved. Run logs, `frames/`, `guest.maps`, `gdb*.log` and `.bwsc` caches are evidence:
  keep them.

## Machine hygiene

- **Do not touch anything outside the checkout.** Never delete, move or rewrite files in the user's home, other
  repositories, saves or system directories. Do not kill processes you do not own.
- **Leave the machine usable.** Bound build and test parallelism (see the build rules above), keep disk space
  healthy, and do not saturate the CPU or disk while the person is using the machine. Stop heavy work you started
  when it is no longer needed.
- **Say what you changed on the machine.** If you installed, downloaded or generated something outside the
  checkout, list it and how to undo it.
- Commits, branches, issues and comments carry **human names only**. Never add an AI tool, model or bot as author,
  co-author or committer, and never add "Generated with" lines or bot signatures.
